#pragma once

#include "CoreMinimal.h"
#include "GameFlow/GfTransport.h"
#include "GameFlow/GfChunkedNdjson.h"
#include "Containers/Ticker.h"

class IGameFlowLogger;
class FSocket;
class ISocketSubsystem;

/**
 * A single GET /watch/gameserver stream over a non-blocking FSocket, polled on
 * the game thread by FTSTicker.
 *
 * The sidecar serves the watch as HTTP/1.1 chunked transfer-encoding carrying
 * NDJSON. This connection opens the socket, sends the raw request once, and on
 * each tick drains readable bytes: it strips the response headers (up to the
 * first "\r\n\r\n") once, then feeds the body through FGfChunkedNdjson. Each
 * decoded JSON line with a "result" object is mapped to FServerInfo and handed
 * to OnFrame; "error" lines are logged and skipped. When the peer closes or the
 * socket errors, OnClosed fires once and the ticker stops.
 *
 * Game-thread only — FTSTicker fires on the game thread; no worker threads, no
 * locks. Close() is idempotent and called by the destructor.
 */
class FWatchConnection : public IGfWatchConnection
{
public:
    FWatchConnection(
        const FString& InHost,
        int32 InPort,
        TFunction<void(const FServerInfo&)> InOnFrame,
        TFunction<void()> InOnClosed,
        IGameFlowLogger& InLogger);

    virtual ~FWatchConnection() override;

    // IGfWatchConnection
    virtual void Close() override;

private:
    /** FTSTicker callback; returns true to keep ticking until the stream ends. */
    bool Tick(float DeltaTime);

    /** Send the raw HTTP request bytes once the socket is connected. */
    void SendRequestOnce();

    /** Drain all currently-readable bytes into the header strip / decoder. */
    void DrainReadable();

    /**
     * Feed post-header bytes to the decoder and dispatch any framed lines.
     * Honours the alive-sentinel: if an OnFrame callback destroys this
     * connection mid-loop, the loop bails without touching any `this` member.
     */
    void ConsumeBody(const uint8* Data, int32 Len);

    /** Fire OnClosed once and stop the ticker. */
    void HandleClosed();

    FString          Host;
    int32            Port = 0;
    TFunction<void(const FServerInfo&)> OnFrame;
    TFunction<void()> OnClosed;
    IGameFlowLogger& Logger;

    ISocketSubsystem* SocketSubsystem = nullptr;
    FSocket*          Socket = nullptr;
    FTSTicker::FDelegateHandle TickHandle;

    bool bRequestSent = false;
    bool bHeadersDone = false;
    bool bClosedFired = false;

    /** Accumulates response bytes until the "\r\n\r\n" header terminator. */
    TArray<uint8>     HeaderBuf;

    FGfChunkedNdjson  Decoder;

    /**
     * Alive sentinel: a heap flag that outlives `this`. Re-entrant callbacks
     * (OnFrame/OnClosed) can synchronously destroy this connection under a
     * non-deferring dispatcher; methods take a stack copy of this shared ref
     * before invoking a callback and, after it returns, check `*Alive` before
     * touching any member. The destructor sets it false. Defense-in-depth — the
     * watcher's deferred teardown already avoids the inline free in practice.
     */
    TSharedRef<bool>  Alive = MakeShared<bool>(true);
};
