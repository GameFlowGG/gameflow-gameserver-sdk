#pragma once

#include "GameFlow/GameFlowOptions.h"
#include "GameFlow/GameFlowEnv.h"
#include "GameFlow/GameFlowLogger.h"

/**
 * Internal helper that resolves the operating mode from options + environment.
 *
 * Resolution order:
 *   1. Options.Mode (explicit override)
 *   2. GAMEFLOW_SDK_MODE env var ("sidecar" or "local", case-insensitive)
 *   3. Presence of AGONES_SDK_HTTP_PORT → Sidecar
 *   4. Fallback → Local (logs an info message)
 */
class FGfModeDetection
{
public:
    static EGameFlowMode Resolve(
        const FGameFlowOptions& Options,
        const FGameFlowEnvReader& Env,
        IGameFlowLogger& Logger);

private:
    static const FString TransportPortEnv;
};
