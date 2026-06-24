#include "GameFlowSubsystem.h"
#include "GameThreadDispatcher.h"
#include "GameFlow/GameFlowClient.h"
#include "GameFlow/GameFlowLogger.h"
#include "GameFlow/GameFlowPlayers.h"

// ---------------------------------------------------------------------------
// USubsystem lifecycle
// ---------------------------------------------------------------------------

void UGameFlowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    FGameFlowOptions Options;
    // Reuse the core's UE_LOG-backed logger — no need for a separate Unreal logger.
    Options.Logger = MakeShared<FGameFlowLog>();
    // Marshal watch/payload callbacks onto the game thread via AsyncTask.
    Options.Dispatcher = MakeShared<FGameThreadDispatcher>();
    // Wire the Blueprint-assignable health-degraded event.
    Options.OnHealthDegraded = [this] { OnHealthDegraded.Broadcast(); };

    Client = MakeUnique<FGameFlowClient>(Options);
}

void UGameFlowSubsystem::Deinitialize()
{
    if (Client)
    {
        // Best-effort: fire-and-forget. We don't wait for the round-trip.
        Client->Shutdown(FGfVoidResult());
        Client.Reset();
    }

    Super::Deinitialize();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
    /** NOT_CONNECTED error returned by lifecycle guards when Client is null. */
    FGameFlowError NotConnectedError()
    {
        return FGameFlowError::Make(EGameFlowErrorCode::NotConnected, TEXT("subsystem not initialized"));
    }
}

// ---------------------------------------------------------------------------
// Lifecycle — delegate straight to Client (guard null Client)
// ---------------------------------------------------------------------------

void UGameFlowSubsystem::Start(FGfVoidResult OnDone)
{
    if (!Client)
    {
        OnDone.ExecuteIfBound(NotConnectedError());
        return;
    }

    // Capture a weak reference so the re-broadcast lambdas don't hold a raw
    // `this` that may have been destroyed by the time an AsyncTask fires.
    TWeakObjectPtr<UGameFlowSubsystem> WeakThis(this);

    Client->Start(FGfVoidResult::CreateLambda([WeakThis, OnDone](const FGameFlowError& Err)
    {
        UGameFlowSubsystem* S = WeakThis.Get();
        if (!S)
        {
            return; // subsystem torn down before Start completed
        }

        if (Err.IsOk())
        {
            // Subscribe AFTER Start succeeds: Watch/OnPayloadChange require the
            // Connected state. The Marshal already routes via FGameThreadDispatcher,
            // so Broadcasts land on the game thread.
            S->Client->Watch([WeakThis](const FServerInfo& Info)
            {
                if (UGameFlowSubsystem* Sub = WeakThis.Get())
                {
                    Sub->OnServerInfo.Broadcast(Info);
                }
            });
            S->Client->OnPayloadChange([WeakThis](const FString& Payload, bool bPresent)
            {
                if (UGameFlowSubsystem* Sub = WeakThis.Get())
                {
                    Sub->OnPayloadChanged.Broadcast(Payload, bPresent);
                }
            });
        }
        OnDone.ExecuteIfBound(Err);
    }));
}

void UGameFlowSubsystem::Ready(FGfVoidResult OnDone)
{
    if (!Client)
    {
        OnDone.ExecuteIfBound(NotConnectedError());
        return;
    }
    Client->Ready(OnDone);
}

void UGameFlowSubsystem::Shutdown(FGfVoidResult OnDone)
{
    if (!Client)
    {
        OnDone.ExecuteIfBound(NotConnectedError());
        return;
    }
    Client->Shutdown(OnDone);
}

void UGameFlowSubsystem::GetPayload(FGfPayloadResult OnDone)
{
    if (!Client)
    {
        OnDone.ExecuteIfBound(NotConnectedError(), FString(), false);
        return;
    }
    Client->GetPayload(OnDone);
}

void UGameFlowSubsystem::GetInfo(FGfInfoResult OnDone)
{
    if (!Client)
    {
        OnDone.ExecuteIfBound(NotConnectedError(), FServerInfo{});
        return;
    }
    Client->GetInfo(OnDone);
}

void UGameFlowSubsystem::ConnectPlayer(const FString& SessionId, FGfVoidResult OnDone)
{
    if (!Client)
    {
        OnDone.ExecuteIfBound(NotConnectedError());
        return;
    }
    Client->Players().Connect(SessionId, OnDone);
}

void UGameFlowSubsystem::DisconnectPlayer(const FString& SessionId, FGfRemoveResult OnDone)
{
    if (!Client)
    {
        OnDone.ExecuteIfBound(NotConnectedError(), false, FPlayerList{});
        return;
    }
    Client->Players().Disconnect(SessionId, OnDone);
}

// ---------------------------------------------------------------------------
// Synchronous reads — return cached values; guard against null Client
// ---------------------------------------------------------------------------

int32 UGameFlowSubsystem::PlayerCount() const
{
    return Client ? Client->Players().Count() : 0;
}

TArray<FString> UGameFlowSubsystem::PlayerSessionIds() const
{
    return Client ? Client->Players().List() : TArray<FString>();
}

int64 UGameFlowSubsystem::PlayerCapacity() const
{
    return Client ? Client->Players().Capacity() : 0;
}

bool UGameFlowSubsystem::PlayersTracked() const
{
    return Client ? Client->Players().TrackingEnabled() : false;
}

EGameFlowMode UGameFlowSubsystem::GetMode() const
{
    return Client ? Client->Mode() : EGameFlowMode::Local;
}
