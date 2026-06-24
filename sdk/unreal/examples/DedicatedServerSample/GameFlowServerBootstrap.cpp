#include "GameFlowServerBootstrap.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

UGameFlowSubsystem* AGameFlowServerBootstrap::GetGameFlow() const
{
    UGameInstance* GI = GetGameInstance();
    if (!GI)
    {
        UE_LOG(LogTemp, Warning, TEXT("[gameflow] GameInstance not available"));
        return nullptr;
    }
    UGameFlowSubsystem* Sub = GI->GetSubsystem<UGameFlowSubsystem>();
    if (!Sub)
    {
        UE_LOG(LogTemp, Warning, TEXT("[gameflow] UGameFlowSubsystem not found — is the GameFlow plugin enabled?"));
    }
    return Sub;
}

// ---------------------------------------------------------------------------
// GameMode overrides
// ---------------------------------------------------------------------------

void AGameFlowServerBootstrap::BeginPlay()
{
    Super::BeginPlay();

    // Skip on clients — this logic only runs on the dedicated server.
    if (!HasAuthority())
    {
        return;
    }

    UGameFlowSubsystem* GF = GetGameFlow();
    if (!GF)
    {
        return;
    }

    // Step 1 — Connect to the GameFlow runtime (or fall back to local mode).
    //   In sidecar mode this probes the runtime with retries and exponential
    //   backoff until ConnectTimeoutMs elapses; in local mode it returns
    //   immediately. The result delegate fires on the game thread.
    GF->Start(FGfVoidResult::CreateWeakLambda(GF, [GF](const FGameFlowError& Error)
    {
        if (!Error.IsOk())
        {
            UE_LOG(LogTemp, Error, TEXT("[gameflow] Start failed: %s"), *Error.Message);
            return;
        }

        UE_LOG(LogTemp, Log, TEXT("[gameflow] Connected (mode=%s)"),
            GF->GetMode() == EGameFlowMode::Sidecar ? TEXT("Sidecar") : TEXT("Local"));

        // Step 2 — Mark the server ready so the platform can route players to it.
        //   In sidecar mode this also starts the automatic health heartbeat.
        GF->Ready(FGfVoidResult::CreateWeakLambda(GF, [](const FGameFlowError& ReadyErr)
        {
            if (!ReadyErr.IsOk())
            {
                UE_LOG(LogTemp, Error, TEXT("[gameflow] Ready failed: %s"), *ReadyErr.Message);
                return;
            }
            UE_LOG(LogTemp, Log, TEXT("[gameflow] Server ready — accepting players"));
        }));
    }));

    // Optional: bind a Blueprint-assignable event for health-degraded alerts.
    GF->OnHealthDegraded.AddDynamic(this, &AGameFlowServerBootstrap::OnHealthDegradedEvent);
}

void AGameFlowServerBootstrap::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    if (!NewPlayer || !NewPlayer->PlayerState)
    {
        return;
    }

    // Stable per-connection tracking key. GetPlayerId() is engine-assigned and
    // unique within the match (no OnlineSubsystem dependency). In production,
    // substitute your own account/session id from the allocation payload.
    const FString SessionId = FString::Printf(TEXT("player-%d"), NewPlayer->PlayerState->GetPlayerId());

    UGameFlowSubsystem* GF = GetGameFlow();
    if (!GF)
    {
        return;
    }

    GF->ConnectPlayer(SessionId, FGfVoidResult::CreateLambda([SessionId](const FGameFlowError& Error)
    {
        if (!Error.IsOk())
        {
            // PLAYER_ALREADY_CONNECTED is benign (reconnect of a live session).
            if (Error.Code != EGameFlowErrorCode::PlayerAlreadyConnected)
            {
                UE_LOG(LogTemp, Warning, TEXT("[gameflow] ConnectPlayer %s failed: %s"),
                    *SessionId, *Error.Message);
            }
        }
    }));
}

void AGameFlowServerBootstrap::Logout(AController* Exiting)
{
    Super::Logout(Exiting);

    APlayerController* PC = Cast<APlayerController>(Exiting);
    if (!PC || !PC->PlayerState)
    {
        return;
    }

    const FString SessionId = FString::Printf(TEXT("player-%d"), PC->PlayerState->GetPlayerId());

    UGameFlowSubsystem* GF = GetGameFlow();
    if (!GF)
    {
        return;
    }

    // DisconnectPlayer is idempotent — safe to call even if ConnectPlayer
    // was never confirmed (e.g. the player dropped before Start() finished).
    GF->DisconnectPlayer(SessionId, FGfRemoveResult::CreateLambda(
        [SessionId](const FGameFlowError& Error, bool /*bFound*/, const FPlayerList& /*List*/)
    {
        if (!Error.IsOk())
        {
            UE_LOG(LogTemp, Warning, TEXT("[gameflow] DisconnectPlayer %s failed: %s"),
                *SessionId, *Error.Message);
        }
    }));
}

// Fired by the OnHealthDegraded event when repeated health pings fail.
// Log or alert here; the SDK keeps the match alive regardless.
void AGameFlowServerBootstrap::OnHealthDegradedEvent()
{
    UE_LOG(LogTemp, Warning, TEXT("[gameflow] Health degraded — check sidecar connectivity"));
}

// ---------------------------------------------------------------------------
// Shutdown
//
// UGameFlowSubsystem::Deinitialize() stops the health heartbeat, closes the
// watch stream, and sends POST /shutdown automatically when the GameInstance
// is torn down (map change or process exit) — no explicit Shutdown() call is
// needed from the GameMode.
// ---------------------------------------------------------------------------
