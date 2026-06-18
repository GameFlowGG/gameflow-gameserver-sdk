using System;
using UnityEngine;

namespace GameFlow.Unity
{
    /// <summary>
    /// Optional Unity glue around a <see cref="GameFlowClient"/>: pumps the main-thread dispatcher every
    /// frame so SDK callbacks land on the main thread, and sends a clean shutdown when the application quits.
    ///
    /// <code>
    /// var runner = GameFlowRunner.Create();
    /// var gf = new GameFlowClient(new GameFlowOptions {
    ///     Logger = new UnityDebugLogger(),
    ///     Dispatcher = runner.Dispatcher,
    /// });
    /// runner.Bind(gf);
    /// await gf.Start();
    /// await gf.Ready();
    /// </code>
    /// </summary>
    [DisallowMultipleComponent]
    public sealed class GameFlowRunner : MonoBehaviour
    {
        private readonly UnityMainThreadDispatcher _dispatcher = new UnityMainThreadDispatcher();
        private GameFlowClient _client;

        /// <summary>Pass this to <c>GameFlowOptions.Dispatcher</c> so callbacks run on the main thread.</summary>
        public IMainThreadDispatcher Dispatcher => _dispatcher;

        /// <summary>Creates a persistent GameObject hosting a runner. Call before constructing the client.</summary>
        public static GameFlowRunner Create(string name = "GameFlow")
        {
            var go = new GameObject(name);
            DontDestroyOnLoad(go);
            return go.AddComponent<GameFlowRunner>();
        }

        /// <summary>Associates the client so the runner can shut it down cleanly on quit.</summary>
        public void Bind(GameFlowClient client) => _client = client;

        private void Update() => _dispatcher.Pump();

        private void OnApplicationQuit()
        {
            if (_client == null) return;
            try { _client.Shutdown().Wait(3000); }
            catch (Exception e) { UnityEngine.Debug.LogWarning("[gameflow] shutdown on quit failed: " + e.Message); }
        }
    }
}
