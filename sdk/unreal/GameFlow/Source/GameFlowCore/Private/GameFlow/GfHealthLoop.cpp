#include "GameFlow/GfHealthLoop.h"
#include "GameFlow/GameFlowLogger.h"

FGfHealthLoop::FGfHealthLoop(
    TFunction<void(TFunction<void(bool)>)> InPing,
    TFunction<void(int32, TFunction<void()>)> InScheduleAfter,
    int32 InIntervalMs,
    TFunction<void()> InOnDegraded,
    IGameFlowLogger& InLog)
    : Ping(MoveTemp(InPing))
    , ScheduleAfter(MoveTemp(InScheduleAfter))
    , IntervalMs(FMath::Max(InIntervalMs, 500)) // clamp to minimum of 500 ms
    , OnDegraded(MoveTemp(InOnDegraded))
    , Log(InLog)
{
}

FGfHealthLoop::~FGfHealthLoop()
{
    // An autonomous ping may still be in flight; mark dead so its completion bails.
    *Alive = false;
}

void FGfHealthLoop::Start()
{
    bStopped = false;
    DoPing();
}

void FGfHealthLoop::Stop()
{
    bStopped = true;
}

void FGfHealthLoop::DoPing()
{
    // Captures 'this' plus a copy of the alive sentinel. The ping is autonomous:
    // its completion can fire after this loop is destroyed, so the lambda checks
    // *Alive before touching ANY member. The owner should also Stop() (same owner
    // contract as FGfWatcher), but the sentinel is the hard safety net.
    TSharedRef<bool> AliveCopy = Alive;
    Ping([this, AliveCopy](bool bOk)
    {
        if (!*AliveCopy)
        {
            return; // loop destroyed while this ping was in flight — do not touch `this`
        }

        if (bOk)
        {
            ConsecutiveFailures = 0;
            bDegradedReported   = false;
        }
        else
        {
            ++ConsecutiveFailures;
            Log.Warn(FString::Printf(TEXT("[gameflow] health ping failed (consecutive: %d)"), ConsecutiveFailures));

            if (ConsecutiveFailures >= DegradedThreshold && !bDegradedReported)
            {
                bDegradedReported = true;
                Log.Error(TEXT("[gameflow] health degraded — 6 consecutive ping failures"));
                OnDegraded();
            }
        }

        // Schedule the next ping only if not stopped, and only after this one settled.
        if (!bStopped)
        {
            ScheduleAfter(IntervalMs, [this]()
            {
                if (!bStopped)
                {
                    DoPing();
                }
            });
        }
    });
}
