#pragma once

#include "CoreMinimal.h"
#include "GameFlow/GameFlowError.h"
#include "GameFlow/GameFlowTypes.h"

// ---------------------------------------------------------------------------
// Result delegates — single-cast, synchronously invoked on the game thread
// ---------------------------------------------------------------------------

/** Callback for operations that return a server info snapshot or an error. */
DECLARE_DELEGATE_TwoParams(FGfInfoResult,   const FGameFlowError&, const FServerInfo&)

/** Callback for fire-and-forget operations that return only success/error. */
DECLARE_DELEGATE_OneParam( FGfVoidResult,   const FGameFlowError&)

/** Callback for operations that return the updated player list. */
DECLARE_DELEGATE_TwoParams(FGfListResult,   const FGameFlowError&, const FPlayerList&)

/** Callback for RemovePlayer — error, whether the player was found, updated list. */
DECLARE_DELEGATE_ThreeParams(FGfRemoveResult, const FGameFlowError&, bool /*bFound*/, const FPlayerList&)

/** Callback for GetPayload — error, the opaque payload, whether it was present. */
DECLARE_DELEGATE_ThreeParams(FGfPayloadResult, const FGameFlowError&, const FString& /*payload*/, bool /*present*/)
