#pragma once
#include "CoreMinimal.h"

class IGameFlowLogger;

/**
 * Automatic health heartbeat. Pings at a fixed cadence (IntervalMs, clamped >= 500),
 * scheduling the next ping only after the previous settles. On failure, logs a warning
 * and keeps the normal cadence (no backoff). After 6 consecutive failures, invokes
 * OnDegraded exactly once (and logs an error); recovers silently on the next success.
 *
 * Ping and ScheduleAfter are injected seams so tests can drive timing with a manual
 * clock. In production Task 14 wires ScheduleAfter to FTSTicker.
 *
 * Game-thread only — no threads, no locks.
 */
class FGfHealthLoop
{
public:
    FGfHealthLoop(
        TFunction<void(TFunction<void(bool)>)> InPing,
        TFunction<void(int32, TFunction<void()>)> InScheduleAfter,
        int32 InIntervalMs,
        TFunction<void()> InOnDegraded,
        IGameFlowLogger& InLog);

    /** Sets the alive sentinel false so an in-flight ping completion no-ops. */
    ~FGfHealthLoop();

    /** Begin pinging: fires the first ping immediately. */
    void Start();

    /** Stop the loop; any pending scheduled callback becomes a no-op. */
    void Stop();

private:
    void DoPing();

    TFunction<void(TFunction<void(bool)>)>    Ping;
    TFunction<void(int32, TFunction<void()>)> ScheduleAfter;
    int32                                     IntervalMs;
    TFunction<void()>                         OnDegraded;
    IGameFlowLogger&                          Log;

    int32 ConsecutiveFailures = 0;
    bool  bDegradedReported   = false;
    bool  bStopped            = false;

    static constexpr int32 DegradedThreshold = 6;

    /**
     * Alive sentinel: a heap flag that outlives `this`. The ping completion is
     * AUTONOMOUS — an in-flight Transport->Health(...) request can complete after
     * this loop is destroyed (the owner cannot "pump to drain" an autonomous
     * heartbeat). The completion lambda captures a stack copy of this shared ref
     * and bails (`if (!*Alive) return;`) before touching any member. The
     * destructor sets it false. Mirrors FWatchConnection's Alive pattern.
     */
    TSharedRef<bool> Alive = MakeShared<bool>(true);
};
