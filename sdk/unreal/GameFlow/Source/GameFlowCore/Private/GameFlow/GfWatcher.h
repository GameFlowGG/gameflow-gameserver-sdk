#pragma once

#include "CoreMinimal.h"
#include "GameFlow/GfTransport.h"
#include "GameFlow/GfBackoff.h"
#include "Containers/Ticker.h"
#include "Delegates/IDelegateInstance.h"

class IGameFlowLogger;
class IGameFlowDispatcher;

/**
 * Shared server-info watch with fan-out to many subscribers and automatic
 * reconnect with exponential backoff. Mirrors the Unity SDK's Watcher.cs.
 *
 * One underlying watch connection is opened lazily on the first subscriber and
 * closed on the last unsubscribe. Every frame is delivered to a snapshot of the
 * current subscribers through the injected dispatcher (so callbacks land on the
 * game thread), and the backoff is reset after any received frame. When the
 * connection closes while subscribers remain, a reconnect is scheduled after
 * Backoff.NextDelayMs() via FTSTicker.
 *
 * Game-thread only — no worker threads, no locks.
 */
class FGfWatcher
{
public:
    FGfWatcher(IGfTransport& InTransport, IGameFlowDispatcher& InDispatcher, IGameFlowLogger& InLogger);
    ~FGfWatcher();

    /** Add a subscriber; opens the stream on the first one. */
    FDelegateHandle Subscribe(TFunction<void(const FServerInfo&)> OnInfo);

    /** Remove a subscriber; closes the stream on the last one. */
    void Unsubscribe(FDelegateHandle Handle);

    /** Close the stream and cancel any pending reconnect. */
    void Close();

private:
    struct FSubscriber
    {
        FDelegateHandle Handle;
        TFunction<void(const FServerInfo&)> Fn;
    };

    void OpenConnection();
    void OnFrame(const FServerInfo& Info);
    void OnClosed();
    void CancelReconnect();

    /**
     * Tear down the live connection. MUST NOT run synchronously from inside a
     * connection callback (OnFrame/OnClosed) — destroying the connection whose
     * Tick frame is still on the stack is a use-after-free. When called during a
     * callback it defers the actual Connection.Reset() to a one-shot ticker.
     */
    void TeardownConnection();

    /** Drop + recreate the connection. Only ever invoked from a deferred ticker. */
    void DoReconnect();

    /** Cancel any pending deferred-teardown ticker. */
    void CancelTeardown();

    IGfTransport&        Transport;
    IGameFlowDispatcher& Dispatcher;
    IGameFlowLogger&     Logger;

    TArray<FSubscriber>  Subscribers;
    TUniquePtr<IGfWatchConnection> Connection;

    FGfBackoff           Backoff;
    FTSTicker::FDelegateHandle ReconnectHandle;

    /** Deferred-teardown ticker handle (used when Close() lands inside a callback). */
    FTSTicker::FDelegateHandle TeardownHandle;

    /** True while we initiated a close, so the connection's OnClosed is a no-op. */
    bool                 bClosing = false;

    /**
     * Depth counter: >0 while a connection callback (OnFrame/OnClosed) is on the
     * stack. While non-zero, Connection.Reset() must be deferred, never inline.
     */
    int32                CallbackDepth = 0;
};
