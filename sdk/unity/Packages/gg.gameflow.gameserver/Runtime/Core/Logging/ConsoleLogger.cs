using System;

namespace GameFlow
{
    /// <summary>
    /// Default engine-agnostic logger: lifecycle/recoverable lines to stdout, errors to stderr,
    /// each prefixed <c>[gameflow]</c>. Debug is off by default. Unity builds inject a Debug.Log logger instead.
    /// </summary>
    public sealed class ConsoleLogger : IGameFlowLogger
    {
        private readonly bool _debug;

        public ConsoleLogger(bool debug = false) => _debug = debug;

        public void Info(string message) => Console.WriteLine("[gameflow] " + message);
        public void Warn(string message) => Console.WriteLine("[gameflow] WARN " + message);
        public void Error(string message) => Console.Error.WriteLine("[gameflow] ERROR " + message);
        public void Debug(string message)
        {
            if (_debug) Console.WriteLine("[gameflow] DEBUG " + message);
        }
    }
}
