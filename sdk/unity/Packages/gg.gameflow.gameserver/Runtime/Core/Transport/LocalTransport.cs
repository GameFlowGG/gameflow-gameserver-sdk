using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;

namespace GameFlow
{
    /// <summary>
    /// In-memory stand-in for the platform runtime, used off-platform. Lifecycle calls are no-ops;
    /// player tracking runs against a local list whose capacity comes from GAMEFLOW_MAX_PLAYERS
    /// (unset = unlimited, 0 = tracking disabled). The launch payload comes from GAMEFLOW_PAYLOAD.
    /// Watch emits a synthetic server object on every mutation.
    /// </summary>
    internal sealed class LocalTransport : ITransport
    {
        private readonly IGameFlowLogger _log;
        private readonly object _lock = new object();
        private readonly List<string> _players = new List<string>();
        private readonly bool _trackingEnabled;
        private readonly long _capacity;
        private readonly string _payload;
        private event Action<ServerInfo> _watchers;

        internal LocalTransport(EnvReader env, IGameFlowLogger log)
        {
            _log = log;
            _payload = env.Get("GAMEFLOW_PAYLOAD");
            var max = env.Get("GAMEFLOW_MAX_PLAYERS");
            if (max == null)
            {
                _trackingEnabled = true;
                _capacity = long.MaxValue;
            }
            else if (long.TryParse(max, out var n) && n == 0)
            {
                _trackingEnabled = false;
                _capacity = 0;
            }
            else
            {
                _trackingEnabled = true;
                _capacity = long.TryParse(max, out var c) ? c : long.MaxValue;
            }
        }

        public Task<ServerInfo> Probe(CancellationToken ct) => Task.FromResult(Snapshot());
        public Task<ServerInfo> GetServerInfo(CancellationToken ct) => Task.FromResult(Snapshot());

        public Task Ready(CancellationToken ct) { _log.Debug("local mode: ready() is a no-op"); return Task.CompletedTask; }
        public Task Health(CancellationToken ct) => Task.CompletedTask;
        public Task Shutdown(CancellationToken ct) { _log.Debug("local mode: shutdown() is a no-op"); return Task.CompletedTask; }

        public Task<PlayerList> AddPlayer(string sessionId, long cachedCapacity, CancellationToken ct)
        {
            lock (_lock)
            {
                if (!_trackingEnabled) throw new PlayerTrackingDisabledException("player tracking is disabled (max players = 0)");
                if (_players.Contains(sessionId)) throw new PlayerAlreadyConnectedException($"session {sessionId} already connected");
                if (_players.Count >= _capacity) throw new ServerFullException($"server full (capacity {_capacity})", _capacity);
                _players.Add(sessionId);
            }
            RaiseWatch();
            return Task.FromResult(CurrentList());
        }

        public Task<(bool found, PlayerList list)> RemovePlayer(string sessionId, CancellationToken ct)
        {
            bool found;
            lock (_lock)
            {
                if (!_trackingEnabled) throw new PlayerTrackingDisabledException("player tracking is disabled (max players = 0)");
                found = _players.Remove(sessionId);
            }
            if (found) RaiseWatch();
            return Task.FromResult((found, CurrentList()));
        }

        public async IAsyncEnumerable<ServerInfo> Watch([EnumeratorCancellation] CancellationToken ct)
        {
            var queue = new ConcurrentQueue<ServerInfo>();
            var signal = new SemaphoreSlim(0);
            void Handler(ServerInfo info) { queue.Enqueue(info); signal.Release(); }
            lock (_lock) _watchers += Handler;
            try
            {
                while (true)
                {
                    await signal.WaitAsync(ct).ConfigureAwait(false);
                    while (queue.TryDequeue(out var info)) yield return info;
                }
            }
            finally
            {
                lock (_lock) _watchers -= Handler;
                signal.Dispose();
            }
        }

        private void RaiseWatch() => _watchers?.Invoke(Snapshot());

        private PlayerList CurrentList()
        {
            lock (_lock)
            {
                return new PlayerList
                {
                    TrackingEnabled = _trackingEnabled,
                    Capacity = _capacity,
                    SessionIds = new List<string>(_players),
                };
            }
        }

        private ServerInfo Snapshot()
        {
            var annotations = new Dictionary<string, string>();
            if (_payload != null) annotations[Model.PayloadAnnotation] = _payload;
            return new ServerInfo
            {
                Name = "local",
                State = "Ready",
                Address = "127.0.0.1",
                Region = "",
                BuildId = "",
                Ports = Array.Empty<ServerPort>(),
                Labels = new Dictionary<string, string>(),
                Annotations = annotations,
                Players = CurrentList(),
            };
        }
    }
}
