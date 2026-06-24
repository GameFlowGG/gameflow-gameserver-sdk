#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "GameFlow/GfJson.h"
#include "Dom/JsonValue.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfJsonTest, "GameFlow.Json.CoerceAndBuild",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfJsonTest::RunTest(const FString& P) {
    TSharedPtr<FJsonObject> Obj = FGfJson::ParseObject(TEXT("{\"capacity\":\"8\",\"n\":42}"));
    TestTrue(TEXT("parsed"), Obj.IsValid());
    TestEqual(TEXT("string int64"), FGfJson::AsInt64(Obj->TryGetField(TEXT("capacity"))), (int64)8);
    TestEqual(TEXT("number int64"), FGfJson::AsInt64(Obj->TryGetField(TEXT("n"))), (int64)42);
    TestNull(TEXT("bad parse"), FGfJson::ParseObject(TEXT("not json")).Get());
    TestEqual(TEXT("empty body"), FGfJson::ObjEmpty(), FString(TEXT("{}")));
    TestEqual(TEXT("value body"), FGfJson::ObjValue(TEXT("a\"b")), FString(TEXT("{\"value\":\"a\\\"b\"}")));
    return true;
}
#endif
