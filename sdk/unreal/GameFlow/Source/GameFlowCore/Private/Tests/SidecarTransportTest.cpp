#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "GameFlow/SidecarTransport.h"
#include "GameFlow/GameFlowLogger.h"
#include "Fixture.h"

// ---------------------------------------------------------------------------
// Fixture-backed mapping: Ready succeeds; AddPlayer maps capacity -> ServerFull
// and a duplicate -> PlayerAlreadyConnected; /ready is recorded exactly once.
// Latent because results arrive via game-thread HTTP completions (GfPumpUntil).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfSidecarTest, "GameFlow.Sidecar.ReadyAddRemoveMapping",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfSidecarTest::RunTest(const FString& P)
{
    if (!Fixture::NodeAvailable())
    {
        AddInfo(TEXT("node >= 23.6 absent; skipping"));
        return true;
    }

    Fixture Fx(TEXT("--players-capacity=2 --players-seed=seeded"));
    TestTrue(TEXT("fixture started"), Fx.IsValid());

    FNullGameFlowLogger Log;
    FSidecarTransport T(FString::Printf(TEXT("http://127.0.0.1:%d"), Fx.Port), 3000, Log);

    FGameFlowError Err;
    bool Done = false;
    T.Ready(FGfVoidResult::CreateLambda([&](const FGameFlowError& E) { Err = E; Done = true; }));
    GfPumpUntil(*this, [&] { return Done; }, 5.0);
    TestTrue(TEXT("ready ok"), Err.IsOk());

    // add p1 ok, then capacity-2 with seeded+p1 -> full on p2; already on seeded
    bool A = false;
    T.AddPlayer(TEXT("p1"), 2, FGfListResult::CreateLambda([&](const FGameFlowError& E, const FPlayerList& L) { Err = E; A = true; }));
    GfPumpUntil(*this, [&] { return A; }, 5.0);
    TestTrue(TEXT("p1 ok"), Err.IsOk());

    bool B = false;
    T.AddPlayer(TEXT("p2"), 2, FGfListResult::CreateLambda([&](const FGameFlowError& E, const FPlayerList&) { Err = E; B = true; }));
    GfPumpUntil(*this, [&] { return B; }, 5.0);
    TestEqual(TEXT("full"), Err.Code, EGameFlowErrorCode::ServerFull);

    bool C = false;
    T.AddPlayer(TEXT("seeded"), 2, FGfListResult::CreateLambda([&](const FGameFlowError& E, const FPlayerList&) { Err = E; C = true; }));
    GfPumpUntil(*this, [&] { return C; }, 5.0);
    TestEqual(TEXT("already"), Err.Code, EGameFlowErrorCode::PlayerAlreadyConnected);

    TestEqual(TEXT("ready counted once"), Fx.CountRequests(TEXT("/ready")), 1);
    return true;
}

// ---------------------------------------------------------------------------
// Tracking disabled: with no --players-capacity the sidecar never initializes
// the players list, so it 404s ("list not found"). AddPlayer maps 404 ->
// PlayerTrackingDisabled; RemovePlayer's 404 re-read also 404s -> disabled.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfSidecarTrackingDisabledTest, "GameFlow.Sidecar.TrackingDisabled",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfSidecarTrackingDisabledTest::RunTest(const FString& P)
{
    if (!Fixture::NodeAvailable())
    {
        AddInfo(TEXT("node >= 23.6 absent; skipping"));
        return true;
    }

    Fixture Fx(TEXT("")); // no players list configured
    TestTrue(TEXT("fixture started"), Fx.IsValid());

    FNullGameFlowLogger Log;
    FSidecarTransport T(FString::Printf(TEXT("http://127.0.0.1:%d"), Fx.Port), 3000, Log);

    FGameFlowError AddErr;
    bool A = false;
    T.AddPlayer(TEXT("x"), 0, FGfListResult::CreateLambda([&](const FGameFlowError& E, const FPlayerList&) { AddErr = E; A = true; }));
    GfPumpUntil(*this, [&] { return A; }, 5.0);
    TestEqual(TEXT("add disabled"), AddErr.Code, EGameFlowErrorCode::PlayerTrackingDisabled);

    FGameFlowError RemErr;
    bool R = false;
    T.RemovePlayer(TEXT("x"), FGfRemoveResult::CreateLambda([&](const FGameFlowError& E, bool, const FPlayerList&) { RemErr = E; R = true; }));
    GfPumpUntil(*this, [&] { return R; }, 5.0);
    TestEqual(TEXT("remove disabled"), RemErr.Code, EGameFlowErrorCode::PlayerTrackingDisabled);
    return true;
}

// ---------------------------------------------------------------------------
// RemovePlayer happy path + not-present disambiguation: removing a seeded value
// succeeds (found:true, gone from the re-read list); removing an absent value
// 404s then re-reads a 2xx list -> Ok, found:false.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfSidecarRemoveTest, "GameFlow.Sidecar.RemovePlayerFoundAndMissing",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfSidecarRemoveTest::RunTest(const FString& P)
{
    if (!Fixture::NodeAvailable())
    {
        AddInfo(TEXT("node >= 23.6 absent; skipping"));
        return true;
    }

    Fixture Fx(TEXT("--players-capacity=2 --players-seed=seeded"));
    TestTrue(TEXT("fixture started"), Fx.IsValid());

    FNullGameFlowLogger Log;
    FSidecarTransport T(FString::Printf(TEXT("http://127.0.0.1:%d"), Fx.Port), 3000, Log);

    FGameFlowError Err;
    bool Found = false;
    FPlayerList After;
    bool R1 = false;
    T.RemovePlayer(TEXT("seeded"), FGfRemoveResult::CreateLambda([&](const FGameFlowError& E, bool F, const FPlayerList& L) { Err = E; Found = F; After = L; R1 = true; }));
    GfPumpUntil(*this, [&] { return R1; }, 5.0);
    TestTrue(TEXT("remove seeded ok"), Err.IsOk());
    TestTrue(TEXT("seeded found"), Found);
    TestFalse(TEXT("seeded gone from list"), After.SessionIds.Contains(TEXT("seeded")));

    bool R2 = false;
    T.RemovePlayer(TEXT("ghost"), FGfRemoveResult::CreateLambda([&](const FGameFlowError& E, bool F, const FPlayerList&) { Err = E; Found = F; R2 = true; }));
    GfPumpUntil(*this, [&] { return R2; }, 5.0);
    TestTrue(TEXT("remove ghost ok"), Err.IsOk());
    TestFalse(TEXT("ghost not found"), Found);
    return true;
}

// ---------------------------------------------------------------------------
// Probe connection failure -> SidecarUnavailable: no fixture, pointed at a port
// with nothing listening. Connection refused fires bConnectedSuccessfully=false.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfSidecarProbeUnavailableTest, "GameFlow.Sidecar.ProbeUnavailable",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfSidecarProbeUnavailableTest::RunTest(const FString& P)
{
    if (!Fixture::NodeAvailable())
    {
        AddInfo(TEXT("node >= 23.6 absent; skipping"));
        return true;
    }

    FNullGameFlowLogger Log;
    // Port 9 (discard) has nothing listening on this host -> connection refused.
    FSidecarTransport T(TEXT("http://127.0.0.1:9"), 3000, Log);

    FGameFlowError Err;
    bool Done = false;
    T.Probe(FGfInfoResult::CreateLambda([&](const FGameFlowError& E, const FServerInfo&) { Err = E; Done = true; }));
    GfPumpUntil(*this, [&] { return Done; }, 5.0);
    TestEqual(TEXT("probe unavailable"), Err.Code, EGameFlowErrorCode::SidecarUnavailable);
    return true;
}

// ---------------------------------------------------------------------------
// Probe 2xx parse: against the fixture, Probe succeeds and yields a populated
// FServerInfo (proving GET /gameserver -> ParseGameServer runs).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfSidecarProbeParseTest, "GameFlow.Sidecar.ProbeParses",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfSidecarProbeParseTest::RunTest(const FString& P)
{
    if (!Fixture::NodeAvailable())
    {
        AddInfo(TEXT("node >= 23.6 absent; skipping"));
        return true;
    }

    Fixture Fx(TEXT("--players-capacity=2 --players-seed=seeded"));
    TestTrue(TEXT("fixture started"), Fx.IsValid());

    FNullGameFlowLogger Log;
    FSidecarTransport T(FString::Printf(TEXT("http://127.0.0.1:%d"), Fx.Port), 3000, Log);

    FGameFlowError Err;
    FServerInfo Info;
    bool Done = false;
    T.Probe(FGfInfoResult::CreateLambda([&](const FGameFlowError& E, const FServerInfo& I) { Err = E; Info = I; Done = true; }));
    GfPumpUntil(*this, [&] { return Done; }, 5.0);
    TestTrue(TEXT("probe ok"), Err.IsOk());
    TestFalse(TEXT("state parsed"), Info.State.IsEmpty());
    TestFalse(TEXT("address parsed"), Info.Address.IsEmpty());
    return true;
}

// ---------------------------------------------------------------------------
// Generic non-2xx -> RequestFailed: --fail-first=1 makes the first request 503.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfSidecarRequestFailedTest, "GameFlow.Sidecar.RequestFailedOn503",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfSidecarRequestFailedTest::RunTest(const FString& P)
{
    if (!Fixture::NodeAvailable())
    {
        AddInfo(TEXT("node >= 23.6 absent; skipping"));
        return true;
    }

    Fixture Fx(TEXT("--fail-first=1"));
    TestTrue(TEXT("fixture started"), Fx.IsValid());

    FNullGameFlowLogger Log;
    FSidecarTransport T(FString::Printf(TEXT("http://127.0.0.1:%d"), Fx.Port), 3000, Log);

    FGameFlowError Err;
    bool Done = false;
    T.Ready(FGfVoidResult::CreateLambda([&](const FGameFlowError& E) { Err = E; Done = true; }));
    GfPumpUntil(*this, [&] { return Done; }, 5.0);
    TestEqual(TEXT("ready 503 -> request failed"), Err.Code, EGameFlowErrorCode::RequestFailed);
    return true;
}

// ---------------------------------------------------------------------------
// Destroy-with-pending-mutation no-UAF (mirrors Conformance.HealthDestructNoUAF):
// heap-own the transport, issue an AddPlayer (and a RemovePlayer), then DESTROY
// it WITHOUT pumping — the POST completions are still in flight inside the
// FHttpManager. Pump for ~1s so they fire on the freed transport; the Alive
// sentinel makes the mutation completions bail before dereferencing `this`.
// Surviving the pump without crashing IS the assertion (pre-fix this UAFs);
// the bailed completions must also NOT invoke OnDone.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfSidecarDestructTest, "GameFlow.Sidecar.DestroyWithPendingMutationNoUAF",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfSidecarDestructTest::RunTest(const FString& P)
{
    if (!Fixture::NodeAvailable())
    {
        AddInfo(TEXT("node >= 23.6 absent; skipping"));
        return true;
    }

    Fixture Fx(TEXT("--players-capacity=4 --players-seed=seeded"));
    TestTrue(TEXT("fixture started"), Fx.IsValid());

    FNullGameFlowLogger Log;
    TUniquePtr<FSidecarTransport> T = MakeUnique<FSidecarTransport>(
        FString::Printf(TEXT("http://127.0.0.1:%d"), Fx.Port), 3000, Log);

    // These must NOT fire after the transport is destroyed (sentinel bails first).
    bool AddFired = false, RemFired = false;
    T->AddPlayer(TEXT("p1"), 4, FGfListResult::CreateLambda([&](const FGameFlowError&, const FPlayerList&) { AddFired = true; }));
    T->RemovePlayer(TEXT("seeded"), FGfRemoveResult::CreateLambda([&](const FGameFlowError&, bool, const FPlayerList&) { RemFired = true; }));

    // Destroy WITHOUT pumping — both POSTs are still in flight in the HTTP manager.
    T.Reset();

    // Pump well past request completion so the in-flight completions land on the
    // freed transport. A surviving deref would UAF (the ASAN-detectable payoff).
    GfPumpUntil(*this, [] { return false; }, 1.5);

    TestFalse(TEXT("add callback did not fire after destruct"), AddFired);
    TestFalse(TEXT("remove callback did not fire after destruct"), RemFired);
    TestTrue(TEXT("survived destroy-with-pending-mutation"), true);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
