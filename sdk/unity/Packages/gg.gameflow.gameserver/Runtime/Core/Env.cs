using System;

namespace GameFlow
{
    /// <summary>
    /// Reads environment variables through an injectable provider so tests can supply values
    /// without mutating the process environment. Port helpers return null rather than throwing.
    /// </summary>
    internal sealed class EnvReader
    {
        private readonly Func<string, string> _provider;

        internal EnvReader(Func<string, string> provider) => _provider = provider ?? Environment.GetEnvironmentVariable;

        internal string Get(string name)
        {
            var v = _provider(name);
            return string.IsNullOrEmpty(v) ? null : v;
        }

        internal int? PortVar(string name)
        {
            var v = Get(name);
            return v != null && int.TryParse(v, out var n) ? n : (int?)null;
        }
    }

    /// <summary>
    /// Platform-provided port/region/build helpers over the real process environment.
    /// Every accessor returns an absent value (null) rather than throwing.
    /// </summary>
    public static class GameFlowEnv
    {
        private static readonly EnvReader Env = new EnvReader(Environment.GetEnvironmentVariable);

        public static int? DefaultPort => Env.PortVar("GAMEFLOW_DEFAULT_PORT");
        public static int? Port(string name) => Env.PortVar("GAMEFLOW_" + Normalize(name) + "_PORT");
        public static int? TlsDefaultPort => Env.PortVar("GAMEFLOW_TLS_DEFAULT_PORT");
        public static int? TlsPort(string name) => Env.PortVar("GAMEFLOW_TLS_" + Normalize(name) + "_PORT");
        public static string Region => Env.Get("GAMEFLOW_REGION");
        public static string BuildId => Env.Get("GAMEFLOW_BUILD_ID");

        private static string Normalize(string name) => name.Trim().ToUpperInvariant().Replace(' ', '_');
    }
}
