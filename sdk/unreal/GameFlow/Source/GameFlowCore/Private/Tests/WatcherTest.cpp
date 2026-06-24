#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "GameFlow/SidecarTransport.h"
#include "GameFlow/GfWatcher.h"
#include "GameFlow/GameFlowLogger.h"
#include "GameFlow/GameFlowOptions.h" // IGameFlowDispatcher
#include "Fixture.h"

#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Containers/Ticker.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfWatcherTest, "GameFlow.Watch.FiresOnPush",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGfWatcherTest::RunTest(const FString& P)
{
    if (!Fixture::NodeAvailable())
    {
        AddInfo(TEXT("node >= 23.6 absent; skipping"));
        return true;
    }

    Fixture Fx(TEXT("--players-capacity=2"));
    TestTrue(TEXT("fixture started"), Fx.IsValid());

    FNullGameFlowLogger Log;
    FInlineDispatcher Disp;
    FSidecarTransport T(FString::Printf(TEXT("http://127.0.0.1:%d"), Fx.Port), 3000, Log);
    FGfWatcher W(T, Disp, Log);

    int32 Hits = 0;
    FDelegateHandle H = W.Subscribe([&](const FServerInfo&) { Hits++; });

    // The fixture sends an initial frame to every watch client on connect.
    GfPumpUntil(*this, [&] { return Hits >= 1; }, 3.0);
    TestTrue(TEXT("got initial frame"), Hits >= 1);

    const int32 Before = Hits;
    Fx.ControlPost(TEXT("/push-update"));
    GfPumpUntil(*this, [&] { return Hits > Before; }, 3.0);
    TestTrue(TEXT("got a pushed frame"), Hits > Before);

    W.Unsubscribe(H);
    return true;
}

// ---------------------------------------------------------------------------
// Regression for the re-entrant use-after-free on the peer-close/reconnect path
// (review finding C1). A self-contained loopback FSocket listener accepts the
// watcher's connection, writes HTTP/200 + one chunked NDJSON frame, then closes
// the socket. The peer-close drives FWatchConnection::HandleClosed -> OnClosed
// -> FGfWatcher::OnClosed, which (pre-fix) freed the connection synchronously
// from inside its own Tick frame. We then keep pumping so the deferred reconnect
// fires. The test completing without a crash IS the assertion. Node-independent.
// ---------------------------------------------------------------------------

namespace
{
    // A tiny non-blocking loopback server. Bind to an ephemeral port, accept one
    // client, send headers + a single chunked NDJSON `result` frame, then close.
    class FLoopbackWatchServer
    {
    public:
        FLoopbackWatchServer()
        {
            SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
            if (!SS) { return; }

            const TSharedRef<FInternetAddr> Addr = SS->CreateInternetAddr();
            bool bValid = false;
            Addr->SetIp(TEXT("127.0.0.1"), bValid);
            Addr->SetPort(0); // ephemeral

            Listener = SS->CreateSocket(NAME_Stream, TEXT("gfwatch-test-listen"), Addr->GetProtocolType());
            if (!Listener) { return; }
            Listener->SetNonBlocking(true);
            Listener->SetReuseAddr(true);
            if (!Listener->Bind(*Addr) || !Listener->Listen(1))
            {
                return;
            }
            Port = Listener->GetPortNo();
        }

        ~FLoopbackWatchServer()
        {
            if (Client && SS) { Client->Close(); SS->DestroySocket(Client); Client = nullptr; }
            if (Listener && SS) { Listener->Close(); SS->DestroySocket(Listener); Listener = nullptr; }
        }

        int32 GetPort() const { return Port; }
        bool IsValid() const { return Listener != nullptr && Port != 0; }

        // Call repeatedly from the pump loop: accept the client, serve once, close.
        void Pump()
        {
            if (!Listener) { return; }

            if (!Client)
            {
                bool bPending = false;
                if (Listener->HasPendingConnection(bPending) && bPending)
                {
                    Client = Listener->Accept(TEXT("gfwatch-test-conn"));
                    if (Client) { Client->SetNonBlocking(true); }
                }
                return;
            }

            if (!bServed)
            {
                // One chunked NDJSON frame: a complete {"result":{...}} line.
                const FString Frame = TEXT("{\"result\":{\"objectMeta\":{\"name\":\"loopback\"},\"status\":{\"state\":\"Ready\",\"address\":\"127.0.0.1\"}}}\n");
                const FTCHARToUTF8 FrameUtf8(*Frame);
                const FString Resp = FString::Printf(
                    TEXT("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n%x\r\n%s\r\n"),
                    FrameUtf8.Length(), *Frame);
                const FTCHARToUTF8 RespUtf8(*Resp);
                int32 Sent = 0;
                Client->Send(reinterpret_cast<const uint8*>(RespUtf8.Get()), RespUtf8.Length(), Sent);
                bServed = true;
                return;
            }

            // Give the watcher several pumps to drain the bytes before we close
            // the socket (a graceful FIN). Closing too early can RST and discard
            // the unread send buffer on some platforms.
            if (++PumpsSinceServe >= 8)
            {
                Client->Close();
                SS->DestroySocket(Client);
                Client = nullptr;
            }
        }

    private:
        ISocketSubsystem* SS = nullptr;
        FSocket* Listener = nullptr;
        FSocket* Client = nullptr;
        int32    Port = 0;
        bool     bServed = false;
        int32    PumpsSinceServe = 0;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfWatcherReconnectTest, "GameFlow.Watch.PeerCloseReconnectNoCrash",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGfWatcherReconnectTest::RunTest(const FString& P)
{
    FNullGameFlowLogger Log;
    FLoopbackWatchServer Server;
    if (!Server.IsValid())
    {
        AddInfo(TEXT("could not open loopback listener; skipping"));
        return true;
    }

    FInlineDispatcher Disp;
    FSidecarTransport T(FString::Printf(TEXT("http://127.0.0.1:%d"), Server.GetPort()), 3000, Log);
    FGfWatcher W(T, Disp, Log);

    int32 Hits = 0;
    FDelegateHandle H = W.Subscribe([&](const FServerInfo&) { Hits++; });

    // Drive both the listener and the engine until the single frame arrives.
    GfPumpUntil(*this, [&] { Server.Pump(); return Hits >= 1; }, 5.0);
    TestTrue(TEXT("got the loopback frame"), Hits >= 1);

    // Keep pumping past the peer-close so HandleClosed -> OnClosed -> the
    // deferred reconnect ticker all fire. Pre-fix this re-entrant teardown was a
    // use-after-free; surviving this loop without a crash is the assertion.
    GfPumpUntil(*this, [&] { Server.Pump(); return false; }, 1.5);

    // Clean teardown (also exercises Unsubscribe -> Close after a reconnect).
    W.Unsubscribe(H);
    TestTrue(TEXT("survived peer-close + reconnect without crashing"), true);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
