#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "GameFlow/GfBackoff.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfBackoffTest, "GameFlow.Backoff.DoublesJitterCapsResets",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfBackoffTest::RunTest(const FString& P) {
    FGfBackoff B(FRandomStream(1));
    const int32 D0 = B.NextDelayMs(), D1 = B.NextDelayMs();
    TestTrue(TEXT("d0 250±20%"), D0 >= 200 && D0 <= 300);
    TestTrue(TEXT("d1 500±20%"), D1 >= 400 && D1 <= 600);
    for (int i=0;i<10;i++) B.NextDelayMs();
    const int32 Capped = B.NextDelayMs();
    TestTrue(TEXT("capped 4000±20%"), Capped >= 3200 && Capped <= 4800);
    B.Reset();
    TestTrue(TEXT("reset to base"), B.NextDelayMs() >= 200 && B.NextDelayMs() <= 600);
    return true;
}
#endif
