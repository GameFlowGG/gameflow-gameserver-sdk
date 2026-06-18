using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace GameFlow
{
    /// <summary>
    /// The runtime boundary. One implementation talks REST to the local platform runtime (<see cref="SidecarTransport"/>),
    /// the other simulates it in-memory for off-platform development (<see cref="LocalTransport"/>).
    /// </summary>
    internal interface ITransport
    {
        /// <summary>Single connection probe. Throws on failure so the client can retry with backoff.</summary>
        Task<ServerInfo> Probe(CancellationToken ct);

        Task Ready(CancellationToken ct);
        Task Health(CancellationToken ct);
        Task Shutdown(CancellationToken ct);

        /// <summary>Registers a session. Throws <see cref="ServerFullException"/> (enriched with capacity),
        /// <see cref="PlayerAlreadyConnectedException"/>, or <see cref="PlayerTrackingDisabledException"/>. Returns the fresh list.</summary>
        Task<PlayerList> AddPlayer(string sessionId, long cachedCapacity, CancellationToken ct);

        /// <summary>Unregisters a session. found=false when it was not present. Idempotent.</summary>
        Task<(bool found, PlayerList list)> RemovePlayer(string sessionId, CancellationToken ct);

        Task<ServerInfo> GetServerInfo(CancellationToken ct);

        /// <summary>One watch attempt: yields each parsed server update until the stream ends. Reconnect lives in the caller.</summary>
        IAsyncEnumerable<ServerInfo> Watch(CancellationToken ct);
    }
}
