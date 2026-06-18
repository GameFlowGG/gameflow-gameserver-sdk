using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace GameFlow
{
    /// <summary>
    /// One shared watch stream fanned out to every subscriber. Opened lazily on the first subscriber,
    /// closed when the last unsubscribes. Reconnects with backoff on stream end or failure, resetting
    /// the backoff after any received message.
    /// </summary>
    internal sealed class Watcher : IDisposable
    {
        private readonly ITransport _transport;
        private readonly IGameFlowLogger _log;
        private readonly object _lock = new object();
        private readonly List<Action<ServerInfo>> _subscribers = new List<Action<ServerInfo>>();
        private CancellationTokenSource _cts;
        private Task _loop;
        private ServerInfo _last;

        internal Watcher(ITransport transport, IGameFlowLogger log)
        {
            _transport = transport;
            _log = log;
        }

        internal ServerInfo Last { get { lock (_lock) return _last; } }

        internal IDisposable Subscribe(Action<ServerInfo> onInfo)
        {
            lock (_lock)
            {
                _subscribers.Add(onInfo);
                if (_subscribers.Count == 1)
                {
                    // Chain behind any still-finishing prior loop so two RunLoops never open the
                    // stream at once on a rapid unsubscribe→subscribe.
                    var previous = _loop;
                    _cts = new CancellationTokenSource();
                    var token = _cts.Token;
                    _loop = Task.Run(async () =>
                    {
                        if (previous != null)
                        {
                            try { await previous.ConfigureAwait(false); } catch { }
                        }
                        await RunLoop(token).ConfigureAwait(false);
                    });
                }
            }
            return new Subscription(this, onInfo);
        }

        private void Unsubscribe(Action<ServerInfo> onInfo)
        {
            lock (_lock)
            {
                _subscribers.Remove(onInfo);
                if (_subscribers.Count == 0) StopLocked();
            }
        }

        private void StopLocked()
        {
            _cts?.Cancel();
            _cts = null;
        }

        private async Task RunLoop(CancellationToken ct)
        {
            var backoff = new Backoff(new Random());
            while (!ct.IsCancellationRequested)
            {
                try
                {
                    await foreach (var info in _transport.Watch(ct).ConfigureAwait(false))
                    {
                        backoff.Reset();
                        Deliver(info);
                    }
                }
                catch (OperationCanceledException) { break; }
                catch (Exception e)
                {
                    if (ct.IsCancellationRequested) break;
                    _log.Warn("watch stream error: " + e.Message);
                }

                if (ct.IsCancellationRequested) break;
                try { await Task.Delay(backoff.NextDelayMs(), ct).ConfigureAwait(false); }
                catch (OperationCanceledException) { break; }
            }
        }

        private void Deliver(ServerInfo info)
        {
            Action<ServerInfo>[] snapshot;
            lock (_lock)
            {
                _last = info;
                snapshot = _subscribers.ToArray();
            }
            foreach (var s in snapshot)
            {
                try { s(info); }
                catch (Exception e) { _log.Warn("watch subscriber threw: " + e.Message); }
            }
        }

        public void Dispose()
        {
            lock (_lock)
            {
                _subscribers.Clear();
                StopLocked();
            }
        }

        private sealed class Subscription : IDisposable
        {
            private readonly Watcher _watcher;
            private readonly Action<ServerInfo> _onInfo;
            private int _disposed;

            internal Subscription(Watcher watcher, Action<ServerInfo> onInfo)
            {
                _watcher = watcher;
                _onInfo = onInfo;
            }

            public void Dispose()
            {
                if (Interlocked.Exchange(ref _disposed, 1) == 0) _watcher.Unsubscribe(_onInfo);
            }
        }
    }
}
