#include "GameFlow/GfWatcher.h"
#include "GameFlow/GameFlowLogger.h"
#include "GameFlow/GameFlowOptions.h" // IGameFlowDispatcher

FGfWatcher::FGfWatcher(IGfTransport& InTransport, IGameFlowDispatcher& InDispatcher, IGameFlowLogger& InLogger)
    : Transport(InTransport)
    , Dispatcher(InDispatcher)
    , Logger(InLogger)
    , Backoff(FRandomStream(0x9F2B7C1D)) // fixed seed; jitter is for thundering-herd, determinism is fine here
{
}

FGfWatcher::~FGfWatcher()
{
    Close();
}

FDelegateHandle FGfWatcher::Subscribe(TFunction<void(const FServerInfo&)> OnInfo)
{
    FSubscriber Sub;
    Sub.Handle = FDelegateHandle(FDelegateHandle::GenerateNewHandle);
    Sub.Fn = MoveTemp(OnInfo);
    const FDelegateHandle Handle = Sub.Handle;
    Subscribers.Add(MoveTemp(Sub));

    // Open the stream lazily on the first subscriber.
    if (Subscribers.Num() == 1 && !Connection)
    {
        OpenConnection();
    }
    return Handle;
}

void FGfWatcher::Unsubscribe(FDelegateHandle Handle)
{
    Subscribers.RemoveAll([Handle](const FSubscriber& S) { return S.Handle == Handle; });
    if (Subscribers.Num() == 0)
    {
        Close();
    }
}

void FGfWatcher::Close()
{
    bClosing = true;
    CancelReconnect();
    TeardownConnection();
    bClosing = false;
}

void FGfWatcher::CancelTeardown()
{
    if (TeardownHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TeardownHandle);
        TeardownHandle.Reset();
    }
}

void FGfWatcher::TeardownConnection()
{
    if (!Connection)
    {
        CancelTeardown();
        return;
    }

    // Stop the connection's ticker now (Close() is idempotent and only removes
    // the ticker + closes the FD — it does NOT free `this`), but defer the
    // actual destruction (Connection.Reset()) when we're inside a connection
    // callback: freeing the connection whose Tick frame is still on the stack
    // would be a use-after-free.
    Connection->Close();

    if (CallbackDepth > 0)
    {
        if (!TeardownHandle.IsValid())
        {
            TeardownHandle = FTSTicker::GetCoreTicker().AddTicker(
                FTickerDelegate::CreateLambda([this](float) -> bool
                {
                    TeardownHandle.Reset();
                    Connection.Reset(); // safe: fresh stack, no callback frame above us
                    return false;       // one-shot
                }),
                0.0f);
        }
        return;
    }

    // Not inside a callback — safe to destroy right away.
    CancelTeardown();
    Connection.Reset();
}

void FGfWatcher::OpenConnection()
{
    Connection = Transport.OpenWatch(
        [this](const FServerInfo& Info) { OnFrame(Info); },
        [this]() { OnClosed(); });
}

void FGfWatcher::OnFrame(const FServerInfo& Info)
{
    // A connection callback is on the stack: forbid inline Connection.Reset().
    ++CallbackDepth;

    // Any frame means the stream is healthy again — reset the reconnect backoff.
    Backoff.Reset();

    // Deliver to a snapshot through the dispatcher so a subscriber that
    // unsubscribes mid-fan-out can't invalidate the iteration. With an inline
    // dispatcher a subscriber may call Unsubscribe -> Close here; the
    // CallbackDepth guard defers the resulting teardown to a one-shot ticker.
    TArray<TFunction<void(const FServerInfo&)>> Snapshot;
    Snapshot.Reserve(Subscribers.Num());
    for (const FSubscriber& S : Subscribers)
    {
        Snapshot.Add(S.Fn);
    }
    for (TFunction<void(const FServerInfo&)>& Fn : Snapshot)
    {
        Dispatcher.Post([Fn, Info]() { Fn(Info); });
    }

    --CallbackDepth;
}

void FGfWatcher::OnClosed()
{
    if (bClosing)
    {
        return; // we initiated the close
    }

    // A connection callback (the connection's Tick frame) is on the stack, so we
    // must NOT destroy the connection here. Schedule a one-shot reconnect ticker;
    // the deferred lambda (fresh stack) drops + recreates the connection. The
    // connection already removed its own ticker in HandleClosed(), so it stays
    // inert until DoReconnect() frees it.
    ++CallbackDepth;

    if (Subscribers.Num() > 0)
    {
        const int32 DelayMs = Backoff.NextDelayMs();
        Logger.Verbose(FString::Printf(TEXT("[watch] stream closed; reconnecting in %d ms"), DelayMs));

        CancelReconnect();
        ReconnectHandle = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateLambda([this](float) -> bool
            {
                ReconnectHandle.Reset();
                DoReconnect();
                return false; // one-shot
            }),
            DelayMs / 1000.0f);
    }
    else
    {
        // No subscribers left to reconnect for — defer the teardown so the
        // connection's Tick frame finishes unwinding before we free it.
        TeardownConnection();
    }

    --CallbackDepth;
}

void FGfWatcher::DoReconnect()
{
    // Runs from a deferred ticker (fresh stack) — safe to destroy + recreate.
    Connection.Reset();
    if (!bClosing && Subscribers.Num() > 0)
    {
        OpenConnection();
    }
}

void FGfWatcher::CancelReconnect()
{
    if (ReconnectHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(ReconnectHandle);
        ReconnectHandle.Reset();
    }
}
