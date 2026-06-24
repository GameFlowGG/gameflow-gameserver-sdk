#pragma once

#include "CoreMinimal.h"
#include "GameFlow/GameFlowDelegates.h"

// Forward declarations
struct FGameFlowOptions;
class IGameFlowLogger;

// ---------------------------------------------------------------------------
// IGfWatchConnection — returned by OpenWatch; Close() unregisters the handler
// ---------------------------------------------------------------------------

class IGfWatchConnection
{
public:
    virtual ~IGfWatchConnection() = default;

    /** Unregister the associated OnFrame handler. Safe to call multiple times. */
    virtual void Close() = 0;
};

// ---------------------------------------------------------------------------
// IGfTransport — pure-virtual transport abstraction
// ---------------------------------------------------------------------------

class IGfTransport
{
public:
    virtual ~IGfTransport() = default;

    /** Quick reachability check — returns a synthetic or cached FServerInfo. */
    virtual void Probe(FGfInfoResult OnDone) = 0;

    /** Full server-info fetch. */
    virtual void GetServerInfo(FGfInfoResult OnDone) = 0;

    /** Mark the server as Ready. */
    virtual void Ready(FGfVoidResult OnDone) = 0;

    /** Send a health ping. */
    virtual void Health(FGfVoidResult OnDone) = 0;

    /** Initiate server shutdown. */
    virtual void Shutdown(FGfVoidResult OnDone) = 0;

    /**
     * Add a player session.
     * @param SessionId      Unique session identifier for the player.
     * @param CachedCapacity Caller-side capacity hint (used by local transport).
     * @param OnDone         Receives the updated player list on success.
     */
    virtual void AddPlayer(const FString& SessionId, int64 CachedCapacity, FGfListResult OnDone) = 0;

    /**
     * Remove a player session.
     * @param SessionId Unique session identifier to remove.
     * @param OnDone    Receives (error, bFound, updatedList).
     */
    virtual void RemovePlayer(const FString& SessionId, FGfRemoveResult OnDone) = 0;

    /**
     * Register a server-info watch.
     * @param OnFrame  Called with a fresh snapshot on every mutation.
     * @param OnClosed Called when the watch is closed from the transport side.
     * @return Connection handle; call Close() to unregister.
     */
    virtual TUniquePtr<IGfWatchConnection> OpenWatch(
        TFunction<void(const FServerInfo&)> OnFrame,
        TFunction<void()> OnClosed) = 0;
};

// ---------------------------------------------------------------------------
// IGfTransportFactory — creates the appropriate IGfTransport
// ---------------------------------------------------------------------------

class IGfTransportFactory
{
public:
    virtual ~IGfTransportFactory() = default;

    virtual TUniquePtr<IGfTransport> Create(
        const FString& BaseUrl,
        const FGameFlowOptions& Options,
        IGameFlowLogger& Logger) = 0;
};
