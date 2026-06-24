#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "GameFlow/GameFlowPlayers.h"
#include "GameFlow/LocalTransport.h"
#include "GameFlow/GameFlowEnv.h"
#include "GameFlow/GameFlowLogger.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfPlayersTest, "GameFlow.Players.ConnectUpdatesCache",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfPlayersTest::RunTest(const FString& P) {
    FNullGameFlowLogger Log;
    FGameFlowEnvReader Env([](const FString& K, FString& V){ if (K==TEXT("GAMEFLOW_MAX_PLAYERS")){V=TEXT("3");return true;} return false; });
    FLocalTransport T(Env, Log);
    FGameFlowPlayers Players(T);
    FServerInfo Seed; T.Probe(FGfInfoResult::CreateLambda([&](const FGameFlowError&, const FServerInfo& I){ Seed = I; }));
    Players.SetCache(Seed.Players);
    FGameFlowError Err; bool Done=false, Found=false;
    Players.Connect(TEXT("a"), FGfVoidResult::CreateLambda([&](const FGameFlowError& E){ Err=E; Done=true; }));
    TestTrue(TEXT("ok"), Err.IsOk()); TestEqual(TEXT("count 1"), Players.Count(), 1);
    TestTrue(TEXT("has a"), Players.List().Contains(TEXT("a")));
    Players.Disconnect(TEXT("a"), FGfRemoveResult::CreateLambda([&](const FGameFlowError& E, bool F, const FPlayerList&){ Found=F; }));
    TestTrue(TEXT("found"), Found); TestEqual(TEXT("count 0"), Players.Count(), 0);
    Players.Disconnect(TEXT("a"), FGfRemoveResult::CreateLambda([&](const FGameFlowError&, bool F, const FPlayerList&){ Found=F; }));
    TestFalse(TEXT("idempotent"), Found);
    return true;
}
#endif
