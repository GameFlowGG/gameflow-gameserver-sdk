#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "GameFlow/GameFlowClient.h"
#include "GameFlow/GameFlowPlayers.h"
#include "Fixture.h"

// ---------------------------------------------------------------------------
// Full-lifecycle conformance suite — the cross-language conformance test ported
// from the Go/Unity SDKs. Fixture-backed and latent: it self-skips when Node is
// absent. A single run exercises the deferred coverage from Tasks 10/13:
//   - connect-with-retry (survives 2 forced 503s via --fail-first=2),
//   - seeded Players cache (tracking, capacity, count),
//   - automatic health pings after Ready,
//   - OnPayloadChange fires on a real change only,
//   - ServerFull carries capacity, NOT_CONNECTED after Shutdown,
//   - idempotent shutdown (/ready and /shutdown each hit exactly once).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfConformanceTest, "GameFlow.Conformance.FullLifecycle",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfConformanceTest::RunTest(const FString& P)
{
    if (!Fixture::NodeAvailable())
    {
        AddInfo(TEXT("node >= 23.6 absent; skipping"));
        return true;
    }

    Fixture Fx(TEXT("--players-capacity=2 --players-seed=seeded --fail-first=2"));
    TestTrue(TEXT("fixture started"), Fx.IsValid());

    FGameFlowOptions O;
    O.HealthIntervalMs = 500;
    O.EnvProvider = [Port = Fx.Port](const FString& K, FString& V)
    {
        if (K == TEXT("AGONES_SDK_HTTP_PORT")) { V = FString::FromInt(Port); return true; }
        return false;
    };

    FGameFlowClient Gf(O);

    FGameFlowError Err;
    bool Done = false;

    // Start must survive the 2 forced failures (connect retries) and seed the cache.
    Gf.Start(FGfVoidResult::CreateLambda([&](const FGameFlowError& E) { Err = E; Done = true; }));
    GfPumpUntil(*this, [&] { return Done; }, 10.0);
    TestTrue(TEXT("connected after retry"), Err.IsOk());
    TestTrue(TEXT("tracking"), Gf.Players().TrackingEnabled());
    TestEqual(TEXT("seeded capacity"), Gf.Players().Capacity(), (int64)2);
    TestEqual(TEXT("seeded count"), Gf.Players().Count(), 1);

    // Ready starts the automatic health heartbeat (sidecar mode).
    Done = false;
    Gf.Ready(FGfVoidResult::CreateLambda([&](const FGameFlowError& E) { Err = E; Done = true; }));
    GfPumpUntil(*this, [&] { return Done; }, 5.0);
    TestTrue(TEXT("ready"), Err.IsOk());
    GfPumpUntil(*this, [&] { return Fx.CountRequests(TEXT("/health")) >= 2; }, 5.0);
    TestTrue(TEXT("health pinged at least twice"), Fx.CountRequests(TEXT("/health")) >= 2);

    // OnPayloadChange fires only when the payload actually changes.
    int32 PayloadHits = 0;
    FString LastPayload;
    Gf.OnPayloadChange([&](const FString& Pl, bool) { PayloadHits++; LastPayload = Pl; });
    Fx.ControlPost(TEXT("/set-payload?value=match-7"));
    GfPumpUntil(*this, [&] { return PayloadHits >= 1; }, 5.0);
    TestEqual(TEXT("payload changed"), LastPayload, FString(TEXT("match-7")));

    // Player mappings: p1 fits (seeded+p1 = 2), p2 overflows capacity 2 -> ServerFull.
    bool Step = false;
    Gf.Players().Connect(TEXT("p1"), FGfVoidResult::CreateLambda([&](const FGameFlowError& E) { Err = E; Step = true; }));
    GfPumpUntil(*this, [&] { return Step; }, 5.0);
    TestTrue(TEXT("p1 ok"), Err.IsOk());

    Step = false;
    Gf.Players().Connect(TEXT("p2"), FGfVoidResult::CreateLambda([&](const FGameFlowError& E) { Err = E; Step = true; }));
    GfPumpUntil(*this, [&] { return Step; }, 5.0);
    TestEqual(TEXT("full"), Err.Code, EGameFlowErrorCode::ServerFull);

    // Idempotent shutdown + post-shutdown NOT_CONNECTED.
    Done = false;
    Gf.Shutdown(FGfVoidResult::CreateLambda([&](const FGameFlowError& E) { Err = E; Done = true; }));
    GfPumpUntil(*this, [&] { return Done; }, 5.0);
    TestTrue(TEXT("shutdown ok"), Err.IsOk());

    bool After = false;
    Gf.Players().Connect(TEXT("x"), FGfVoidResult::CreateLambda([&](const FGameFlowError& E) { Err = E; After = true; }));
    GfPumpUntil(*this, [&] { return After; }, 5.0);
    TestEqual(TEXT("not connected"), Err.Code, EGameFlowErrorCode::NotConnected);

    TestEqual(TEXT("ready once"), Fx.CountRequests(TEXT("/ready")), 1);
    TestEqual(TEXT("shutdown once"), Fx.CountRequests(TEXT("/shutdown")), 1);
    return true;
}

// ---------------------------------------------------------------------------
// Destruct-without-Shutdown must not UAF on the autonomous health ping (C1).
//
// Start + Ready a sidecar client so the health loop is running, pump until at
// least one /health ping lands, then DESTROY the client WITHOUT Shutdown and
// keep pumping for a window >= HealthIntervalMs. Two hazards are exercised:
//   1. the pending next-ping core ticker (must be RemoveTicker'd by the dtor),
//   2. an in-flight Transport->Health(...) completion (must bail via the loop's
//      Alive sentinel).
// Surviving the post-destruction pump IS the assertion: pre-fix the pending
// health tick / in-flight completion would deref the freed loop and crash.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfHealthDestructTest, "GameFlow.Conformance.HealthDestructNoUAF",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfHealthDestructTest::RunTest(const FString& P)
{
    if (!Fixture::NodeAvailable())
    {
        AddInfo(TEXT("node >= 23.6 absent; skipping"));
        return true;
    }

    Fixture Fx(TEXT("--players-capacity=2 --players-seed=seeded"));
    TestTrue(TEXT("fixture started"), Fx.IsValid());

    FGameFlowOptions O;
    O.HealthIntervalMs = 500; // clamped minimum; the pending-tick window we must survive
    O.EnvProvider = [Port = Fx.Port](const FString& K, FString& V)
    {
        if (K == TEXT("AGONES_SDK_HTTP_PORT")) { V = FString::FromInt(Port); return true; }
        return false;
    };

    // Heap-own the client so we can destroy it mid-test, leaving the health loop
    // running (no Shutdown, no Stop) — the exact crash path.
    TUniquePtr<FGameFlowClient> Gf = MakeUnique<FGameFlowClient>(O);

    bool Done = false;
    Gf->Start(FGfVoidResult::CreateLambda([&](const FGameFlowError&) { Done = true; }));
    TestTrue(TEXT("started"), GfPumpUntil(*this, [&] { return Done; }, 10.0));

    Done = false;
    Gf->Ready(FGfVoidResult::CreateLambda([&](const FGameFlowError&) { Done = true; }));
    TestTrue(TEXT("ready"), GfPumpUntil(*this, [&] { return Done; }, 5.0));

    // Ensure the health loop is actually live (>= 1 ping) before we destroy it.
    TestTrue(TEXT("health pinged"), GfPumpUntil(*this, [&] { return Fx.CountRequests(TEXT("/health")) >= 1; }, 5.0));

    // Destroy WITHOUT Shutdown — a next-ping tick is pending and a ping may be in flight.
    const int32 HealthAtDestroy = Fx.CountRequests(TEXT("/health"));
    Gf.Reset();

    // Pump well past two HealthIntervalMs windows: a surviving ticker/completion
    // would UAF (the primary hazard), and a surviving pending tick would also fire
    // at least one MORE /health. Both must be gone.
    GfPumpUntil(*this, [] { return false; }, 1.5);

    // Observable assertion (deterministic, allocator-independent): the cancelled
    // pending tick fired no further ping after destruction. Surviving the pump
    // without crashing is the secondary assertion (the ASAN-detectable payoff).
    TestEqual(TEXT("no health ping after destruct"), Fx.CountRequests(TEXT("/health")), HealthAtDestroy);
    TestTrue(TEXT("survived destruct-without-shutdown"), true);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
