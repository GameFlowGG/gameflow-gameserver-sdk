#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "GameFlow/GfModel.h"
#include "GameFlow/GfJson.h"

// ---------------------------------------------------------------------------
// Core parse: ParseList + ParseGameServer round-trip
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfModelTest, "GameFlow.Model.ParseGameServerAndList",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfModelTest::RunTest(const FString& P) {
    const FPlayerList L = FGfModel::ParseList(FGfJson::ParseObject(
        TEXT("{\"capacity\":\"8\",\"values\":[\"a\",\"b\"]}")));
    TestTrue(TEXT("tracked"), L.bTrackingEnabled);
    TestEqual(TEXT("cap"), L.Capacity, (int64)8);
    TestEqual(TEXT("ids"), L.SessionIds.Num(), 2);

    const FServerInfo I = FGfModel::ParseGameServer(FGfJson::ParseObject(
        TEXT("{\"object_meta\":{\"name\":\"gs-1\",\"annotations\":{\"GAMEFLOW_PAYLOAD\":\"p\",\"GAMEFLOW_REGION\":\"eu\"}},")
        TEXT("\"status\":{\"state\":\"Ready\",\"address\":\"10.0.0.1\",\"ports\":[{\"name\":\"default\",\"port\":7777}],")
        TEXT("\"lists\":{\"players\":{\"capacity\":\"2\",\"values\":[\"a\"]}}}}")));
    TestEqual(TEXT("name"), I.Name, FString(TEXT("gs-1")));
    TestEqual(TEXT("state"), I.State, FString(TEXT("Ready")));
    TestEqual(TEXT("region"), I.Region, FString(TEXT("eu")));
    TestEqual(TEXT("port"), I.Ports.Num() ? I.Ports[0].Port : -1, 7777);
    TestTrue(TEXT("players tracked"), I.Players.bTrackingEnabled);
    FString Payload; TestTrue(TEXT("payload present"), FGfModel::PayloadOf(I, Payload));
    TestEqual(TEXT("payload value"), Payload, FString(TEXT("p")));
    return true;
}

// ---------------------------------------------------------------------------
// camelCase tolerance: objectMeta (local/fixtures) instead of object_meta
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfModelCamelCaseTest, "GameFlow.Model.CamelCaseMetaTolerance",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfModelCamelCaseTest::RunTest(const FString& P) {
    const FServerInfo I = FGfModel::ParseGameServer(FGfJson::ParseObject(
        TEXT("{\"objectMeta\":{\"name\":\"gs-camel\",\"annotations\":{\"GAMEFLOW_REGION\":\"us\"}},")
        TEXT("\"status\":{\"state\":\"Ready\",\"address\":\"10.0.0.2\",\"ports\":[]}}")));
    TestEqual(TEXT("name from objectMeta"), I.Name, FString(TEXT("gs-camel")));
    TestEqual(TEXT("region from objectMeta annotation"), I.Region, FString(TEXT("us")));
    return true;
}

// ---------------------------------------------------------------------------
// Port as string: "port":"7777" (string coercion via AsInt64)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfModelPortStringTest, "GameFlow.Model.PortAsString",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfModelPortStringTest::RunTest(const FString& P) {
    const FServerInfo I = FGfModel::ParseGameServer(FGfJson::ParseObject(
        TEXT("{\"object_meta\":{\"name\":\"gs-2\",\"annotations\":{}},")
        TEXT("\"status\":{\"state\":\"Ready\",\"address\":\"10.0.0.3\",\"ports\":[{\"name\":\"game\",\"port\":\"7777\"}]}}")));
    TestEqual(TEXT("port count"), I.Ports.Num(), 1);
    TestEqual(TEXT("port value from string"), I.Ports.Num() ? I.Ports[0].Port : -1, 7777);
    return true;
}

// ---------------------------------------------------------------------------
// Players absent: status.lists.players missing → untracked default
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfModelPlayersAbsentTest, "GameFlow.Model.PlayersAbsentDefault",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfModelPlayersAbsentTest::RunTest(const FString& P) {
    const FServerInfo I = FGfModel::ParseGameServer(FGfJson::ParseObject(
        TEXT("{\"object_meta\":{\"name\":\"gs-3\",\"annotations\":{}},")
        TEXT("\"status\":{\"state\":\"Ready\",\"address\":\"10.0.0.4\",\"ports\":[]}}")));
    TestFalse(TEXT("not tracking"), I.Players.bTrackingEnabled);
    TestEqual(TEXT("capacity zero"), I.Players.Capacity, (int64)0);
    TestEqual(TEXT("no session ids"), I.Players.SessionIds.Num(), 0);
    return true;
}

// ---------------------------------------------------------------------------
// BuildId: parses GAMEFLOW_BUILD_ID annotation
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfModelBuildIdTest, "GameFlow.Model.BuildId",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FGfModelBuildIdTest::RunTest(const FString& P) {
    const FServerInfo I = FGfModel::ParseGameServer(FGfJson::ParseObject(
        TEXT("{\"object_meta\":{\"name\":\"gs-4\",\"annotations\":{\"GAMEFLOW_BUILD_ID\":\"build-42\",\"GAMEFLOW_REGION\":\"ap\"}},")
        TEXT("\"status\":{\"state\":\"Allocated\",\"address\":\"10.0.0.5\",\"ports\":[]}}")));
    TestEqual(TEXT("build id"), I.BuildId, FString(TEXT("build-42")));
    TestEqual(TEXT("region alongside build id"), I.Region, FString(TEXT("ap")));
    return true;
}

#endif
