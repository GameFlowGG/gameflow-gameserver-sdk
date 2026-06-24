#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "GameFlow/GfChunkedNdjson.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGfChunkTest, "GameFlow.Watch.ChunkedNdjsonFraming",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// Feed an ASCII string's UTF-8 bytes into the decoder.
static void FeedStr(FGfChunkedNdjson& D, const FString& S, TArray<FString>& Out)
{
    const FTCHARToUTF8 U(*S);
    D.Feed(reinterpret_cast<const uint8*>(U.Get()), U.Length(), Out);
}

bool FGfChunkTest::RunTest(const FString& P)
{
    FGfChunkedNdjson D;
    TArray<FString> Lines;

    // Two HTTP chunks. The first JSON line `{"result":{"x":1}}\n` (19 bytes) is
    // deliberately SPLIT across the chunk boundary to prove the decoder buffers
    // a line across chunks:
    //   chunk 1 = `{"result":{"x":1`            (16 bytes -> hex 0x10)
    //   chunk 2 = `}}\n{"error":"z"}\n`         (17 bytes -> hex 0x11)
    // followed by the terminating `0\r\n\r\n`.
    FeedStr(D, TEXT("10\r\n{\"result\":{\"x\":1"), Lines);          // partial chunk 1 size+data
    TestEqual(TEXT("no line yet (split mid-line)"), Lines.Num(), 0);

    FeedStr(D, TEXT("\r\n11\r\n}}\n{\"error\":\"z\"}\n\r\n"), Lines); // rest of c1, then c2
    FeedStr(D, TEXT("0\r\n\r\n"), Lines);                            // body terminator

    TestEqual(TEXT("two lines"), Lines.Num(), 2);
    if (Lines.Num() == 2)
    {
        TestEqual(TEXT("first line"), Lines[0], TEXT("{\"result\":{\"x\":1}}"));
        TestEqual(TEXT("second line"), Lines[1], TEXT("{\"error\":\"z\"}"));
        TestTrue(TEXT("first is result"), Lines[0].Contains(TEXT("\"result\"")));
        TestTrue(TEXT("second is error"), Lines[1].Contains(TEXT("\"error\"")));
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
