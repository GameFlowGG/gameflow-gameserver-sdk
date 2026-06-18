using System;
using System.Collections.Concurrent;

namespace GameFlow
{
    /// <summary>
    /// Thread-safe queue of actions to run on the engine's main thread. Producers call <see cref="Post"/>
    /// from any thread; the host drains it with <see cref="Pump"/> once per frame. Pure C# so it is testable
    /// without Unity; the Unity layer wraps it in an <see cref="IMainThreadDispatcher"/>.
    /// </summary>
    public sealed class MainThreadQueue
    {
        private readonly ConcurrentQueue<Action> _queue = new ConcurrentQueue<Action>();

        public void Post(Action action)
        {
            if (action != null) _queue.Enqueue(action);
        }

        /// <summary>Runs and removes every currently-queued action, in order. A throwing action is skipped.</summary>
        public void Pump()
        {
            while (_queue.TryDequeue(out var action))
            {
                try { action(); }
                catch { /* a bad callback must not stall the pump */ }
            }
        }
    }
}
