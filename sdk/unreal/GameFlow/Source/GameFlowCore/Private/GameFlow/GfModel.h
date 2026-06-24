#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "GameFlow/GameFlowTypes.h"

/** Internal wire→model mapper. Not public API. */
struct FGfModel
{
    /** Annotation key that carries the opaque server payload. */
    static constexpr const TCHAR* PayloadAnnotation = TEXT("GAMEFLOW_PAYLOAD");

    /**
     * Parse a GET /v1beta1/lists/players response object into FPlayerList.
     * On a null/invalid Root, returns a default-constructed (untracked) FPlayerList.
     */
    static FPlayerList ParseList(const TSharedPtr<FJsonObject>& Root);

    /**
     * Parse a GET /gameserver response object into FServerInfo.
     * Tolerates both object_meta (proto/runtime) and objectMeta (local/fixtures).
     * Missing keys yield defaults; never crashes.
     */
    static FServerInfo ParseGameServer(const TSharedPtr<FJsonObject>& Root);

    /**
     * Extract the GAMEFLOW_PAYLOAD annotation value from a server's annotations.
     * Returns true and sets Out when the annotation is present; returns false otherwise.
     */
    static bool PayloadOf(const FServerInfo& Info, FString& Out);
};
