using System;

namespace GameFlow
{
    /// <summary>Stable error codes, identical across every GameFlow SDK.</summary>
    public enum GameFlowErrorCode
    {
        SidecarUnavailable,
        PlayerAlreadyConnected,
        ServerFull,
        PlayerTrackingDisabled,
        NotConnected,
        RequestFailed,
    }

    /// <summary>Base type for every error the SDK raises. The <see cref="Code"/> is contract; the type and message are idiomatic.</summary>
    public class GameFlowException : Exception
    {
        public GameFlowErrorCode Code { get; }

        public GameFlowException(GameFlowErrorCode code, string message, Exception inner = null)
            : base(message, inner) => Code = code;
    }

    /// <summary>The local GameFlow runtime could not be reached within the connect timeout.</summary>
    public sealed class SidecarUnavailableException : GameFlowException
    {
        public SidecarUnavailableException(string message, Exception inner = null)
            : base(GameFlowErrorCode.SidecarUnavailable, message, inner) { }
    }

    /// <summary>The session is already connected.</summary>
    public sealed class PlayerAlreadyConnectedException : GameFlowException
    {
        public PlayerAlreadyConnectedException(string message)
            : base(GameFlowErrorCode.PlayerAlreadyConnected, message) { }
    }

    /// <summary>The server is at capacity. <see cref="Capacity"/> carries the known limit.</summary>
    public sealed class ServerFullException : GameFlowException
    {
        public long Capacity { get; }

        public ServerFullException(string message, long capacity)
            : base(GameFlowErrorCode.ServerFull, message) => Capacity = capacity;
    }

    /// <summary>Player tracking is disabled for this game (created with max players = 0).</summary>
    public sealed class PlayerTrackingDisabledException : GameFlowException
    {
        public PlayerTrackingDisabledException(string message)
            : base(GameFlowErrorCode.PlayerTrackingDisabled, message) { }
    }

    /// <summary>The SDK is not connected (before connect, or after shutdown).</summary>
    public sealed class NotConnectedException : GameFlowException
    {
        public NotConnectedException(string message)
            : base(GameFlowErrorCode.NotConnected, message) { }
    }

    /// <summary>A runtime request failed for a reason without a more specific code.</summary>
    public sealed class RequestFailedException : GameFlowException
    {
        public RequestFailedException(string message, Exception inner = null)
            : base(GameFlowErrorCode.RequestFailed, message, inner) { }
    }
}
