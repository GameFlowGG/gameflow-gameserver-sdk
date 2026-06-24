#include "GameFlow/GameFlowPlayers.h"
#include "GameFlow/GfTransport.h"

FGameFlowPlayers::FGameFlowPlayers(IGfTransport& InTransport)
    : Transport(InTransport)
{
}

void FGameFlowPlayers::Connect(const FString& SessionId, FGfVoidResult OnDone)
{
    if (EnsureConnectedFn)
    {
        const FGameFlowError Guard = EnsureConnectedFn();
        if (!Guard.IsOk())
        {
            OnDone.ExecuteIfBound(Guard);
            return;
        }
    }

    Transport.AddPlayer(SessionId, Cache.Capacity,
        FGfListResult::CreateLambda([this, OnDone](const FGameFlowError& Err, const FPlayerList& List)
        {
            if (Err.IsOk())
            {
                Cache = List;
            }
            OnDone.ExecuteIfBound(Err);
        }));
}

void FGameFlowPlayers::Disconnect(const FString& SessionId, FGfRemoveResult OnDone)
{
    if (EnsureConnectedFn)
    {
        const FGameFlowError Guard = EnsureConnectedFn();
        if (!Guard.IsOk())
        {
            OnDone.ExecuteIfBound(Guard, false, Cache);
            return;
        }
    }

    Transport.RemovePlayer(SessionId,
        FGfRemoveResult::CreateLambda([this, OnDone](const FGameFlowError& Err, bool bFound, const FPlayerList& List)
        {
            if (Err.IsOk())
            {
                Cache = List;
            }
            OnDone.ExecuteIfBound(Err, bFound, List);
        }));
}

int32 FGameFlowPlayers::Count() const
{
    return Cache.SessionIds.Num();
}

const TArray<FString>& FGameFlowPlayers::List() const
{
    // Return a reference that stays valid even when untracked/empty.
    return Cache.SessionIds;
}

int64 FGameFlowPlayers::Capacity() const
{
    return Cache.Capacity;
}

bool FGameFlowPlayers::TrackingEnabled() const
{
    return Cache.bTrackingEnabled;
}

void FGameFlowPlayers::SetCache(const FPlayerList& InCache)
{
    Cache = InCache;
}

void FGameFlowPlayers::SetEnsureConnected(TFunction<FGameFlowError()> InFn)
{
    EnsureConnectedFn = MoveTemp(InFn);
}
