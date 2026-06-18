using System;

namespace GameFlow
{
    /// <summary>
    /// Exponential backoff for connect and watch reconnect: 250 ms base, doubling to a 4000 ms cap,
    /// with ±20% jitter on each delay. <see cref="Reset"/> returns to the base (called after a received message).
    /// </summary>
    internal sealed class Backoff
    {
        private readonly Random _rng;
        private readonly int _baseMs;
        private readonly int _capMs;
        private int _current;

        internal Backoff(Random rng, int baseMs = 250, int capMs = 4000)
        {
            _rng = rng;
            _baseMs = baseMs;
            _capMs = capMs;
            _current = baseMs;
        }

        internal int NextDelayMs()
        {
            int raw = Math.Min(_current, _capMs);
            double jitter = 1.0 + (_rng.NextDouble() * 0.4 - 0.2); // ±20%
            int delay = (int)(raw * jitter);
            _current = Math.Min(_current * 2, _capMs);
            return delay;
        }

        internal void Reset() => _current = _baseMs;
    }
}
