#include "GameFlow/GfModeDetection.h"

const FString FGfModeDetection::TransportPortEnv = TEXT("AGONES_SDK_HTTP_PORT");

EGameFlowMode FGfModeDetection::Resolve(
    const FGameFlowOptions& Options,
    const FGameFlowEnvReader& Env,
    IGameFlowLogger& Logger)
{
    // 1. Explicit override wins.
    if (Options.Mode.IsSet())
    {
        return Options.Mode.GetValue();
    }

    // 2. GAMEFLOW_SDK_MODE env var.
    FString ModeStr;
    if (Env.Get(TEXT("GAMEFLOW_SDK_MODE"), ModeStr))
    {
        ModeStr.ToLowerInline();
        if (ModeStr == TEXT("sidecar"))
        {
            return EGameFlowMode::Sidecar;
        }
        if (ModeStr == TEXT("local"))
        {
            return EGameFlowMode::Local;
        }
        // Unknown value — warn and fall through to auto-detection.
        Logger.Warn(FString::Printf(TEXT("ignoring unrecognized GAMEFLOW_SDK_MODE='%s'"), *ModeStr));
    }

    // 3. Agones sidecar port present → sidecar.
    FString PortStr;
    if (Env.Get(TransportPortEnv, PortStr))
    {
        return EGameFlowMode::Sidecar;
    }

    // 4. Fallback.
    Logger.Info(TEXT("no platform runtime detected; running in local mode"));
    return EGameFlowMode::Local;
}
