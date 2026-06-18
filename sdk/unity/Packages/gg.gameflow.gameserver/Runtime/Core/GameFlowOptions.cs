using System;

namespace GameFlow
{
    /// <summary>How the SDK talks to the world: against the platform runtime, or simulated locally.</summary>
    public enum GameFlowMode
    {
        Sidecar,
        Local,
    }

    /// <summary>
    /// Marshals callbacks onto the engine's main thread. The Unity layer supplies an implementation
    /// that drains queued actions in <c>Update()</c>; headless code may leave this null.
    /// </summary>
    public interface IMainThreadDispatcher
    {
        void Post(Action action);
    }

    /// <summary>Configuration for a <see cref="GameFlowClient"/>. All fields are optional; defaults match the cross-SDK spec.</summary>
    public sealed class GameFlowOptions
    {
        /// <summary>Force a mode. Null = auto-detect (env, then platform port presence).</summary>
        public GameFlowMode? Mode;

        /// <summary>Connect probe budget before failing with <see cref="SidecarUnavailableException"/>.</summary>
        public int ConnectTimeoutMs = 30000;

        /// <summary>Per-request timeout (the watch stream is exempt).</summary>
        public int RequestTimeoutMs = 3000;

        /// <summary>Health heartbeat cadence, clamped to at least 500 ms.</summary>
        public int HealthIntervalMs = 5000;

        /// <summary>Override the transport port (defaults to the platform-provided port, else 9358).</summary>
        public int? Port;

        /// <summary>Injectable logger. Defaults to a <see cref="ConsoleLogger"/>; use <see cref="NullLogger"/> to silence.</summary>
        public IGameFlowLogger Logger;

        /// <summary>When set, watch/payload callbacks are delivered through this dispatcher (main thread).</summary>
        public IMainThreadDispatcher Dispatcher;

        /// <summary>Fires once when health degrades (6 consecutive failed pings); recovery is silent.</summary>
        public Action OnHealthDegraded;

        /// <summary>Test seam: resolve environment variables. Defaults to <see cref="Environment.GetEnvironmentVariable"/>.</summary>
        public Func<string, string> EnvProvider;
    }
}
