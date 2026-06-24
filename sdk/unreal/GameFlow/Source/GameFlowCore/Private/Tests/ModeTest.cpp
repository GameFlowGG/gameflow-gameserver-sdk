#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "GameFlow/GameFlowOptions.h"
#include "GameFlow/GameFlowEnv.h"
#include "GameFlow/GameFlowLogger.h"
#include "GameFlow/GfModeDetection.h"
static FGameFlowEnvReader EnvOf(TMap<FString,FString> M) {
    return FGameFlowEnvReader([M](const FString& K, FString& V){ if (M.Contains(K)){V=M[K];return true;} return false; }); }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfModeTest, "GameFlow.Mode.ExplicitEnvAuto",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfModeTest::RunTest(const FString& P) {
    FNullGameFlowLogger Log;
    FGameFlowOptions Explicit; Explicit.Mode = EGameFlowMode::Local;
    TestEqual(TEXT("explicit>env"), FGfModeDetection::Resolve(Explicit, EnvOf({{TEXT("AGONES_SDK_HTTP_PORT"),TEXT("9358")}}), Log), EGameFlowMode::Local);
    FGameFlowOptions Auto;
    TestEqual(TEXT("env>auto"), FGfModeDetection::Resolve(Auto, EnvOf({{TEXT("GAMEFLOW_SDK_MODE"),TEXT("local")},{TEXT("AGONES_SDK_HTTP_PORT"),TEXT("9358")}}), Log), EGameFlowMode::Local);
    TestEqual(TEXT("auto sidecar"), FGfModeDetection::Resolve(Auto, EnvOf({{TEXT("AGONES_SDK_HTTP_PORT"),TEXT("9358")}}), Log), EGameFlowMode::Sidecar);
    TestEqual(TEXT("auto local"), FGfModeDetection::Resolve(Auto, EnvOf({}), Log), EGameFlowMode::Local);
    return true;
}
#endif
