#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "GameFlow/GameFlowError.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfErrorTest, "GameFlow.Error.CodesAndCapacity",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfErrorTest::RunTest(const FString& P) {
    const FGameFlowError Full = FGameFlowError::Make(EGameFlowErrorCode::ServerFull, TEXT("full"), 8);
    TestFalse(TEXT("not ok"), Full.IsOk());
    TestEqual(TEXT("capacity"), Full.Capacity, (int64)8);
    TestEqual(TEXT("token"), Full.CodeString(), FString(TEXT("SERVER_FULL")));
    TestTrue(TEXT("ok is ok"), FGameFlowError::Ok().IsOk());
    TestEqual(TEXT("ok token empty"), FGameFlowError::Ok().CodeString(), FString());
    // All six tokens are the cross-language contract — a typo must be caught here.
    const auto Tok = [](EGameFlowErrorCode C){ return FGameFlowError::Make(C, TEXT("")).CodeString(); };
    TestEqual(TEXT("sidecar unavailable"),     Tok(EGameFlowErrorCode::SidecarUnavailable),     FString(TEXT("SIDECAR_UNAVAILABLE")));
    TestEqual(TEXT("player already connected"), Tok(EGameFlowErrorCode::PlayerAlreadyConnected), FString(TEXT("PLAYER_ALREADY_CONNECTED")));
    TestEqual(TEXT("server full"),             Tok(EGameFlowErrorCode::ServerFull),             FString(TEXT("SERVER_FULL")));
    TestEqual(TEXT("player tracking disabled"), Tok(EGameFlowErrorCode::PlayerTrackingDisabled), FString(TEXT("PLAYER_TRACKING_DISABLED")));
    TestEqual(TEXT("not connected"),           Tok(EGameFlowErrorCode::NotConnected),           FString(TEXT("NOT_CONNECTED")));
    TestEqual(TEXT("request failed"),          Tok(EGameFlowErrorCode::RequestFailed),          FString(TEXT("REQUEST_FAILED")));
    return true;
}
#endif
