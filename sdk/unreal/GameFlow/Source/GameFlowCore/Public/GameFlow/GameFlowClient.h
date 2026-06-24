#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "GameFlow/GameFlowDelegates.h"
#include "GameFlow/GameFlowError.h"
#include "GameFlow/GameFlowOptions.h"
#include "GameFlow/GameFlowEnv.h"
#include "GameFlow/GameFlowLogger.h"
#include "GameFlow/GameFlowTypes.h"

// Private types — forward-declared so the public header stays implementation-free.
class IGfTransport;
class FGameFlowPlayers;
class FGfWatcher;
class FGfHealthLoop;
struct FGfBackoff;

/**
 * FGameFlowClient — the GameFlow game-server SDK entry point.
 *
 * One instance per server process: connect with Start(), mark ready with Ready()
 * (the automatic health heartbeat then runs in sidecar mode), track sessions
 * through Players(), watch server-info / payload changes, and end the match with
 * Shutdown(). Off-platform the SDK resolves to Local mode automatically, so the
 * same build runs everywhere.
 *
 * Game-thread only — every method must be called on the game thread, and every
 * result delegate / watch callback fires on the game thread (callbacks are
 * marshalled through Options.Dispatcher, or inline when none is set).
 *
 * Lifetime contract: the client must outlive any in-flight async operation. The
 * transport, watcher, and health loop capture `this`; the destructor stops the
 * health loop and closes the watcher BEFORE the transport is destroyed, and the
 * members are declared so reverse-order destruction frees them first regardless.
 * Callers must not destroy the client while a Start/Ready/Connect/etc. is still
 * pending (the conformance test pumps each op to completion before teardown).
 */
class GAMEFLOWCORE_API FGameFlowClient
{
public:
    explicit FGameFlowClient(const FGameFlowOptions& Options = FGameFlowOptions());
    ~FGameFlowClient();

    FGameFlowClient(const FGameFlowClient&) = delete;
    FGameFlowClient& operator=(const FGameFlowClient&) = delete;

    /** Connect to the runtime (or local stub). Valid only once, from the New state. */
    void Start(FGfVoidResult OnDone);

    /** Mark the server ready; starts the automatic health heartbeat in sidecar mode. */
    void Ready(FGfVoidResult OnDone);

    /** Fetch the opaque launch payload: (error, payload, present). */
    void GetPayload(FGfPayloadResult OnDone);

    /** Fetch the full server-info snapshot. */
    void GetInfo(FGfInfoResult OnDone);

    /** Subscribe to every server-info frame. Returns a handle for Unsubscribe(). */
    FDelegateHandle Watch(TFunction<void(const FServerInfo&)> OnInfo);

    /**
     * Subscribe to payload changes. OnChange(payload, present) fires only when the
     * payload differs from the last seen value (seeded from the Start() snapshot).
     */
    FDelegateHandle OnPayloadChange(TFunction<void(const FString& /*payload*/, bool /*present*/)> OnChange);

    /** Remove a Watch/OnPayloadChange subscription. */
    void Unsubscribe(FDelegateHandle Handle);

    /** End the match. Idempotent; a failed /shutdown request is logged, not surfaced. */
    void Shutdown(FGfVoidResult OnDone);

    /** Player tracking. Calls before Start() / after Shutdown() report NOT_CONNECTED. */
    FGameFlowPlayers& Players();

    /** The resolved mode after Start(). */
    EGameFlowMode Mode() const { return ResolvedMode; }

private:
    enum class EState { New, Connected, ShutDown };

    /** Returns Ok when state == Connected, else a NOT_CONNECTED error. */
    FGameFlowError EnsureConnected() const;

    /** Run Fn through Options.Dispatcher, or inline when none is set. */
    void Marshal(TFunction<void()> Fn);

    /** Seed Players cache + SeedPayload + create the watcher, then finish Ok. */
    void FinishStart(const FServerInfo& Info, FGfVoidResult OnDone);

    /** Recursive connect-with-retry probe used in sidecar mode. */
    void TryProbe(FGfVoidResult OnDone);

    // --- Members. DECLARATION ORDER MATTERS for destruction (see lifetime note). ---

    FGameFlowOptions             Options;
    TSharedPtr<IGameFlowLogger>  Logger;
    TSharedPtr<IGameFlowDispatcher> Dispatcher; // Options.Dispatcher, or an inline one
    FGameFlowEnvReader           Env;

    // Transport MUST be declared before Watcher/Health: reverse-order destruction
    // then frees Health/Watcher (which capture the transport) before the transport.
    TUniquePtr<IGfTransport>     Transport;
    TUniquePtr<FGameFlowPlayers> PlayersImpl;
    TUniquePtr<FGfWatcher>       Watcher;
    TUniquePtr<FGfHealthLoop>    Health;

    EGameFlowMode  ResolvedMode = EGameFlowMode::Local;
    EState         State        = EState::New;
    FString        SeedPayload;
    bool           bSeedPayloadPresent = false;
    bool           bTrackingWarned     = false;

    // Connect-with-retry state (sidecar mode).
    TUniquePtr<FGfBackoff>     ConnectBackoff;
    double                     ConnectDeadline = 0.0;
    FGameFlowError             LastProbeError;
    FTSTicker::FDelegateHandle ProbeRetryHandle;

    /**
     * Handle for the one-shot core ticker the health loop's ScheduleAfter seam
     * creates (the pending next-ping tick). Retained so Shutdown() and the
     * destructor can RemoveTicker it before the loop is freed — otherwise the
     * pending tick's lambda would deref the destroyed loop. At most one is
     * outstanding (scheduling happens only after the prior ping settles).
     */
    FTSTicker::FDelegateHandle HealthTickHandle;
};
