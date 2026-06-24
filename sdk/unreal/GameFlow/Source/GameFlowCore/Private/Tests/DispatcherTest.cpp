#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "GameFlow/GameFlowOptions.h" // FInlineDispatcher

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfInlineDispatcherTest, "GameFlow.Dispatcher.InlineRunsImmediately",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfInlineDispatcherTest::RunTest(const FString& P)
{
    FInlineDispatcher D; int32 Ran = 0; D.Post([&]{ Ran++; });
    TestEqual(TEXT("ran inline"), Ran, 1);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
