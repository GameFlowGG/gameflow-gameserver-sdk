using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace GameFlow
{
    /// <summary>
    /// REST transport to the local platform runtime. Mutations never trust the runtime's echo; the SDK
    /// re-reads the players list after every change (real runtimes answer mutations inconsistently).
    /// </summary>
    internal sealed class SidecarTransport : ITransport
    {
        private const string ListPath = "/v1beta1/lists/players";
        private readonly string _baseUrl;
        private readonly int _requestTimeoutMs;
        private readonly IGameFlowLogger _log;
        private readonly HttpClient _http;

        internal SidecarTransport(string baseUrl, int requestTimeoutMs, IGameFlowLogger log, HttpClient client = null)
        {
            _baseUrl = baseUrl.TrimEnd('/');
            _requestTimeoutMs = requestTimeoutMs;
            _log = log;
            _http = client ?? new HttpClient { Timeout = Timeout.InfiniteTimeSpan };
        }

        public async Task<ServerInfo> Probe(CancellationToken ct)
        {
            var (status, body) = await Send(HttpMethod.Get, "/gameserver", null, ct).ConfigureAwait(false);
            if (!IsSuccess(status)) throw new RequestFailedException($"probe failed: HTTP {(int)status}");
            return Model.ParseGameServer(Json.Parse(body));
        }

        public Task<ServerInfo> GetServerInfo(CancellationToken ct) => Probe(ct);

        public Task Ready(CancellationToken ct) => PostLifecycle("/ready", ct);
        public Task Health(CancellationToken ct) => PostLifecycle("/health", ct);
        public Task Shutdown(CancellationToken ct) => PostLifecycle("/shutdown", ct);

        private async Task PostLifecycle(string path, CancellationToken ct)
        {
            var (status, _) = await Send(HttpMethod.Post, path, "{}", ct).ConfigureAwait(false);
            if (!IsSuccess(status)) throw new RequestFailedException($"{path} failed: HTTP {(int)status}");
        }

        public async Task<PlayerList> AddPlayer(string sessionId, long cachedCapacity, CancellationToken ct)
        {
            var (status, body) = await Send(HttpMethod.Post, ListPath + ":addValue", Json.Obj(("value", sessionId)), ct).ConfigureAwait(false);
            if (IsSuccess(status)) return await ReadList(ct).ConfigureAwait(false);
            switch ((int)status)
            {
                case 409:
                    throw new PlayerAlreadyConnectedException($"session {sessionId} already connected");
                case 400 when GrpcCode(body) == 11:
                    throw new ServerFullException($"server full (capacity {cachedCapacity})", cachedCapacity);
                case 404:
                    throw new PlayerTrackingDisabledException("player tracking is disabled for this game");
                default:
                    throw new RequestFailedException($"connect {sessionId} failed: HTTP {(int)status}");
            }
        }

        public async Task<(bool found, PlayerList list)> RemovePlayer(string sessionId, CancellationToken ct)
        {
            var (status, _) = await Send(HttpMethod.Post, ListPath + ":removeValue", Json.Obj(("value", sessionId)), ct).ConfigureAwait(false);
            if (IsSuccess(status)) return (true, await ReadList(ct).ConfigureAwait(false));
            if ((int)status == 404)
            {
                // 404 is ambiguous: value-not-present vs list-missing. Re-read to disambiguate.
                var (ls, lb) = await Send(HttpMethod.Get, ListPath, null, ct).ConfigureAwait(false);
                if (IsSuccess(ls)) return (false, Model.ParseList(Json.Parse(lb)));
                if ((int)ls == 404) throw new PlayerTrackingDisabledException("player tracking is disabled for this game");
                throw new RequestFailedException($"disconnect {sessionId} failed re-read: HTTP {(int)ls}");
            }
            throw new RequestFailedException($"disconnect {sessionId} failed: HTTP {(int)status}");
        }

        private async Task<PlayerList> ReadList(CancellationToken ct)
        {
            var (status, body) = await Send(HttpMethod.Get, ListPath, null, ct).ConfigureAwait(false);
            if (IsSuccess(status)) return Model.ParseList(Json.Parse(body));
            if ((int)status == 404) throw new PlayerTrackingDisabledException("player tracking is disabled for this game");
            throw new RequestFailedException($"reading players list failed: HTTP {(int)status}");
        }

        public async IAsyncEnumerable<ServerInfo> Watch([EnumeratorCancellation] CancellationToken ct)
        {
            using var req = new HttpRequestMessage(HttpMethod.Get, _baseUrl + "/watch/gameserver");
            using var resp = await _http.SendAsync(req, HttpCompletionOption.ResponseHeadersRead, ct).ConfigureAwait(false);
            resp.EnsureSuccessStatusCode();
            using var stream = await resp.Content.ReadAsStreamAsync().ConfigureAwait(false);
            using var reader = new StreamReader(stream, Encoding.UTF8);
            // ReadLineAsync has no cancellation overload on netstandard2.1; disposing the stream
            // unblocks it. resp is disposed once, by its own using.
            using var reg = ct.Register(() => { try { stream.Dispose(); } catch { } });

            while (true)
            {
                string line = null;
                bool cancelled = false;
                try { line = await reader.ReadLineAsync().ConfigureAwait(false); }
                catch when (ct.IsCancellationRequested) { cancelled = true; }
                if (cancelled || line == null) yield break;
                if (line.Length == 0) continue;

                ServerInfo info = TryParseWatchLine(line);
                if (info != null) yield return info;
            }
        }

        private ServerInfo TryParseWatchLine(string line)
        {
            try
            {
                if (Json.Parse(line) is Dictionary<string, object> obj)
                {
                    if (obj.TryGetValue("result", out var r)) return Model.ParseGameServer(r);
                    if (obj.ContainsKey("error")) { _log.Warn("watch stream error line skipped"); return null; }
                }
            }
            catch
            {
                _log.Warn("watch stream parse error; line skipped");
            }
            return null;
        }

        private async Task<(HttpStatusCode status, string body)> Send(HttpMethod method, string path, string jsonBody, CancellationToken ct)
        {
            using var cts = CancellationTokenSource.CreateLinkedTokenSource(ct);
            cts.CancelAfter(_requestTimeoutMs);
            using var req = new HttpRequestMessage(method, _baseUrl + path);
            if (jsonBody != null) req.Content = new StringContent(jsonBody, Encoding.UTF8, "application/json");
            try
            {
                using var resp = await _http.SendAsync(req, cts.Token).ConfigureAwait(false);
                var body = await resp.Content.ReadAsStringAsync().ConfigureAwait(false);
                return (resp.StatusCode, body);
            }
            catch (OperationCanceledException) when (!ct.IsCancellationRequested)
            {
                throw new RequestFailedException($"request {method} {path} timed out after {_requestTimeoutMs} ms");
            }
            catch (HttpRequestException e)
            {
                throw new RequestFailedException($"request {method} {path} failed: {e.Message}", e);
            }
        }

        private static bool IsSuccess(HttpStatusCode status) => (int)status >= 200 && (int)status < 300;

        private static long GrpcCode(string body)
        {
            try
            {
                if (Json.Parse(body) is Dictionary<string, object> d && d.TryGetValue("code", out var c)) return Json.AsLong(c);
            }
            catch { }
            return -1;
        }
    }
}
