using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace GameFlow
{
    /// <summary>
    /// Player session tracking. Mutations go to the runtime; the returned list becomes a local cache so
    /// <see cref="Count"/>, <see cref="List"/>, <see cref="Capacity"/>, and <see cref="TrackingEnabled"/> read synchronously.
    /// </summary>
    public sealed class Players
    {
        private readonly ITransport _transport;
        private readonly Action _ensureConnected;
        private readonly object _lock = new object();
        private PlayerList _cache;

        internal Players(ITransport transport, Action ensureConnected = null)
        {
            _transport = transport;
            _ensureConnected = ensureConnected;
        }

        /// <summary>Seed the cache from the connect probe.</summary>
        internal void SetCache(PlayerList list)
        {
            lock (_lock) _cache = list;
        }

        /// <summary>Registers a joining session. Throws <see cref="ServerFullException"/>,
        /// <see cref="PlayerAlreadyConnectedException"/>, or <see cref="PlayerTrackingDisabledException"/>.</summary>
        public async Task Connect(string sessionId)
        {
            _ensureConnected?.Invoke();
            long capacity;
            lock (_lock) capacity = _cache?.Capacity ?? 0;
            var list = await _transport.AddPlayer(sessionId, capacity, CancellationToken.None).ConfigureAwait(false);
            lock (_lock) _cache = list;
        }

        /// <summary>Unregisters a leaving session. Returns false when the session was not present (idempotent).</summary>
        public async Task<bool> Disconnect(string sessionId)
        {
            _ensureConnected?.Invoke();
            var (found, list) = await _transport.RemovePlayer(sessionId, CancellationToken.None).ConfigureAwait(false);
            lock (_lock) _cache = list;
            return found;
        }

        public int Count { get { lock (_lock) return _cache?.SessionIds.Count ?? 0; } }

        public IReadOnlyList<string> List { get { lock (_lock) return _cache?.SessionIds ?? Array.Empty<string>(); } }

        public long Capacity { get { lock (_lock) return _cache?.Capacity ?? 0; } }

        public bool TrackingEnabled { get { lock (_lock) return _cache?.TrackingEnabled ?? false; } }
    }
}
