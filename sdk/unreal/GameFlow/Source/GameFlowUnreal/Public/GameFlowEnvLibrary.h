#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameFlowEnvLibrary.generated.h"

/**
 * Blueprint function library for reading GameFlow environment variables from
 * the real process environment. All functions are pure and do not throw.
 */
UCLASS()
class GAMEFLOWUNREAL_API UGameFlowEnvLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Reads GAMEFLOW_DEFAULT_PORT. Returns false when absent. */
    UFUNCTION(BlueprintPure, Category = "GameFlow|Env")
    static bool DefaultPort(int32& OutPort);

    /**
     * Reads a named port variable. Name is normalized:
     *   trimmed, upper-cased, spaces→underscores → GAMEFLOW_<NAME>_PORT.
     * Returns false when absent.
     */
    UFUNCTION(BlueprintPure, Category = "GameFlow|Env")
    static bool Port(FString Name, int32& OutPort);

    /** Reads GAMEFLOW_TLS_DEFAULT_PORT. Returns false when absent. */
    UFUNCTION(BlueprintPure, Category = "GameFlow|Env")
    static bool TlsDefaultPort(int32& OutPort);

    /**
     * Reads a named TLS port variable → GAMEFLOW_TLS_<NAME>_PORT.
     * Returns false when absent.
     */
    UFUNCTION(BlueprintPure, Category = "GameFlow|Env")
    static bool TlsPort(FString Name, int32& OutPort);

    /** Returns GAMEFLOW_REGION, or empty string when absent. */
    UFUNCTION(BlueprintPure, Category = "GameFlow|Env")
    static FString Region();

    /** Returns GAMEFLOW_BUILD_ID, or empty string when absent. */
    UFUNCTION(BlueprintPure, Category = "GameFlow|Env")
    static FString BuildId();
};
