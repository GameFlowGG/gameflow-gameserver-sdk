#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "GameFlow/GameFlowEnv.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfEnvTest, "GameFlow.Env.PortVarParsesOrAbsent",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfEnvTest::RunTest(const FString& P) {
    TMap<FString,FString> M = {{TEXT("GAMEFLOW_DEFAULT_PORT"),TEXT("7777")},{TEXT("GAMEFLOW_BAD"),TEXT("x")}};
    FGameFlowEnvReader R([&](const FString& K, FString& V){ if (M.Contains(K)){ V=M[K]; return true; } return false; });
    int32 Port = -1;
    TestTrue(TEXT("good"), R.PortVar(TEXT("GAMEFLOW_DEFAULT_PORT"), Port)); TestEqual(TEXT("v"), Port, 7777);
    TestFalse(TEXT("bad parse"), R.PortVar(TEXT("GAMEFLOW_BAD"), Port));
    TestFalse(TEXT("missing"), R.PortVar(TEXT("NOPE"), Port));
    return true;
}
#endif
