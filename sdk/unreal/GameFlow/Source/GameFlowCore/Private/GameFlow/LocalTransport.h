#pragma once

#include "CoreMinimal.h"
#include "GameFlow/GfTransport.h"
#include "GameFlow/GameFlowEnv.h"
#include "GameFlow/GameFlowLogger.h"
#include "GameFlow/GameFlowTypes.h"

/**
 * In-process transport used in Local mode (no sidecar).
 *
 * All callbacks are invoked synchronously and inline — this transport runs
 * exclusively on the game thread and requires no locking.
 */
class FLocalTransport : public IGfTransport
{
public:
    FLocalTransport(const FGameFlowEnvReader& Env, IGameFlowLogger& InLogger);

    // IGfTransport
    void Probe(FGfInfoResult OnDone) override;
    void GetServerInfo(FGfInfoResult OnDone) override;
    void Ready(FGfVoidResult OnDone) override;
    void Health(FGfVoidResult OnDone) override;
    void Shutdown(FGfVoidResult OnDone) override;
    void AddPlayer(const FString& SessionId, int64 CachedCapacity, FGfListResult OnDone) override;
    void RemovePlayer(const FString& SessionId, FGfRemoveResult OnDone) override;
    TUniquePtr<IGfWatchConnection> OpenWatch(
        TFunction<void(const FServerInfo&)> OnFrame,
        TFunction<void()> OnClosed) override;

private:
    IGameFlowLogger& Logger;

    bool    bTrackingEnabled = true;
    int64   Capacity         = INT64_MAX;
    FString Payload;               // GAMEFLOW_PAYLOAD value, empty when absent

    TArray<FString> Players;

    // Watch handlers — keyed by incrementing id for O(1) removal
    int32 NextWatchId = 0;
    TMap<int32, TFunction<void(const FServerInfo&)>> WatchHandlers;

    /** Build a synthetic FServerInfo from current in-memory state. */
    FServerInfo Snapshot() const;

    /** Invoke every registered OnFrame handler with the current snapshot. */
    void RaiseWatch();
};
