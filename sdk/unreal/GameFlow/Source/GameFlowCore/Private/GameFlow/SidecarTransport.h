#pragma once

#include "CoreMinimal.h"
#include "GameFlow/GfTransport.h"
#include "GameFlow/GameFlowLogger.h"

/**
 * REST transport that talks to the local GameFlow runtime sidecar over HTTP via
 * FHttpModule.
 *
 * Every endpoint forwards its result through the supplied OnDone delegate.
 * FHttpModule completions tick on the game thread, so callbacks fire on the
 * game thread too — no threads, no locks.
 *
 * Lifetime: the player-mutation completions capture `this` to chain a list
 * re-read (`this->ReadList()`), so a completion can fire after the transport is
 * destroyed (FHttpManager holds the pending request — e.g. the subsystem's
 * fire-and-forget `Client.Reset()` on Deinitialize). The Alive sentinel (below)
 * is the hard safety net: those lambdas bail before touching any member when the
 * transport is gone. Other completions capture only OnDone/values and are safe.
 *
 * This class implements the request/response endpoints only. The server-info
 * watch stream (OpenWatch) is a placeholder here; the real socket-backed stream
 * is implemented in Task 11.
 */
class FSidecarTransport : public IGfTransport
{
public:
    FSidecarTransport(const FString& InBaseUrl, int32 InRequestTimeoutMs, IGameFlowLogger& InLogger);
    ~FSidecarTransport();

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
    FString          BaseUrl;          // e.g. http://127.0.0.1:8080 (no trailing '/')
    int32            RequestTimeoutMs;
    IGameFlowLogger& Logger;

    /** Players list path; the ':addValue'/':removeValue' suffixes are literal. */
    static constexpr const TCHAR* PlayersPath = TEXT("/v1beta1/lists/players");

    /**
     * Issue an HTTP request and forward (bConnectedSuccessfully, status, body)
     * to Cb on the game thread. Body empty means no request body.
     */
    void Send(const FString& Verb, const FString& Path, const FString& Body,
        TFunction<void(bool bOk, int32 Code, const FString& RespBody)> Cb);

    /** Parse the gRPC error code out of a response body; -1 when absent. */
    static int32 GrpcCode(const FString& Body);

    /** Helper for Ready/Health/Shutdown: POST {} then map 2xx -> Ok. */
    void PostEmpty(const FString& Path, FGfVoidResult OnDone);

    /** Fetch the players list (GET) and forward the parsed FPlayerList. */
    void ReadList(TFunction<void(bool bOk, int32 Code, const FPlayerList&)> Cb);

    /**
     * Alive sentinel: a heap flag that outlives `this`. The AddPlayer/RemovePlayer
     * mutation completions capture `this` to chain a `ReadList()` re-read, so a
     * completion can fire after the transport is destroyed (FHttpManager holds the
     * pending request). Each such lambda captures a copy of this sentinel and bails
     * (`if (!*Alive) return;`) before touching any member. The destructor sets it
     * false. Mirrors FGfHealthLoop's / FWatchConnection's Alive pattern.
     */
    TSharedRef<bool> Alive = MakeShared<bool>(true);
};
