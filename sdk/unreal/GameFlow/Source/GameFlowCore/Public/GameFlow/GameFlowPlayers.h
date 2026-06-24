#pragma once

#include "CoreMinimal.h"
#include "GameFlow/GameFlowDelegates.h"
#include "GameFlow/GameFlowTypes.h"
#include "GameFlow/GameFlowError.h"

// IGfTransport is private; forward-declare so the public header stays clean.
class IGfTransport;

/**
 * FGameFlowPlayers — public player-tracking API.
 *
 * Wraps an IGfTransport reference and maintains a cached FPlayerList so all
 * read operations (Count, List, Capacity, TrackingEnabled) are synchronous
 * and game-thread safe with no locking required.
 *
 * Lifetime contract: Connect/Disconnect completion lambdas capture `this`.
 * The owner (FGameFlowClient, Task 14) must outlive any in-flight calls —
 * the same contract as FGfWatcher and FGfHealthLoop.
 */
class GAMEFLOWCORE_API FGameFlowPlayers
{
public:
    explicit FGameFlowPlayers(IGfTransport& InTransport);

    // -----------------------------------------------------------------------
    // Mutating operations — results arrive via delegate on the game thread
    // -----------------------------------------------------------------------

    /** Add a player session. Updates the cache on success. */
    void Connect(const FString& SessionId, FGfVoidResult OnDone);

    /** Remove a player session. Updates the cache on success. */
    void Disconnect(const FString& SessionId, FGfRemoveResult OnDone);

    // -----------------------------------------------------------------------
    // Synchronous reads — always return cached values, no lock needed
    // -----------------------------------------------------------------------

    /** Current number of connected players. */
    int32 Count() const;

    /** Session IDs of connected players. Returns an empty array when untracked. */
    const TArray<FString>& List() const;

    /** Max player capacity as reported by the runtime. */
    int64 Capacity() const;

    /** Whether player tracking is enabled for this server. */
    bool TrackingEnabled() const;

    // -----------------------------------------------------------------------
    // Internal seams — called by FGameFlowClient (Task 14)
    // -----------------------------------------------------------------------

    /** Seed the cache from the initial Probe result. */
    void SetCache(const FPlayerList& InCache);

    /**
     * Install a guard called before every transport call.
     * When the function returns a non-OK error the result delegate is invoked
     * with that error and the transport is NOT called (reports NOT_CONNECTED
     * before Start / after Shutdown).
     */
    void SetEnsureConnected(TFunction<FGameFlowError()> InFn);

private:
    IGfTransport& Transport;
    FPlayerList   Cache;
    TFunction<FGameFlowError()> EnsureConnectedFn;
};
