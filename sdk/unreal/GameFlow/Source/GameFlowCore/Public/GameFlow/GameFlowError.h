#pragma once

#include "CoreMinimal.h"
#include "GameFlowError.generated.h"

UENUM(BlueprintType)
enum class EGameFlowErrorCode : uint8
{
    None                    UMETA(DisplayName = "None"),
    SidecarUnavailable      UMETA(DisplayName = "Sidecar Unavailable"),
    PlayerAlreadyConnected  UMETA(DisplayName = "Player Already Connected"),
    ServerFull              UMETA(DisplayName = "Server Full"),
    PlayerTrackingDisabled  UMETA(DisplayName = "Player Tracking Disabled"),
    NotConnected            UMETA(DisplayName = "Not Connected"),
    RequestFailed           UMETA(DisplayName = "Request Failed"),
};

USTRUCT(BlueprintType)
struct GAMEFLOWCORE_API FGameFlowError
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "GameFlow")
    EGameFlowErrorCode Code = EGameFlowErrorCode::None;

    UPROPERTY(BlueprintReadOnly, Category = "GameFlow")
    FString Message;

    UPROPERTY(BlueprintReadOnly, Category = "GameFlow")
    int64 Capacity = 0;

    /** Returns true when Code is None (no error). */
    bool IsOk() const { return Code == EGameFlowErrorCode::None; }

    /** Returns the proto token string for Code, or empty string when Ok. */
    FString CodeString() const;

    /** Construct an error result. */
    static FGameFlowError Make(EGameFlowErrorCode C, const FString& M, int64 Cap = 0);

    /** Construct a success (no-error) result. */
    static FGameFlowError Ok();
};
