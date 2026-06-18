using System;
using System.Threading;
using System.Threading.Tasks;

namespace GameFlow
{
    /// <summary>
    /// Automatic health heartbeat. Pings at a fixed cadence (clamped to ≥ 500 ms), scheduling the next
    /// ping only after the previous one settles. Failures warn and keep the cadence (no backoff); after
    /// six consecutive failures it reports degraded health exactly once, recovering silently on success.
    /// Never throws out of the loop and never keeps the host process alive beyond <see cref="StopAsync"/>.
    /// </summary>
    internal sealed class HealthLoop
    {
        private const int DegradedThreshold = 6;

        private readonly Func<CancellationToken, Task> _ping;
        private readonly Func<int, CancellationToken, Task> _delay;
        private readonly int _intervalMs;
        private readonly Action _onDegraded;
        private readonly IGameFlowLogger _log;
        private CancellationTokenSource _cts;
        private Task _loop;

        internal HealthLoop(Func<CancellationToken, Task> ping, Func<int, CancellationToken, Task> delay,
            int intervalMs, Action onDegraded, IGameFlowLogger log)
        {
            _ping = ping;
            _delay = delay;
            _intervalMs = Math.Max(500, intervalMs);
            _onDegraded = onDegraded;
            _log = log;
        }

        internal void Start()
        {
            _cts = new CancellationTokenSource();
            var ct = _cts.Token;
            _loop = Task.Run(() => RunLoop(ct));
        }

        private async Task RunLoop(CancellationToken ct)
        {
            int consecutiveFailures = 0;
            bool degradedReported = false;

            while (!ct.IsCancellationRequested)
            {
                try
                {
                    await _ping(ct).ConfigureAwait(false);
                    consecutiveFailures = 0;
                    degradedReported = false;
                }
                catch (OperationCanceledException) when (ct.IsCancellationRequested) { break; }
                catch (Exception e)
                {
                    consecutiveFailures++;
                    _log.Warn($"health ping failed ({consecutiveFailures}): {e.Message}");
                    if (consecutiveFailures >= DegradedThreshold && !degradedReported)
                    {
                        degradedReported = true;
                        _log.Error("health degraded: 6 consecutive failed pings");
                        try { _onDegraded?.Invoke(); } catch (Exception cb) { _log.Warn("health degraded callback threw: " + cb.Message); }
                    }
                }

                if (ct.IsCancellationRequested) break;
                try { await _delay(_intervalMs, ct).ConfigureAwait(false); }
                catch (OperationCanceledException) { break; }
            }
        }

        internal async Task StopAsync()
        {
            _cts?.Cancel();
            if (_loop != null)
            {
                try { await _loop.ConfigureAwait(false); } catch { }
            }
        }
    }
}
