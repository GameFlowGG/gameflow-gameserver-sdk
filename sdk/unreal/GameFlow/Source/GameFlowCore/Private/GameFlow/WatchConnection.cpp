#include "GameFlow/WatchConnection.h"
#include "GameFlow/GfJson.h"
#include "GameFlow/GfModel.h"
#include "GameFlow/GameFlowLogger.h"

#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

FWatchConnection::FWatchConnection(
    const FString& InHost,
    int32 InPort,
    TFunction<void(const FServerInfo&)> InOnFrame,
    TFunction<void()> InOnClosed,
    IGameFlowLogger& InLogger)
    : Host(InHost)
    , Port(InPort)
    , OnFrame(MoveTemp(InOnFrame))
    , OnClosed(MoveTemp(InOnClosed))
    , Logger(InLogger)
{
    SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem)
    {
        Logger.Error(TEXT("[watch] no socket subsystem"));
        return;
    }

    const TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
    bool bAddrValid = false;
    Addr->SetIp(*Host, bAddrValid);
    Addr->SetPort(Port);
    if (!bAddrValid)
    {
        Logger.Error(FString::Printf(TEXT("[watch] invalid address %s:%d"), *Host, Port));
        return;
    }

    Socket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("gfwatch"), Addr->GetProtocolType());
    if (!Socket)
    {
        Logger.Error(TEXT("[watch] failed to create socket"));
        return;
    }

    Socket->SetNonBlocking(true);
    // Non-blocking connect returns immediately; localhost completes almost at
    // once. We confirm writability/connected state before sending the request.
    Socket->Connect(*Addr);

    TickHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateRaw(this, &FWatchConnection::Tick), 0.0f);
}

FWatchConnection::~FWatchConnection()
{
    // Mark dead so any callback frame still unwinding above us stops touching
    // `this` (the alive-sentinel outlives the object).
    *Alive = false;
    Close();
}

void FWatchConnection::Close()
{
    if (TickHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
        TickHandle.Reset();
    }
    if (Socket && SocketSubsystem)
    {
        Socket->Close();
        SocketSubsystem->DestroySocket(Socket);
        Socket = nullptr;
    }
}

bool FWatchConnection::Tick(float /*DeltaTime*/)
{
    if (!Socket)
    {
        return false; // never opened; stop ticking
    }

    // A callback fired below (HandleClosed/OnClosed, or OnFrame under a
    // non-deferring dispatcher) may synchronously destroy `this`. Hold a stack
    // copy of the alive-sentinel and, after any such call, return a literal
    // false WITHOUT reading any `this` member if we were destroyed.
    const TSharedRef<bool> AliveLocal = Alive;

    // Wait until the non-blocking connect has settled before writing. Treat a
    // hard connect failure as a close.
    const ESocketConnectionState ConnState = Socket->GetConnectionState();
    if (ConnState == SCS_ConnectionError)
    {
        HandleClosed();
        return false; // close path: never reads this->TickHandle
    }

    if (!bRequestSent)
    {
        // Wait for the non-blocking connect to complete. SCS_Connected is the
        // primary signal; on platforms whose state lags, WaitForPendingConnection
        // reporting writable (no pending error) is an equivalent green light.
        bool bConnected = (ConnState == SCS_Connected);
        if (!bConnected)
        {
            bool bHasPendingConnection = false;
            if (Socket->WaitForPendingConnection(bHasPendingConnection, FTimespan::Zero()))
            {
                // Returned true with no pending connection => the connect settled.
                bConnected = !bHasPendingConnection;
            }
        }
        if (!bConnected)
        {
            return true; // still connecting; retry next tick
        }
        SendRequestOnce();
    }

    DrainReadable();

    // DrainReadable may have invoked a callback that destroyed `this`. If so,
    // the sentinel is false and we must not touch any member — just stop.
    if (!*AliveLocal)
    {
        return false;
    }
    return TickHandle.IsValid(); // false once HandleClosed() reset the handle
}

void FWatchConnection::SendRequestOnce()
{
    if (bRequestSent || !Socket)
    {
        return;
    }

    const FString Request =
        TEXT("GET /watch/gameserver HTTP/1.1\r\n")
        TEXT("Host: ") + Host + TEXT("\r\n")
        TEXT("Accept: application/json\r\n")
        TEXT("Connection: keep-alive\r\n")
        TEXT("\r\n");

    const FTCHARToUTF8 Utf8(*Request);
    int32 Sent = 0;
    Socket->Send(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), Sent);
    bRequestSent = true;
    Logger.Verbose(TEXT("[watch] request sent"));
}

void FWatchConnection::DrainReadable()
{
    if (!Socket)
    {
        return;
    }

    // ConsumeBody fires OnFrame, which under a non-deferring dispatcher can
    // synchronously destroy `this`. Hold a stack copy of the alive-sentinel and
    // bail the loop without touching members once we've been destroyed.
    const TSharedRef<bool> AliveLocal = Alive;

    uint8 Buf[4096];
    for (;;)
    {
        int32 Read = 0;
        const bool bOk = Socket->Recv(Buf, sizeof(Buf), Read, ESocketReceiveFlags::None);
        if (bOk && Read > 0)
        {
            int32 BodyStart = 0;

            if (!bHeadersDone)
            {
                // Accumulate until we find the "\r\n\r\n" header terminator.
                HeaderBuf.Append(Buf, Read);
                for (int32 i = 3; i < HeaderBuf.Num(); ++i)
                {
                    if (HeaderBuf[i - 3] == '\r' && HeaderBuf[i - 2] == '\n' &&
                        HeaderBuf[i - 1] == '\r' && HeaderBuf[i] == '\n')
                    {
                        // Body bytes that arrived in THIS recv start after the
                        // terminator; compute the offset within Buf.
                        const int32 BodyInHeaderBuf = i + 1;
                        const int32 BytesBeforeThisRecv = HeaderBuf.Num() - Read;
                        BodyStart = FMath::Max(0, BodyInHeaderBuf - BytesBeforeThisRecv);
                        bHeadersDone = true;
                        break;
                    }
                }
                if (!bHeadersDone)
                {
                    continue; // need more bytes for the headers
                }
            }

            if (Read - BodyStart > 0)
            {
                ConsumeBody(Buf + BodyStart, Read - BodyStart);
                if (!*AliveLocal)
                {
                    return; // a callback destroyed us; touch nothing more
                }
            }
            // A short read means the kernel buffer is drained for now.
            if (Read < (int32)sizeof(Buf))
            {
                return;
            }
            continue;
        }

        if (bOk && Read == 0)
        {
            return; // nothing available right now
        }

        // Recv returned false. On a non-blocking socket EWOULDBLOCK is normal;
        // a real error/closed condition ends the stream.
        const ESocketErrors Err = SocketSubsystem ? SocketSubsystem->GetLastErrorCode() : SE_NO_ERROR;
        if (Err == SE_EWOULDBLOCK || Err == SE_NO_ERROR)
        {
            return; // would block; try again next tick
        }
        HandleClosed();
        return;
    }
}

void FWatchConnection::ConsumeBody(const uint8* Data, int32 Len)
{
    // Lines is a stack-local copy of the framed payloads, so iterating it stays
    // valid even if a callback destroys `this` mid-loop. Hold the alive-sentinel
    // and bail (touching no member) the moment OnFrame frees us.
    const TSharedRef<bool> AliveLocal = Alive;

    TArray<FString> Lines;
    Decoder.Feed(Data, Len, Lines);

    for (const FString& Line : Lines)
    {
        if (Line.IsEmpty())
        {
            continue;
        }
        const TSharedPtr<FJsonObject> Obj = FGfJson::ParseObject(Line);
        if (!Obj.IsValid())
        {
            Logger.Warn(FString::Printf(TEXT("[watch] unparseable line: %s"), *Line));
            continue;
        }

        const TSharedPtr<FJsonObject>* Result = nullptr;
        if (Obj->TryGetObjectField(TEXT("result"), Result) && Result)
        {
            OnFrame(FGfModel::ParseGameServer(*Result));
            if (!*AliveLocal)
            {
                return; // a callback destroyed us during OnFrame; stop now
            }
            continue;
        }

        FString ErrText;
        if (Obj->TryGetStringField(TEXT("error"), ErrText) || Obj->HasField(TEXT("error")))
        {
            Logger.Warn(FString::Printf(TEXT("[watch] error frame: %s"), *Line));
            continue;
        }

        Logger.Warn(FString::Printf(TEXT("[watch] frame without result/error: %s"), *Line));
    }
}

void FWatchConnection::HandleClosed()
{
    if (bClosedFired)
    {
        return;
    }
    bClosedFired = true;

    // Stop ticking before invoking the callback; the callback (the watcher's
    // reconnect path) may Close()/re-open us.
    if (TickHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
        TickHandle.Reset();
    }
    OnClosed();
}
