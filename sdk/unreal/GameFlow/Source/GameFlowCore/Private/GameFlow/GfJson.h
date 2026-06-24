#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/** Thin internal helper over UE's Json module. Not public API. */
struct FGfJson
{
    /** Deserialize a JSON object from text; returns nullptr on parse failure. */
    static TSharedPtr<FJsonObject> ParseObject(const FString& Text);

    /**
     * Coerce a JSON value to int64.
     * Handles string ("8" → 8) and number (42 → 42); returns 0 for null/invalid.
     */
    static int64 AsInt64(const TSharedPtr<FJsonValue>& V);

    /** Return "{}" — an empty JSON object body. */
    static FString ObjEmpty();

    /**
     * Return {"value":"<SessionId>"} with correct JSON escaping
     * (e.g. a"b → {"value":"a\"b"}).
     */
    static FString ObjValue(const FString& SessionId);
};
