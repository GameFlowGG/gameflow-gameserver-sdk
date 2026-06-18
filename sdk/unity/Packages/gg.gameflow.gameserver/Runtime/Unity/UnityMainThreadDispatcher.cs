using System;

namespace GameFlow.Unity
{
    /// <summary>
    /// Delivers SDK callbacks (watch, payload change, health-degraded) on Unity's main thread.
    /// <see cref="GameFlowRunner"/> drains it every frame. Pass <see cref="GameFlowRunner.Dispatcher"/>
    /// to <c>GameFlowOptions.Dispatcher</c> so callbacks are safe to touch Unity APIs.
    /// </summary>
    public sealed class UnityMainThreadDispatcher : IMainThreadDispatcher
    {
        private readonly MainThreadQueue _queue = new MainThreadQueue();

        public void Post(Action action) => _queue.Post(action);

        internal void Pump() => _queue.Pump();
    }
}
