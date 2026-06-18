using System;

namespace GameFlow
{
    /// <summary>
    /// Resolves the effective mode: explicit option &gt; GAMEFLOW_SDK_MODE &gt; auto-detect.
    /// Auto uses the presence of the platform transport port to choose sidecar; otherwise local.
    /// </summary>
    internal static class ModeDetection
    {
        internal const string TransportPortEnv = "AGONES_SDK_HTTP_PORT";

        internal static GameFlowMode Resolve(GameFlowOptions options, EnvReader env, IGameFlowLogger log)
        {
            if (options.Mode.HasValue) return options.Mode.Value;

            var explicitMode = env.Get("GAMEFLOW_SDK_MODE");
            if (explicitMode != null)
            {
                if (explicitMode.Equals("sidecar", StringComparison.OrdinalIgnoreCase)) return GameFlowMode.Sidecar;
                if (explicitMode.Equals("local", StringComparison.OrdinalIgnoreCase)) return GameFlowMode.Local;
            }

            if (env.Get(TransportPortEnv) != null) return GameFlowMode.Sidecar;

            log.Info("no platform runtime detected; running in local mode");
            return GameFlowMode.Local;
        }
    }
}
