#pragma once

#include "CoreMinimal.h"
#include "GameFlowTypes.generated.h"

/** A single named port exposed by the game server. */
USTRUCT(BlueprintType)
struct GAMEFLOWCORE_API FServerPort
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "GameFlow")
    FString Name;

    UPROPERTY(BlueprintReadOnly, Category = "GameFlow")
    int32 Port = 0;
};

/** Player-list tracking state from the sidecar's lists.players block. */
USTRUCT(BlueprintType)
struct GAMEFLOWCORE_API FPlayerList
{
    GENERATED_BODY()

    /** True when the runtime's lists.players block was present in the response. */
    UPROPERTY(BlueprintReadOnly, Category = "GameFlow")
    bool bTrackingEnabled = false;

    UPROPERTY(BlueprintReadOnly, Category = "GameFlow")
    int64 Capacity = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GameFlow")
    TArray<FString> SessionIds;
};

/** Full snapshot of a game server returned by GET /gameserver. */
USTRUCT(BlueprintType)
struct GAMEFLOWCORE_API FServerInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "GameFlow")
    FString Name;

    UPROPERTY(BlueprintReadOnly, Category = "GameFlow")
    FString State;

    UPROPERTY(BlueprintReadOnly, Category = "GameFlow")
    FString Address;

    UPROPERTY(BlueprintReadOnly, Category = "GameFlow")
    FString Region;

    UPROPERTY(BlueprintReadOnly, Category = "GameFlow")
    FString BuildId;

    UPROPERTY(BlueprintReadOnly, Category = "GameFlow")
    TArray<FServerPort> Ports;

    UPROPERTY(BlueprintReadOnly, Category = "GameFlow")
    FPlayerList Players;

    /** All annotations from object_meta.annotations. */
    UPROPERTY(BlueprintReadOnly, Category = "GameFlow")
    TMap<FString, FString> Annotations;

    /** All labels from object_meta.labels. */
    UPROPERTY(BlueprintReadOnly, Category = "GameFlow")
    TMap<FString, FString> Labels;
};
