#include "GameFlow/SidecarTransport.h"
#include "GameFlow/GfJson.h"
#include "GameFlow/GfModel.h"
#include "GameFlow/GameFlowError.h"
#include "GameFlow/WatchConnection.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

// ---------------------------------------------------------------------------
// File-local helpers
// ---------------------------------------------------------------------------

static bool IsSuccess(int32 Code)
{
    return Code >= 200 && Code < 300;
}

// ---------------------------------------------------------------------------
// FSidecarTransport
// ---------------------------------------------------------------------------

FSidecarTransport::FSidecarTransport(const FString& InBaseUrl, int32 InRequestTimeoutMs, IGameFlowLogger& InLogger)
    : BaseUrl(InBaseUrl)
    , RequestTimeoutMs(InRequestTimeoutMs)
    , Logger(InLogger)
{
    // Normalise: drop a single trailing '/' so BaseUrl + Path concatenates cleanly.
    while (BaseUrl.EndsWith(TEXT("/")))
    {
        BaseUrl.LeftChopInline(1);
    }
}

FSidecarTransport::~FSidecarTransport()
{
    // A player-mutation completion may still be in flight; mark dead so its
    // chained-read lambda bails before dereferencing this freed transport.
    *Alive = false;
}

void FSidecarTransport::Send(const FString& Verb, const FString& Path, const FString& Body,
    TFunction<void(bool bOk, int32 Code, const FString& RespBody)> Cb)
{
    auto Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(BaseUrl + Path);
    Req->SetVerb(Verb);
    if (!Body.IsEmpty())
    {
        Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
        Req->SetContentAsString(Body);
    }
    Req->SetTimeout(RequestTimeoutMs / 1000.f);
    Req->OnProcessRequestComplete().BindLambda(
        [Cb = MoveTemp(Cb)](FHttpRequestPtr, FHttpResponsePtr Resp, bool bConnected)
        {
            const bool bOk = bConnected && Resp.IsValid();
            Cb(bOk, bOk ? Resp->GetResponseCode() : 0, bOk ? Resp->GetContentAsString() : FString());
        });
    Req->ProcessRequest();
}

int32 FSidecarTransport::GrpcCode(const FString& Body)
{
    const TSharedPtr<FJsonObject> Obj = FGfJson::ParseObject(Body);
    int32 Code;
    if (Obj.IsValid() && Obj->TryGetNumberField(TEXT("code"), Code))
    {
        return Code;
    }
    return -1;
}

void FSidecarTransport::PostEmpty(const FString& Path, FGfVoidResult OnDone)
{
    Send(TEXT("POST"), Path, FGfJson::ObjEmpty(),
        [OnDone](bool bOk, int32 Code, const FString&)
        {
            if (bOk && IsSuccess(Code))
            {
                OnDone.ExecuteIfBound(FGameFlowError::Ok());
            }
            else
            {
                OnDone.ExecuteIfBound(
                    FGameFlowError::Make(EGameFlowErrorCode::RequestFailed,
                        FString::Printf(TEXT("request failed (HTTP %d)"), Code)));
            }
        });
}

void FSidecarTransport::ReadList(TFunction<void(bool bOk, int32 Code, const FPlayerList&)> Cb)
{
    Send(TEXT("GET"), PlayersPath, FString(),
        [Cb = MoveTemp(Cb)](bool bOk, int32 Code, const FString& RespBody)
        {
            FPlayerList List;
            if (bOk && IsSuccess(Code))
            {
                List = FGfModel::ParseList(FGfJson::ParseObject(RespBody));
            }
            Cb(bOk, Code, List);
        });
}

void FSidecarTransport::Probe(FGfInfoResult OnDone)
{
    Send(TEXT("GET"), TEXT("/gameserver"), FString(),
        [OnDone](bool bOk, int32 Code, const FString& RespBody)
        {
            if (!bOk)
            {
                // Connection failure -> SidecarUnavailable so connect-retry can catch it.
                OnDone.ExecuteIfBound(
                    FGameFlowError::Make(EGameFlowErrorCode::SidecarUnavailable, TEXT("sidecar unavailable")),
                    FServerInfo{});
                return;
            }
            if (IsSuccess(Code))
            {
                OnDone.ExecuteIfBound(FGameFlowError::Ok(),
                    FGfModel::ParseGameServer(FGfJson::ParseObject(RespBody)));
                return;
            }
            OnDone.ExecuteIfBound(
                FGameFlowError::Make(EGameFlowErrorCode::RequestFailed,
                    FString::Printf(TEXT("probe failed (HTTP %d)"), Code)),
                FServerInfo{});
        });
}

void FSidecarTransport::GetServerInfo(FGfInfoResult OnDone)
{
    Send(TEXT("GET"), TEXT("/gameserver"), FString(),
        [OnDone](bool bOk, int32 Code, const FString& RespBody)
        {
            if (bOk && IsSuccess(Code))
            {
                OnDone.ExecuteIfBound(FGameFlowError::Ok(),
                    FGfModel::ParseGameServer(FGfJson::ParseObject(RespBody)));
                return;
            }
            OnDone.ExecuteIfBound(
                FGameFlowError::Make(EGameFlowErrorCode::RequestFailed,
                    FString::Printf(TEXT("get server info failed (HTTP %d)"), Code)),
                FServerInfo{});
        });
}

void FSidecarTransport::Ready(FGfVoidResult OnDone)
{
    Logger.Verbose(TEXT("[sidecar] Ready()"));
    PostEmpty(TEXT("/ready"), OnDone);
}

void FSidecarTransport::Health(FGfVoidResult OnDone)
{
    Logger.Verbose(TEXT("[sidecar] Health()"));
    PostEmpty(TEXT("/health"), OnDone);
}

void FSidecarTransport::Shutdown(FGfVoidResult OnDone)
{
    Logger.Verbose(TEXT("[sidecar] Shutdown()"));
    PostEmpty(TEXT("/shutdown"), OnDone);
}

void FSidecarTransport::AddPlayer(const FString& SessionId, int64 CachedCapacity, FGfListResult OnDone)
{
    const FString Path = FString(PlayersPath) + TEXT(":addValue");
    const TSharedRef<bool> AliveCopy = Alive;
    Send(TEXT("POST"), Path, FGfJson::ObjValue(SessionId),
        [this, AliveCopy, CachedCapacity, OnDone](bool bOk, int32 Code, const FString& RespBody)
        {
            // Bail before touching `this` if the transport was destroyed in flight.
            if (!*AliveCopy) { return; }

            if (bOk && IsSuccess(Code))
            {
                // Don't trust the mutation echo — re-read the authoritative list.
                // The add already happened; if the follow-up read fails, surface
                // RequestFailed (with the list we have) rather than reporting Ok.
                ReadList([AliveCopy, OnDone](bool bReadOk, int32 ReadCode, const FPlayerList& List)
                {
                    if (!*AliveCopy) { return; } // transport gone during the re-read
                    if (bReadOk && IsSuccess(ReadCode))
                    {
                        OnDone.ExecuteIfBound(FGameFlowError::Ok(), List);
                    }
                    else
                    {
                        OnDone.ExecuteIfBound(
                            FGameFlowError::Make(EGameFlowErrorCode::RequestFailed,
                                FString::Printf(TEXT("add player list re-read failed (HTTP %d)"), ReadCode)),
                            List);
                    }
                });
                return;
            }
            if (bOk && Code == 409)
            {
                OnDone.ExecuteIfBound(
                    FGameFlowError::Make(EGameFlowErrorCode::PlayerAlreadyConnected, TEXT("player already connected")),
                    FPlayerList{});
                return;
            }
            if (bOk && Code == 400 && GrpcCode(RespBody) == 11)
            {
                OnDone.ExecuteIfBound(
                    FGameFlowError::Make(EGameFlowErrorCode::ServerFull, TEXT("server is full"), CachedCapacity),
                    FPlayerList{});
                return;
            }
            if (bOk && Code == 404)
            {
                OnDone.ExecuteIfBound(
                    FGameFlowError::Make(EGameFlowErrorCode::PlayerTrackingDisabled, TEXT("player tracking is disabled")),
                    FPlayerList{});
                return;
            }
            OnDone.ExecuteIfBound(
                FGameFlowError::Make(EGameFlowErrorCode::RequestFailed,
                    FString::Printf(TEXT("add player failed (HTTP %d)"), Code)),
                FPlayerList{});
        });
}

void FSidecarTransport::RemovePlayer(const FString& SessionId, FGfRemoveResult OnDone)
{
    const FString Path = FString(PlayersPath) + TEXT(":removeValue");
    const TSharedRef<bool> AliveCopy = Alive;
    Send(TEXT("POST"), Path, FGfJson::ObjValue(SessionId),
        [this, AliveCopy, OnDone](bool bOk, int32 Code, const FString&)
        {
            // Bail before touching `this` if the transport was destroyed in flight.
            if (!*AliveCopy) { return; }

            if (bOk && IsSuccess(Code))
            {
                // The remove already happened; a failed re-read is RequestFailed
                // (still found:true, with the list we have) rather than Ok.
                ReadList([AliveCopy, OnDone](bool bReadOk, int32 ReadCode, const FPlayerList& List)
                {
                    if (!*AliveCopy) { return; } // transport gone during the re-read
                    if (bReadOk && IsSuccess(ReadCode))
                    {
                        OnDone.ExecuteIfBound(FGameFlowError::Ok(), /*bFound*/ true, List);
                    }
                    else
                    {
                        OnDone.ExecuteIfBound(
                            FGameFlowError::Make(EGameFlowErrorCode::RequestFailed,
                                FString::Printf(TEXT("remove player list re-read failed (HTTP %d)"), ReadCode)),
                            /*bFound*/ true, List);
                    }
                });
                return;
            }
            if (bOk && Code == 404)
            {
                // The player wasn't in the list. Re-read to disambiguate a
                // missing value (list exists -> found:false) from a disabled
                // tracker (list itself 404s).
                ReadList([AliveCopy, OnDone](bool bListOk, int32 ListCode, const FPlayerList& List)
                {
                    if (!*AliveCopy) { return; } // transport gone during the re-read
                    if (bListOk && IsSuccess(ListCode))
                    {
                        OnDone.ExecuteIfBound(FGameFlowError::Ok(), /*bFound*/ false, List);
                    }
                    else if (bListOk && ListCode == 404)
                    {
                        OnDone.ExecuteIfBound(
                            FGameFlowError::Make(EGameFlowErrorCode::PlayerTrackingDisabled, TEXT("player tracking is disabled")),
                            false, FPlayerList{});
                    }
                    else
                    {
                        OnDone.ExecuteIfBound(
                            FGameFlowError::Make(EGameFlowErrorCode::RequestFailed,
                                FString::Printf(TEXT("remove player list re-read failed (HTTP %d)"), ListCode)),
                            false, FPlayerList{});
                    }
                });
                return;
            }
            OnDone.ExecuteIfBound(
                FGameFlowError::Make(EGameFlowErrorCode::RequestFailed,
                    FString::Printf(TEXT("remove player failed (HTTP %d)"), Code)),
                false, FPlayerList{});
        });
}

// ---------------------------------------------------------------------------
// OpenWatch — socket-backed NDJSON watch stream (GET /watch/gameserver).
// ---------------------------------------------------------------------------

namespace
{
    /** Pull host + port out of a base URL like "http://127.0.0.1:8080". */
    void ParseHostPort(const FString& BaseUrl, FString& OutHost, int32& OutPort)
    {
        FString Rest = BaseUrl;
        int32 SchemeEnd;
        if (Rest.FindChar(TEXT(':'), SchemeEnd) && Rest.Mid(SchemeEnd).StartsWith(TEXT("://")))
        {
            Rest = Rest.Mid(SchemeEnd + 3); // drop "scheme://"
        }
        // Trim any trailing path.
        int32 Slash;
        if (Rest.FindChar(TEXT('/'), Slash))
        {
            Rest = Rest.Left(Slash);
        }
        int32 Colon;
        if (Rest.FindLastChar(TEXT(':'), Colon))
        {
            OutHost = Rest.Left(Colon);
            OutPort = FCString::Atoi(*Rest.Mid(Colon + 1));
        }
        else
        {
            OutHost = Rest;
            OutPort = 80;
        }
        if (OutHost.IsEmpty())
        {
            OutHost = TEXT("127.0.0.1");
        }
    }
}

TUniquePtr<IGfWatchConnection> FSidecarTransport::OpenWatch(
    TFunction<void(const FServerInfo&)> OnFrame,
    TFunction<void()> OnClosed)
{
    FString Host;
    int32 Port = 0;
    ParseHostPort(BaseUrl, Host, Port);
    Logger.Verbose(FString::Printf(TEXT("[sidecar] OpenWatch %s:%d"), *Host, Port));
    return MakeUnique<FWatchConnection>(Host, Port, MoveTemp(OnFrame), MoveTemp(OnClosed), Logger);
}
