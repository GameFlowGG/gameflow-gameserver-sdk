#pragma once

#include "CoreMinimal.h"
#include "Async/Async.h"
#include "GameFlow/GameFlowOptions.h" // IGameFlowDispatcher

/**
 * Game-thread dispatcher — marshals callbacks onto the Unreal game thread via AsyncTask.
 *
 * Used by UGameFlowSubsystem so that Watch/OnPayloadChange callbacks from the
 * watcher socket arrive on the game thread before Broadcast() is called.
 */
class FGameThreadDispatcher : public IGameFlowDispatcher
{
public:
    virtual void Post(TFunction<void()> Fn) override
    {
        AsyncTask(ENamedThreads::GameThread, [Fn = MoveTemp(Fn)]() mutable { Fn(); });
    }
};
