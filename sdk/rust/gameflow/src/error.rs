//! Error types for the GameFlow SDK.
//!
//! Every fallible call returns [`GameFlowError`]. The [`GameFlowError::code`]
//! accessor exposes a stable [`ErrorCode`] that is identical across every
//! GameFlow SDK (TypeScript, Godot, Rust, ...); error *messages* are idiomatic
//! per language and not part of the contract.

use std::error::Error as StdError;
use std::fmt;

/// A boxed error usable as the underlying cause of a [`GameFlowError`].
pub type BoxError = Box<dyn StdError + Send + Sync + 'static>;

/// Result alias for SDK operations.
pub type Result<T> = std::result::Result<T, GameFlowError>;

/// Stable, machine-readable error codes shared by every GameFlow SDK.
///
/// These strings are part of the cross-language contract: handle them
/// programmatically and keep them frozen even if variant names change.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ErrorCode {
    /// The GameFlow runtime could not be reached (or stopped responding).
    SidecarUnavailable,
    /// The session id is already in the connected players list.
    PlayerAlreadyConnected,
    /// The players list is at capacity.
    ServerFull,
    /// Player tracking is disabled (the game was created with max players = 0).
    PlayerTrackingDisabled,
    /// A method was called after the SDK was shut down.
    NotConnected,
    /// An unexpected non-2xx response or transport failure.
    RequestFailed,
}

impl ErrorCode {
    /// The stable wire string for this code.
    pub const fn as_str(self) -> &'static str {
        match self {
            ErrorCode::SidecarUnavailable => "SIDECAR_UNAVAILABLE",
            ErrorCode::PlayerAlreadyConnected => "PLAYER_ALREADY_CONNECTED",
            ErrorCode::ServerFull => "SERVER_FULL",
            ErrorCode::PlayerTrackingDisabled => "PLAYER_TRACKING_DISABLED",
            ErrorCode::NotConnected => "NOT_CONNECTED",
            ErrorCode::RequestFailed => "REQUEST_FAILED",
        }
    }
}

impl fmt::Display for ErrorCode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.as_str())
    }
}

/// The single error type returned by every fallible SDK call.
#[non_exhaustive]
#[derive(Debug)]
pub enum GameFlowError {
    /// The GameFlow runtime could not be reached (or stopped responding).
    SidecarUnavailable {
        /// Human-readable detail.
        message: String,
        /// The underlying transport cause, when available.
        source: Option<BoxError>,
    },
    /// The session id is already in the connected players list.
    PlayerAlreadyConnected {
        /// The offending session id.
        session_id: String,
    },
    /// The players list is at capacity.
    ServerFull {
        /// The configured capacity, when known.
        capacity: Option<i64>,
    },
    /// Player tracking is disabled (the game was created with max players = 0).
    PlayerTrackingDisabled,
    /// A method was called after the SDK was shut down.
    NotConnected {
        /// Human-readable detail.
        message: String,
    },
    /// An unexpected non-2xx response or transport failure.
    RequestFailed {
        /// Human-readable detail.
        message: String,
        /// The HTTP status, when the failure came from a response.
        status: Option<u16>,
        /// The underlying cause, when available.
        source: Option<BoxError>,
    },
}

impl GameFlowError {
    /// The stable [`ErrorCode`] for this error.
    pub fn code(&self) -> ErrorCode {
        match self {
            GameFlowError::SidecarUnavailable { .. } => ErrorCode::SidecarUnavailable,
            GameFlowError::PlayerAlreadyConnected { .. } => ErrorCode::PlayerAlreadyConnected,
            GameFlowError::ServerFull { .. } => ErrorCode::ServerFull,
            GameFlowError::PlayerTrackingDisabled => ErrorCode::PlayerTrackingDisabled,
            GameFlowError::NotConnected { .. } => ErrorCode::NotConnected,
            GameFlowError::RequestFailed { .. } => ErrorCode::RequestFailed,
        }
    }

    pub(crate) fn sidecar_unavailable_with(
        message: impl Into<String>,
        source: Option<BoxError>,
    ) -> Self {
        GameFlowError::SidecarUnavailable {
            message: message.into(),
            source,
        }
    }

    pub(crate) fn not_connected() -> Self {
        GameFlowError::NotConnected {
            message: "the SDK has been shut down".to_string(),
        }
    }
}

impl fmt::Display for GameFlowError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            GameFlowError::SidecarUnavailable { message, .. } => write!(f, "{message}"),
            GameFlowError::PlayerAlreadyConnected { session_id } => {
                write!(f, "player \"{session_id}\" is already connected")
            }
            GameFlowError::ServerFull {
                capacity: Some(capacity),
            } => {
                write!(f, "server is full (capacity {capacity})")
            }
            GameFlowError::ServerFull { capacity: None } => write!(f, "server is full"),
            GameFlowError::PlayerTrackingDisabled => write!(
                f,
                "player tracking is disabled for this server. Set \"Max Players per Server\" to a \
                 value greater than 0 in your game settings on GameFlow"
            ),
            GameFlowError::NotConnected { message } => write!(f, "{message}"),
            GameFlowError::RequestFailed { message, .. } => write!(f, "{message}"),
        }
    }
}

impl StdError for GameFlowError {
    fn source(&self) -> Option<&(dyn StdError + 'static)> {
        match self {
            GameFlowError::SidecarUnavailable {
                source: Some(source),
                ..
            }
            | GameFlowError::RequestFailed {
                source: Some(source),
                ..
            } => Some(source.as_ref() as &(dyn StdError + 'static)),
            _ => None,
        }
    }
}
