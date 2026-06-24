#pragma once

#include "CoreMinimal.h"
#include "GameFlow/GameFlowLogger.h"

// Forward-declare the transport factory defined in Task 9.
class IGfTransportFactory;

#include "GameFlowOptions.generated.h"

/** Operating mode for the GameFlow SDK. */
UENUM(BlueprintType)
enum class EGameFlowMode : uint8
{
    /** Connect to an Agones sidecar process running alongside the game server. */
    Sidecar UMETA(DisplayName = "Sidecar"),
    /** Run locally without a sidecar, using stub/no-op transport. */
    Local   UMETA(DisplayName = "Local"),
};

/**
 * Marshals SDK callbacks back onto the game thread.
 *
 * Implement this interface if you need custom thread marshalling; a default
 * implementation that uses AsyncTask(ENamedThreads::GameThread, ...) will
 * be provided by the client module.
 */
class GAMEFLOWCORE_API IGameFlowDispatcher
{
public:
    virtual ~IGameFlowDispatcher() = default;
    virtual void Post(TFunction<void()> Fn) = 0;
};

/**
 * Inline dispatcher — runs the action immediately on the calling thread.
 *
 * Engine-free, so it lives in GameFlowCore. Useful in unit tests and any
 * context where the caller is already on the correct thread.
 */
class GAMEFLOWCORE_API FInlineDispatcher : public IGameFlowDispatcher
{
public:
    virtual void Post(TFunction<void()> Fn) override { Fn(); }
};

/**
 * Configuration struct passed to the GameFlow client at startup.
 *
 * Not a USTRUCT — holds TFunction<>/TSharedPtr<>/TOptional<> members that are
 * not Blueprint-exposable. Use from C++ only.
 */
struct GAMEFLOWCORE_API FGameFlowOptions
{
    /** When set, overrides environment-variable mode detection. */
    TOptional<EGameFlowMode> Mode;

    /** Timeout in milliseconds for the initial connection. Default 30 000 ms. */
    int32 ConnectTimeoutMs = 30000;

    /** Timeout in milliseconds for individual sidecar HTTP requests. Default 3 000 ms. */
    int32 RequestTimeoutMs = 3000;

    /** Interval in milliseconds between health-ping calls. Default 5 000 ms. */
    int32 HealthIntervalMs = 5000;

    /** When set, overrides the sidecar port (AGONES_SDK_HTTP_PORT) used by the transport. */
    TOptional<int32> Port;

    /** Called when repeated health pings fail and the connection is considered degraded. */
    TFunction<void()> OnHealthDegraded;

    /**
     * Custom environment-variable provider injected into FGameFlowEnvReader.
     * Signature matches FGameFlowEnvReader::FProvider: returns true + fills Out when set.
     * When null the real process environment is used.
     */
    TFunction<bool(const FString&, FString&)> EnvProvider;

    /** Custom logger; uses FGameFlowLog (UE_LOG) when null. */
    TSharedPtr<IGameFlowLogger> Logger;

    /** Custom thread dispatcher; defaults to game-thread AsyncTask when null. */
    TSharedPtr<IGameFlowDispatcher> Dispatcher;

    /** Custom transport factory; auto-selected from Mode when null. */
    TSharedPtr<IGfTransportFactory> TransportFactory;
};
