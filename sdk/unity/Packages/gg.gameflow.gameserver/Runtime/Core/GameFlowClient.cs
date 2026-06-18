using System;
using System.Threading;
using System.Threading.Tasks;

namespace GameFlow
{
    /// <summary>
    /// The GameFlow game-server SDK entry point. One instance per server process: connect with
    /// <see cref="Start"/>, mark ready with <see cref="Ready"/> (health reporting then runs automatically),
    /// track sessions through <see cref="Players"/>, and end the match with <see cref="Shutdown"/>.
    /// Off-platform the SDK runs in local mode automatically, so the same build runs everywhere.
    /// </summary>
    public sealed class GameFlowClient
    {
        private enum State { New, Connected, ShutDown }

        private readonly GameFlowOptions _options;
        private readonly IGameFlowLogger _log;
        private readonly EnvReader _env;
        private readonly object _lock = new object();

        private ITransport _transport;
        private GameFlowMode _mode;
        private Players _players;
        private Watcher _watcher;
        private HealthLoop _health;
        private CancellationTokenSource _lifetime;
        private string _seedPayload;
        private bool _trackingWarned;
        private State _state = State.New;

        public GameFlowClient(GameFlowOptions options = null)
        {
            _options = options ?? new GameFlowOptions();
            _log = _options.Logger ?? new ConsoleLogger();
            _env = new EnvReader(_options.EnvProvider);
        }

        /// <summary>The resolved mode after <see cref="Start"/>.</summary>
        public GameFlowMode Mode => _mode;

        /// <summary>Player tracking. Throws <see cref="NotConnectedException"/> before <see cref="Start"/>.</summary>
        public Players Players => _players ?? throw new NotConnectedException("call Start() before using the SDK");

        public async Task Start(CancellationToken ct = default)
        {
            lock (_lock)
            {
                if (_state != State.New) throw new NotConnectedException("Start() may only be called once");
            }

            _mode = ModeDetection.Resolve(_options, _env, _log);
            ServerInfo info;
            if (_mode == GameFlowMode.Local)
            {
                _transport = new LocalTransport(_env, _log);
                info = await _transport.Probe(ct).ConfigureAwait(false);
            }
            else
            {
                int port = _options.Port ?? _env.PortVar(ModeDetection.TransportPortEnv) ?? 9358;
                _transport = new SidecarTransport($"http://127.0.0.1:{port}", _options.RequestTimeoutMs, _log);
                info = await ConnectWithRetry(ct).ConfigureAwait(false);
            }

            _players = new Players(_transport, EnsureConnected);
            _players.SetCache(info.Players);
            _seedPayload = Model.PayloadOf(info);
            WarnIfTrackingDisabled(info);

            _watcher = new Watcher(_transport, _log);
            _lifetime = new CancellationTokenSource();
            lock (_lock) _state = State.Connected;
            _log.Info($"connected ({_mode.ToString().ToLowerInvariant()} mode)");
        }

        public async Task Ready()
        {
            EnsureConnected();
            await _transport.Ready(CancellationToken.None).ConfigureAwait(false);
            _log.Info("server ready");
            if (_mode == GameFlowMode.Sidecar)
            {
                _health = new HealthLoop(
                    ping: c => _transport.Health(c),
                    delay: (ms, c) => Task.Delay(ms, c),
                    intervalMs: _options.HealthIntervalMs,
                    onDegraded: () => Marshal(() => _options.OnHealthDegraded?.Invoke()),
                    log: _log);
                _health.Start();
            }
        }

        public async Task<string> Payload()
        {
            EnsureConnected();
            var info = await _transport.GetServerInfo(CancellationToken.None).ConfigureAwait(false);
            return Model.PayloadOf(info);
        }

        public Task<ServerInfo> Info()
        {
            EnsureConnected();
            return _transport.GetServerInfo(CancellationToken.None);
        }

        public IDisposable Watch(Action<ServerInfo> listener)
        {
            EnsureConnected();
            return _watcher.Subscribe(info => Marshal(() => listener(info)));
        }

        public IDisposable OnPayloadChange(Action<string> listener)
        {
            EnsureConnected();
            string lastSeen = _seedPayload;
            return _watcher.Subscribe(info =>
            {
                var payload = Model.PayloadOf(info);
                if (!string.Equals(payload, lastSeen, StringComparison.Ordinal))
                {
                    lastSeen = payload;
                    Marshal(() => listener(payload));
                }
            });
        }

        public async Task Shutdown()
        {
            lock (_lock)
            {
                if (_state == State.ShutDown) return;
                if (_state != State.Connected) throw new NotConnectedException("not connected");
                _state = State.ShutDown;
            }

            if (_health != null) await _health.StopAsync().ConfigureAwait(false);
            _watcher?.Dispose();
            _lifetime?.Cancel();

            try { await _transport.Shutdown(CancellationToken.None).ConfigureAwait(false); }
            catch (Exception e) { _log.Warn("shutdown request failed: " + e.Message); }

            _log.Info("server shut down");
        }

        private async Task<ServerInfo> ConnectWithRetry(CancellationToken ct)
        {
            var backoff = new Backoff(new Random());
            var deadline = DateTime.UtcNow.AddMilliseconds(_options.ConnectTimeoutMs);
            Exception last = null;
            while (DateTime.UtcNow < deadline)
            {
                try { return await _transport.Probe(ct).ConfigureAwait(false); }
                catch (OperationCanceledException) { throw; }
                catch (Exception e)
                {
                    last = e;
                    int delay = backoff.NextDelayMs();
                    if (DateTime.UtcNow.AddMilliseconds(delay) >= deadline) break;
                    try { await Task.Delay(delay, ct).ConfigureAwait(false); }
                    catch (OperationCanceledException) { throw; }
                }
            }
            throw new SidecarUnavailableException("could not reach the GameFlow runtime within the connect timeout", last);
        }

        private void WarnIfTrackingDisabled(ServerInfo info)
        {
            if (_mode == GameFlowMode.Sidecar && info.Players != null && !info.Players.TrackingEnabled && !_trackingWarned)
            {
                _trackingWarned = true;
                _log.Warn("player tracking is disabled (game configured with max players = 0); the platform cannot see player counts and idle servers may be shut down");
            }
        }

        private void Marshal(Action action)
        {
            if (_options.Dispatcher != null) _options.Dispatcher.Post(action);
            else action();
        }

        private void EnsureConnected()
        {
            lock (_lock)
            {
                if (_state != State.Connected) throw new NotConnectedException("not connected");
            }
        }
    }
}
