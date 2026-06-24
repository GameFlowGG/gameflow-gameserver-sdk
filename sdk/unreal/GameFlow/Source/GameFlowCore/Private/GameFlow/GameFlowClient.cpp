#include "GameFlow/GameFlowClient.h"

#include "GameFlow/GfBackoff.h"
#include "GameFlow/GfTransport.h"
#include "GameFlow/GfModeDetection.h"
#include "GameFlow/GfModel.h"
#include "GameFlow/GameFlowPlayers.h"
#include "GameFlow/GfWatcher.h"
#include "GameFlow/GfHealthLoop.h"
#include "GameFlow/LocalTransport.h"
#include "GameFlow/RealTransportFactory.h"

#include "Containers/Ticker.h"

namespace
{
    /** Default sidecar port when neither Options.Port nor AGONES_SDK_HTTP_PORT is set. */
    constexpr int32 DefaultSidecarPort = 9358;

    /** Schedule Fn to run once after Ms milliseconds via the core ticker. */
    FTSTicker::FDelegateHandle ScheduleOnce(int32 Ms, TFunction<void()> Fn)
    {
        return FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateLambda([Fn = MoveTemp(Fn)](float) -> bool
            {
                Fn();
                return false; // one-shot
            }),
            Ms / 1000.0f);
    }
}

FGameFlowClient::FGameFlowClient(const FGameFlowOptions& InOptions)
    : Options(InOptions)
    , Logger(InOptions.Logger ? InOptions.Logger : MakeShared<FGameFlowLog>())
    , Dispatcher(InOptions.Dispatcher ? InOptions.Dispatcher : MakeShared<FInlineDispatcher>())
    , Env(InOptions.EnvProvider)
    , ConnectBackoff(MakeUnique<FGfBackoff>(FRandomStream(0x5D3E7A11))) // fixed seed; jitter is for thundering-herd only
{
}

FGameFlowClient::~FGameFlowClient()
{
    // Stop the health loop and close the watcher BEFORE the transport is destroyed —
    // both capture the transport, so tearing them down first avoids a use-after-free.
    // (Member declaration order already guarantees this on reverse-order destruction;
    // doing it explicitly documents the contract and is robust to reordering.)
    if (Health)
    {
        Health->Stop();
        // Cancel the pending next-ping tick (it captures the loop) BEFORE freeing
        // the loop — Stop() only sets a flag; the ticker still holds a live lambda.
        if (HealthTickHandle.IsValid())
        {
            FTSTicker::GetCoreTicker().RemoveTicker(HealthTickHandle);
            HealthTickHandle.Reset();
        }
        Health.Reset();
    }
    if (Watcher)
    {
        Watcher->Close();
        Watcher.Reset();
    }
    if (ProbeRetryHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(ProbeRetryHandle);
        ProbeRetryHandle.Reset();
    }
    // PlayersImpl and Transport then destruct in reverse declaration order.
}

void FGameFlowClient::Start(FGfVoidResult OnDone)
{
    if (State != EState::New)
    {
        OnDone.ExecuteIfBound(
            FGameFlowError::Make(EGameFlowErrorCode::NotConnected, TEXT("Start() may only be called once")));
        return;
    }

    ResolvedMode = FGfModeDetection::Resolve(Options, Env, *Logger);

    if (ResolvedMode == EGameFlowMode::Local)
    {
        Transport = MakeUnique<FLocalTransport>(Env, *Logger);
        Transport->Probe(FGfInfoResult::CreateLambda(
            [this, OnDone](const FGameFlowError& E, const FServerInfo& Info)
            {
                if (!E.IsOk())
                {
                    OnDone.ExecuteIfBound(E);
                    return;
                }
                FinishStart(Info, OnDone);
            }));
        return;
    }

    // Sidecar mode: resolve the port, build the transport via the (optionally
    // injected) factory, then connect with retry until ConnectTimeoutMs.
    int32 Port = DefaultSidecarPort;
    if (Options.Port.IsSet())
    {
        Port = Options.Port.GetValue();
    }
    else
    {
        int32 EnvPort = 0;
        if (Env.PortVar(TEXT("AGONES_SDK_HTTP_PORT"), EnvPort))
        {
            Port = EnvPort;
        }
    }

    static FRealTransportFactory DefaultFactory;
    IGfTransportFactory& Factory = Options.TransportFactory ? *Options.TransportFactory : DefaultFactory;
    Transport = Factory.Create(
        FString::Printf(TEXT("http://127.0.0.1:%d"), Port), Options, *Logger);

    ConnectBackoff->Reset();
    ConnectDeadline = FPlatformTime::Seconds() + Options.ConnectTimeoutMs / 1000.0;
    LastProbeError = FGameFlowError::Make(
        EGameFlowErrorCode::SidecarUnavailable, TEXT("could not reach the GameFlow runtime"));
    TryProbe(OnDone);
}

void FGameFlowClient::TryProbe(FGfVoidResult OnDone)
{
    Transport->Probe(FGfInfoResult::CreateLambda(
        [this, OnDone](const FGameFlowError& E, const FServerInfo& Info)
        {
            if (E.IsOk())
            {
                FinishStart(Info, OnDone);
                return;
            }

            // Any non-OK probe error is retryable during connect (connection refused
            // -> SidecarUnavailable, HTTP 5xx -> RequestFailed). Mirror the Go/Unity
            // loop: retry until the deadline, then surface SidecarUnavailable.
            LastProbeError = E;
            const int32 DelayMs = ConnectBackoff->NextDelayMs();
            const double Now = FPlatformTime::Seconds();
            if (Now + DelayMs / 1000.0 < ConnectDeadline)
            {
                Logger->Verbose(FString::Printf(
                    TEXT("runtime not reachable yet (%s); retrying in %d ms"), *LastProbeError.Message, DelayMs));
                ProbeRetryHandle = ScheduleOnce(DelayMs, [this, OnDone]()
                {
                    ProbeRetryHandle.Reset();
                    TryProbe(OnDone);
                });
                return;
            }

            OnDone.ExecuteIfBound(FGameFlowError::Make(
                EGameFlowErrorCode::SidecarUnavailable,
                FString::Printf(TEXT("could not reach the GameFlow runtime within the connect timeout (%s)"),
                    *LastProbeError.Message)));
        }));
}

void FGameFlowClient::FinishStart(const FServerInfo& Info, FGfVoidResult OnDone)
{
    PlayersImpl = MakeUnique<FGameFlowPlayers>(*Transport);
    PlayersImpl->SetCache(Info.Players);
    PlayersImpl->SetEnsureConnected([this]() { return EnsureConnected(); });

    bSeedPayloadPresent = FGfModel::PayloadOf(Info, SeedPayload);

    // Warn once when the platform can't see player counts (max players = 0).
    if (ResolvedMode == EGameFlowMode::Sidecar && !Info.Players.bTrackingEnabled && !bTrackingWarned)
    {
        bTrackingWarned = true;
        Logger->Warn(TEXT("player tracking is disabled (game configured with max players = 0); "
            "the platform cannot see player counts and idle servers may be shut down"));
    }

    Watcher = MakeUnique<FGfWatcher>(*Transport, *Dispatcher, *Logger);

    State = EState::Connected;
    Logger->Info(FString::Printf(TEXT("connected (%s mode)"),
        ResolvedMode == EGameFlowMode::Sidecar ? TEXT("sidecar") : TEXT("local")));
    OnDone.ExecuteIfBound(FGameFlowError::Ok());
}

void FGameFlowClient::Ready(FGfVoidResult OnDone)
{
    const FGameFlowError Conn = EnsureConnected();
    if (!Conn.IsOk())
    {
        OnDone.ExecuteIfBound(Conn);
        return;
    }

    Transport->Ready(FGfVoidResult::CreateLambda([this, OnDone](const FGameFlowError& E)
    {
        if (!E.IsOk())
        {
            OnDone.ExecuteIfBound(E);
            return;
        }

        Logger->Info(TEXT("server ready"));

        // Start the automatic health heartbeat (sidecar mode only).
        if (ResolvedMode == EGameFlowMode::Sidecar)
        {
            Health = MakeUnique<FGfHealthLoop>(
                /*Ping=*/ [this](TFunction<void(bool)> PingDone)
                {
                    Transport->Health(FGfVoidResult::CreateLambda(
                        [PingDone](const FGameFlowError& HE) { PingDone(HE.IsOk()); }));
                },
                /*ScheduleAfter=*/ [this](int32 Ms, TFunction<void()> Fn)
                {
                    // Retain the pending next-ping tick so Shutdown()/dtor can cancel
                    // it before the loop is freed. At most one is outstanding; the
                    // prior one has already fired (scheduling follows a settled ping),
                    // but clear any stale handle defensively before storing the new one.
                    if (HealthTickHandle.IsValid())
                    {
                        FTSTicker::GetCoreTicker().RemoveTicker(HealthTickHandle);
                    }
                    HealthTickHandle = ScheduleOnce(Ms, MoveTemp(Fn));
                },
                /*IntervalMs=*/ Options.HealthIntervalMs,
                /*OnDegraded=*/ [this]()
                {
                    Marshal([this]() { if (Options.OnHealthDegraded) Options.OnHealthDegraded(); });
                },
                /*Log=*/ *Logger);
            Health->Start();
        }

        OnDone.ExecuteIfBound(FGameFlowError::Ok());
    }));
}

void FGameFlowClient::GetPayload(FGfPayloadResult OnDone)
{
    const FGameFlowError Conn = EnsureConnected();
    if (!Conn.IsOk())
    {
        OnDone.ExecuteIfBound(Conn, FString(), false);
        return;
    }

    Transport->GetServerInfo(FGfInfoResult::CreateLambda(
        [OnDone](const FGameFlowError& E, const FServerInfo& Info)
        {
            if (!E.IsOk())
            {
                OnDone.ExecuteIfBound(E, FString(), false);
                return;
            }
            FString Payload;
            const bool bPresent = FGfModel::PayloadOf(Info, Payload);
            OnDone.ExecuteIfBound(FGameFlowError::Ok(), Payload, bPresent);
        }));
}

void FGameFlowClient::GetInfo(FGfInfoResult OnDone)
{
    const FGameFlowError Conn = EnsureConnected();
    if (!Conn.IsOk())
    {
        OnDone.ExecuteIfBound(Conn, FServerInfo{});
        return;
    }
    Transport->GetServerInfo(OnDone);
}

FDelegateHandle FGameFlowClient::Watch(TFunction<void(const FServerInfo&)> OnInfo)
{
    if (!EnsureConnected().IsOk())
    {
        return FDelegateHandle();
    }
    return Watcher->Subscribe([this, OnInfo = MoveTemp(OnInfo)](const FServerInfo& I)
    {
        Marshal([OnInfo, I]() { OnInfo(I); });
    });
}

FDelegateHandle FGameFlowClient::OnPayloadChange(TFunction<void(const FString&, bool)> OnChange)
{
    if (!EnsureConnected().IsOk())
    {
        return FDelegateHandle();
    }

    // Per-subscription LastSeen, seeded from the Start() snapshot; the watcher
    // delivers a frame immediately on subscribe, so the seed suppresses a spurious
    // initial fire when the payload hasn't actually changed.
    TSharedPtr<FString> LastSeen = MakeShared<FString>(SeedPayload);
    TSharedPtr<bool> LastPresent = MakeShared<bool>(bSeedPayloadPresent);

    return Watcher->Subscribe([this, OnChange = MoveTemp(OnChange), LastSeen, LastPresent](const FServerInfo& Info)
    {
        FString Payload;
        const bool bPresent = FGfModel::PayloadOf(Info, Payload);
        if (Payload != *LastSeen || bPresent != *LastPresent)
        {
            *LastSeen = Payload;
            *LastPresent = bPresent;
            Marshal([OnChange, Payload, bPresent]() { OnChange(Payload, bPresent); });
        }
    });
}

void FGameFlowClient::Unsubscribe(FDelegateHandle Handle)
{
    if (Watcher)
    {
        Watcher->Unsubscribe(Handle);
    }
}

void FGameFlowClient::Shutdown(FGfVoidResult OnDone)
{
    if (State == EState::ShutDown)
    {
        OnDone.ExecuteIfBound(FGameFlowError::Ok());
        return;
    }
    if (State != EState::Connected)
    {
        OnDone.ExecuteIfBound(
            FGameFlowError::Make(EGameFlowErrorCode::NotConnected, TEXT("not connected")));
        return;
    }

    // Stop the local machinery first, then flip state, then POST /shutdown.
    if (Health)
    {
        Health->Stop();
        // Cancel the pending next-ping tick so it can't fire post-shutdown.
        if (HealthTickHandle.IsValid())
        {
            FTSTicker::GetCoreTicker().RemoveTicker(HealthTickHandle);
            HealthTickHandle.Reset();
        }
    }
    if (Watcher)
    {
        Watcher->Close();
    }
    State = EState::ShutDown;

    Transport->Shutdown(FGfVoidResult::CreateLambda([this, OnDone](const FGameFlowError& E)
    {
        // A failed shutdown request is logged, not surfaced — the server is going
        // away regardless, so the caller always sees Ok.
        if (!E.IsOk())
        {
            Logger->Warn(FString::Printf(TEXT("shutdown request failed: %s"), *E.Message));
        }
        Logger->Info(TEXT("server shut down"));
        OnDone.ExecuteIfBound(FGameFlowError::Ok());
    }));
}

FGameFlowPlayers& FGameFlowClient::Players()
{
    // PlayersImpl exists only after a successful Start(). Before that, hand back a
    // lazily-built placeholder whose EnsureConnected guard reports NOT_CONNECTED on
    // every call — matching the post-Shutdown behavior so the API never throws.
    if (!PlayersImpl)
    {
        if (!Transport)
        {
            // No transport yet (Start not called / failed): a no-op transport so the
            // guard short-circuits before any call reaches it.
            Transport = MakeUnique<FLocalTransport>(Env, *Logger);
        }
        PlayersImpl = MakeUnique<FGameFlowPlayers>(*Transport);
        PlayersImpl->SetEnsureConnected([this]() { return EnsureConnected(); });
    }
    return *PlayersImpl;
}

FGameFlowError FGameFlowClient::EnsureConnected() const
{
    if (State == EState::Connected)
    {
        return FGameFlowError::Ok();
    }
    return FGameFlowError::Make(EGameFlowErrorCode::NotConnected, TEXT("not connected"));
}

void FGameFlowClient::Marshal(TFunction<void()> Fn)
{
    Dispatcher->Post(MoveTemp(Fn));
}
