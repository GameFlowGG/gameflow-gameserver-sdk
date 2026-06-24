#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "GameFlow/GfHealthLoop.h"
#include "GameFlow/GameFlowLogger.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfHealthTest, "GameFlow.Health.DegradesOnceAfterSixFailures",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfHealthTest::RunTest(const FString& P) {
    FNullGameFlowLogger Log;
    int32 Pings = 0, Degraded = 0;
    TArray<TFunction<void()>> Scheduled; // manual clock: we fire these ourselves
    auto Ping = [&](TFunction<void(bool)> Done){ Pings++; Done(Pings > 7); }; // first 7 fail, then succeed
    auto Schedule = [&](int32 Ms, TFunction<void()> Fn){ Scheduled.Add(MoveTemp(Fn)); };
    FGfHealthLoop Loop(Ping, Schedule, 500, [&]{ Degraded++; }, Log);
    Loop.Start();                                  // fires ping #1
    for (int i = 0; i < 8 && Scheduled.Num(); ++i) // advance the clock 8 ticks
        { TFunction<void()> Fn = Scheduled.Pop(); Fn(); }
    TestTrue(TEXT(">=7 pings"), Pings >= 7);
    TestEqual(TEXT("degraded once"), Degraded, 1);
    Loop.Stop();
    return true;
}

// ---------------------------------------------------------------------------
// Clamp: an IntervalMs below 500 must be clamped to exactly 500 in the
// constructor; the injected ScheduleAfter receives the clamped value.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfHealthClampTest, "GameFlow.Health.ClampsIntervalToMinimum",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfHealthClampTest::RunTest(const FString& P) {
    FNullGameFlowLogger Log;
    int32 Pings = 0, CapturedMs = -1;
    TArray<TFunction<void()>> Scheduled;
    auto Ping     = [&](TFunction<void(bool)> Done){ Pings++; Done(true); };
    auto Schedule = [&](int32 Ms, TFunction<void()> Fn){ CapturedMs = Ms; Scheduled.Add(MoveTemp(Fn)); };
    FGfHealthLoop Loop(Ping, Schedule, /*IntervalMs=*/100, [](){}, Log); // 100 < 500 — must clamp
    Loop.Start();                        // ping #1 fires synchronously, completes, schedules next
    TestTrue(TEXT("one ping fired"), Pings == 1);
    TestEqual(TEXT("interval clamped to 500"), CapturedMs, 500);
    Loop.Stop();
    return true;
}

// ---------------------------------------------------------------------------
// Stop(): a callback already queued in ScheduleAfter must become a no-op after
// Stop() is called — no further pings should be dispatched.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfHealthStopTest, "GameFlow.Health.StopHaltsPings",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfHealthStopTest::RunTest(const FString& P) {
    FNullGameFlowLogger Log;
    int32 Pings = 0;
    TArray<TFunction<void()>> Scheduled;
    auto Ping     = [&](TFunction<void(bool)> Done){ Pings++; Done(true); };
    auto Schedule = [&](int32 Ms, TFunction<void()> Fn){ Scheduled.Add(MoveTemp(Fn)); };
    FGfHealthLoop Loop(Ping, Schedule, 500, [](){}, Log);
    Loop.Start();                        // ping #1, queues one scheduled fn
    const int32 PingsAfterStart = Pings;
    TestTrue(TEXT("one ping before stop"), PingsAfterStart == 1);
    TestTrue(TEXT("one fn queued"), Scheduled.Num() == 1);
    Loop.Stop();                         // stop before the scheduled fn fires
    TFunction<void()> Fn = Scheduled.Pop();
    Fn();                                // fire the already-queued callback — must no-op
    TestEqual(TEXT("no ping after stop"), Pings, PingsAfterStart);
    return true;
}

// ---------------------------------------------------------------------------
// Re-degrade: success resets bDegradedReported so a fresh run of 6 failures
// can fire OnDegraded a second time.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfHealthRedegradeTest, "GameFlow.Health.RedegradeAfterRecovery",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfHealthRedegradeTest::RunTest(const FString& P) {
    FNullGameFlowLogger Log;
    int32 Pings = 0, Degraded = 0;
    TArray<TFunction<void()>> Scheduled;
    // Fail calls 1-6, succeed call 7, fail calls 8-13 (6 more).
    auto Ping     = [&](TFunction<void(bool)> Done){ ++Pings; Done(Pings == 7 || Pings > 13); };
    auto Schedule = [&](int32 Ms, TFunction<void()> Fn){ Scheduled.Add(MoveTemp(Fn)); };
    FGfHealthLoop Loop(Ping, Schedule, 500, [&]{ Degraded++; }, Log);

    // Phase 1: 6 failures → degrade once.
    Loop.Start();  // ping #1
    for (int i = 0; i < 5 && Scheduled.Num(); ++i)
        { TFunction<void()> Fn = Scheduled.Pop(); Fn(); }
    TestEqual(TEXT("degraded once after 6 failures"), Degraded, 1);

    // Phase 2: 1 success → resets ConsecutiveFailures and bDegradedReported.
    if (Scheduled.Num()) { TFunction<void()> Fn = Scheduled.Pop(); Fn(); } // ping #7 succeeds

    // Phase 3: 6 more failures → degrade a second time.
    for (int i = 0; i < 6 && Scheduled.Num(); ++i)
        { TFunction<void()> Fn = Scheduled.Pop(); Fn(); }
    TestEqual(TEXT("degraded twice after second run of 6 failures"), Degraded, 2);

    Loop.Stop();
    return true;
}

// ---------------------------------------------------------------------------
// In-flight ping completion after destruction must NOT touch the freed loop
// (C1 defense-in-depth). The health ping is autonomous, so its completion can
// fire after the loop is gone. We capture the completion (simulating a request
// still in flight), destroy the loop, then invoke the completion: the loop's
// Alive sentinel must make it a safe no-op. Pre-fix the completion dereferenced
// the freed loop (e.g. called the freed ScheduleAfter TFunction) → UAF.
//
// Deterministic — no fixture, no timing: the captured completion is held in a
// holder that outlives the loop, exactly as an in-flight async op would be.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfHealthInFlightAfterDestructTest, "GameFlow.Health.InFlightCompletionAfterDestructNoUAF",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfHealthInFlightAfterDestructTest::RunTest(const FString& P) {
    FNullGameFlowLogger Log;
    int32 Scheduled = 0;

    // Capture the ping completion instead of invoking it — it is now "in flight".
    TFunction<void(bool)> InFlight;
    auto Ping     = [&](TFunction<void(bool)> Done){ InFlight = MoveTemp(Done); };
    auto Schedule = [&](int32 Ms, TFunction<void()> Fn){ ++Scheduled; }; // must NOT be reached post-destruct

    {
        FGfHealthLoop Loop(Ping, Schedule, 500, []{}, Log);
        Loop.Start();          // fires ping #1; its completion is now captured in InFlight
    }                          // Loop destroyed here WITHOUT calling the completion

    TestTrue(TEXT("completion captured in flight"), (bool)InFlight);

    // Fire the in-flight completion after the loop is gone: the Alive sentinel
    // must short-circuit it (no member access, no reschedule).
    InFlight(true);

    TestEqual(TEXT("no reschedule after destruct"), Scheduled, 0);
    TestTrue(TEXT("survived in-flight completion after destruct"), true);
    return true;
}
#endif
