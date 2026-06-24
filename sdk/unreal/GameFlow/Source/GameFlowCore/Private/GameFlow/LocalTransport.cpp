#include "GameFlow/LocalTransport.h"
#include "GameFlow/GfModel.h"
#include "GameFlow/GameFlowError.h"

// ---------------------------------------------------------------------------
// Private watch-connection — unregisters its handler on Close()
// ---------------------------------------------------------------------------

class FLocalWatchConnection : public IGfWatchConnection
{
public:
    FLocalWatchConnection(TMap<int32, TFunction<void(const FServerInfo&)>>& InHandlers, int32 InId)
        : Handlers(InHandlers)
        , Id(InId)
        , bClosed(false)
    {}

    void Close() override
    {
        if (!bClosed)
        {
            bClosed = true;
            Handlers.Remove(Id);
        }
    }

    ~FLocalWatchConnection() override
    {
        Close();
    }

private:
    TMap<int32, TFunction<void(const FServerInfo&)>>& Handlers;
    int32 Id;
    bool  bClosed;
};

// ---------------------------------------------------------------------------
// FLocalTransport
// ---------------------------------------------------------------------------

FLocalTransport::FLocalTransport(const FGameFlowEnvReader& Env, IGameFlowLogger& InLogger)
    : Logger(InLogger)
{
    FString MaxPlayers;
    if (Env.Get(TEXT("GAMEFLOW_MAX_PLAYERS"), MaxPlayers))
    {
        if (MaxPlayers == TEXT("0"))
        {
            bTrackingEnabled = false;
            Capacity         = 0;
        }
        else
        {
            bTrackingEnabled = true;
            Capacity         = FCString::Atoi64(*MaxPlayers);
        }
    }
    else
    {
        // Absent → unlimited
        bTrackingEnabled = true;
        Capacity         = INT64_MAX;
    }

    Env.Get(TEXT("GAMEFLOW_PAYLOAD"), Payload);
}

FServerInfo FLocalTransport::Snapshot() const
{
    FServerInfo Info;
    Info.Name    = TEXT("local");
    Info.State   = TEXT("Ready");
    Info.Address = TEXT("127.0.0.1");

    if (!Payload.IsEmpty())
    {
        Info.Annotations.Add(FGfModel::PayloadAnnotation, Payload);
    }

    // Mirror the in-memory player state
    Info.Players.bTrackingEnabled = bTrackingEnabled;
    Info.Players.Capacity         = Capacity;
    Info.Players.SessionIds       = Players;

    return Info;
}

void FLocalTransport::RaiseWatch()
{
    const FServerInfo S = Snapshot();
    // Iterate over a copy of the keys so a Close() inside a handler is safe
    TArray<int32> Keys;
    WatchHandlers.GetKeys(Keys);
    for (int32 Key : Keys)
    {
        if (const auto* Handler = WatchHandlers.Find(Key))
        {
            (*Handler)(S);
        }
    }
}

void FLocalTransport::Probe(FGfInfoResult OnDone)
{
    OnDone.ExecuteIfBound(FGameFlowError::Ok(), Snapshot());
}

void FLocalTransport::GetServerInfo(FGfInfoResult OnDone)
{
    OnDone.ExecuteIfBound(FGameFlowError::Ok(), Snapshot());
}

void FLocalTransport::Ready(FGfVoidResult OnDone)
{
    Logger.Verbose(TEXT("[local] Ready()"));
    OnDone.ExecuteIfBound(FGameFlowError::Ok());
}

void FLocalTransport::Health(FGfVoidResult OnDone)
{
    Logger.Verbose(TEXT("[local] Health()"));
    OnDone.ExecuteIfBound(FGameFlowError::Ok());
}

void FLocalTransport::Shutdown(FGfVoidResult OnDone)
{
    Logger.Verbose(TEXT("[local] Shutdown()"));
    OnDone.ExecuteIfBound(FGameFlowError::Ok());
}

void FLocalTransport::AddPlayer(const FString& SessionId, int64 /*CachedCapacity*/, FGfListResult OnDone)
{
    if (!bTrackingEnabled)
    {
        OnDone.ExecuteIfBound(
            FGameFlowError::Make(EGameFlowErrorCode::PlayerTrackingDisabled, TEXT("Player tracking is disabled")),
            FPlayerList{});
        return;
    }

    if (Players.Contains(SessionId))
    {
        OnDone.ExecuteIfBound(
            FGameFlowError::Make(EGameFlowErrorCode::PlayerAlreadyConnected, FString::Printf(TEXT("Player '%s' is already connected"), *SessionId)),
            Snapshot().Players);
        return;
    }

    if (Capacity != INT64_MAX && (int64)Players.Num() >= Capacity)
    {
        OnDone.ExecuteIfBound(
            FGameFlowError::Make(EGameFlowErrorCode::ServerFull, TEXT("Server is full"), Capacity),
            Snapshot().Players);
        return;
    }

    Players.Add(SessionId);
    RaiseWatch();
    OnDone.ExecuteIfBound(FGameFlowError::Ok(), Snapshot().Players);
}

void FLocalTransport::RemovePlayer(const FString& SessionId, FGfRemoveResult OnDone)
{
    if (!bTrackingEnabled)
    {
        OnDone.ExecuteIfBound(
            FGameFlowError::Make(EGameFlowErrorCode::PlayerTrackingDisabled, TEXT("Player tracking is disabled")),
            false,
            FPlayerList{});
        return;
    }

    const int32 RemovedCount = Players.Remove(SessionId);
    const bool bFound = (RemovedCount > 0);

    if (bFound)
    {
        RaiseWatch();
    }

    OnDone.ExecuteIfBound(FGameFlowError::Ok(), bFound, Snapshot().Players);
}

TUniquePtr<IGfWatchConnection> FLocalTransport::OpenWatch(
    TFunction<void(const FServerInfo&)> OnFrame,
    TFunction<void()> /*OnClosed*/) // OnClosed is never invoked: local transport has no server-side close
{
    const int32 Id = NextWatchId++;
    WatchHandlers.Add(Id, MoveTemp(OnFrame));
    return MakeUnique<FLocalWatchConnection>(WatchHandlers, Id);
}
