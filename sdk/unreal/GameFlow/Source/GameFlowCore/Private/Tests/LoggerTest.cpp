#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "GameFlow/GameFlowLogger.h"
class FRecordingLogger : public IGameFlowLogger {
public: TArray<FString> Lines;
    void Info(const FString& M) override { Lines.Add(TEXT("I:")+M); }
    void Warn(const FString& M) override { Lines.Add(TEXT("W:")+M); }
    void Error(const FString& M) override { Lines.Add(TEXT("E:")+M); }
    void Verbose(const FString& M) override { Lines.Add(TEXT("V:")+M); }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfLoggerTest, "GameFlow.Logger.NullDiscardsRecordingCaptures",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfLoggerTest::RunTest(const FString& P) {
    FNullGameFlowLogger Null; Null.Info(TEXT("x")); Null.Error(TEXT("y")); // must not crash, discards
    FRecordingLogger Rec; Rec.Warn(TEXT("hi"));
    TestEqual(TEXT("recorded"), Rec.Lines.Num(), 1);
    TestEqual(TEXT("line"), Rec.Lines[0], FString(TEXT("W:hi")));
    return true;
}
#endif
