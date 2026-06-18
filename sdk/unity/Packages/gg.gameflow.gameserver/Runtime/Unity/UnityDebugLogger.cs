using UnityEngine;

namespace GameFlow.Unity
{
    /// <summary>Routes SDK logging to Unity's console with the <c>[gameflow]</c> prefix. Debug lines are off unless enabled.</summary>
    public sealed class UnityDebugLogger : IGameFlowLogger
    {
        private readonly bool _verbose;

        public UnityDebugLogger(bool verbose = false) => _verbose = verbose;

        public void Info(string message) => UnityEngine.Debug.Log("[gameflow] " + message);
        public void Warn(string message) => UnityEngine.Debug.LogWarning("[gameflow] " + message);
        public void Error(string message) => UnityEngine.Debug.LogError("[gameflow] " + message);

        public void Debug(string message)
        {
            if (_verbose) UnityEngine.Debug.Log("[gameflow] " + message);
        }
    }
}
