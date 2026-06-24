#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "GameFlow/LocalTransport.h"
#include "GameFlow/GameFlowEnv.h"
#include "GameFlow/GameFlowLogger.h"

// ---------------------------------------------------------------------------
// Basic in-memory player tracking: add, full, already-connected, remove
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfLocalTransportTest, "GameFlow.LocalTransport.TracksInMemory",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfLocalTransportTest::RunTest(const FString& P) {
    FNullGameFlowLogger Log;
    FGameFlowEnvReader Env([](const FString& K, FString& V){ if (K==TEXT("GAMEFLOW_MAX_PLAYERS")){V=TEXT("2");return true;} return false; });
    FLocalTransport T(Env, Log);
    FGameFlowError Err; FPlayerList After; bool Found=false;
    T.AddPlayer(TEXT("a"), 2, FGfListResult::CreateLambda([&](const FGameFlowError& E, const FPlayerList& L){ Err=E; After=L; }));
    TestTrue(TEXT("add ok"), Err.IsOk()); TestEqual(TEXT("one"), After.SessionIds.Num(), 1);
    // full
    T.AddPlayer(TEXT("b"), 2, FGfListResult::CreateLambda([&](const FGameFlowError& E, const FPlayerList&){ Err=E; }));
    TestTrue(TEXT("ok b"), Err.IsOk());
    T.AddPlayer(TEXT("c"), 2, FGfListResult::CreateLambda([&](const FGameFlowError& E, const FPlayerList&){ Err=E; }));
    TestEqual(TEXT("full"), Err.Code, EGameFlowErrorCode::ServerFull);
    // already
    T.AddPlayer(TEXT("a"), 2, FGfListResult::CreateLambda([&](const FGameFlowError& E, const FPlayerList&){ Err=E; }));
    TestEqual(TEXT("already"), Err.Code, EGameFlowErrorCode::PlayerAlreadyConnected);
    // remove found / not found
    T.RemovePlayer(TEXT("a"), FGfRemoveResult::CreateLambda([&](const FGameFlowError& E, bool F, const FPlayerList&){ Err=E; Found=F; }));
    TestTrue(TEXT("found"), Found);
    T.RemovePlayer(TEXT("ghost"), FGfRemoveResult::CreateLambda([&](const FGameFlowError& E, bool F, const FPlayerList&){ Found=F; }));
    TestFalse(TEXT("not found"), Found);
    return true;
}

// ---------------------------------------------------------------------------
// Tracking disabled (MAX_PLAYERS=0): AddPlayer and RemovePlayer both yield
// PlayerTrackingDisabled
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfLocalTransportTrackingDisabledTest, "GameFlow.LocalTransport.TrackingDisabled",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfLocalTransportTrackingDisabledTest::RunTest(const FString& P) {
    FNullGameFlowLogger Log;
    FGameFlowEnvReader Env([](const FString& K, FString& V){ if (K==TEXT("GAMEFLOW_MAX_PLAYERS")){V=TEXT("0");return true;} return false; });
    FLocalTransport T(Env, Log);
    FGameFlowError AddErr, RemErr;
    T.AddPlayer(TEXT("x"), 0, FGfListResult::CreateLambda([&](const FGameFlowError& E, const FPlayerList&){ AddErr=E; }));
    TestEqual(TEXT("add disabled"), AddErr.Code, EGameFlowErrorCode::PlayerTrackingDisabled);
    T.RemovePlayer(TEXT("x"), FGfRemoveResult::CreateLambda([&](const FGameFlowError& E, bool, const FPlayerList&){ RemErr=E; }));
    TestEqual(TEXT("remove disabled"), RemErr.Code, EGameFlowErrorCode::PlayerTrackingDisabled);
    return true;
}

// ---------------------------------------------------------------------------
// Watch fires on mutation, and Close() unregisters the handler
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfLocalTransportWatchTest, "GameFlow.LocalTransport.WatchFiresAndCloses",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfLocalTransportWatchTest::RunTest(const FString& P) {
    FNullGameFlowLogger Log;
    FGameFlowEnvReader Env([](const FString& K, FString& V){ if (K==TEXT("GAMEFLOW_MAX_PLAYERS")){V=TEXT("2");return true;} return false; });
    FLocalTransport T(Env, Log);
    int32 FireCount = 0;
    FServerInfo LastFrame;
    TUniquePtr<IGfWatchConnection> Conn = T.OpenWatch(
        [&](const FServerInfo& S){ ++FireCount; LastFrame=S; },
        [](){});
    // AddPlayer should fire the watch
    T.AddPlayer(TEXT("a"), 2, FGfListResult::CreateLambda([](const FGameFlowError&, const FPlayerList&){}));
    TestEqual(TEXT("fire after add"), FireCount, 1);
    TestTrue(TEXT("frame has a"), LastFrame.Players.SessionIds.Contains(TEXT("a")));
    // RemovePlayer should fire the watch
    T.RemovePlayer(TEXT("a"), FGfRemoveResult::CreateLambda([](const FGameFlowError&, bool, const FPlayerList&){}));
    TestEqual(TEXT("fire after remove"), FireCount, 2);
    // Close unregisters — subsequent mutation must NOT increment counter
    Conn->Close();
    T.AddPlayer(TEXT("b"), 2, FGfListResult::CreateLambda([](const FGameFlowError&, const FPlayerList&){}));
    TestEqual(TEXT("no fire after close"), FireCount, 2);
    return true;
}
#endif
