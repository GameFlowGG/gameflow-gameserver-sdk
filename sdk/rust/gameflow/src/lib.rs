//! Official GameFlow SDK for dedicated game servers.
//!
//! Integrate a dedicated server with [GameFlow](https://gameflow.gg): server
//! lifecycle, automatic health reporting and player tracking. The same code
//! runs on GameFlow and on your machine — off-platform the SDK enters **local
//! mode** automatically, simulating the runtime with zero configuration.
//!
//! ```no_run
//! use gameflow::GameFlow;
//!
//! # async fn run() -> gameflow::Result<()> {
//! let gf = GameFlow::connect().await?;
//! gf.ready().await?; // health reporting starts automatically
//!
//! gf.players().connect("session-1").await?; // when a player joins
//! gf.players().disconnect("session-1").await?; // when a player leaves
//!
//! gf.shutdown().await?; // when the match ends
//! # Ok(())
//! # }
//! ```
//!
//! The SDK is async and built on `tokio`; call it from within a tokio runtime.
//! It is engine-agnostic. For [Bevy](https://bevyengine.org), copy the bridge
//! module from the `bevy-server` example into your project (it compiles against
//! your own Bevy version).

#![forbid(unsafe_code)]
#![warn(missing_docs)]

mod env;
mod error;
mod gameflow;
mod health;
mod logger;
mod players;
mod transport;
mod types;
mod watch;

#[cfg(test)]
mod local_tests;

pub use crate::env::{Ports, TlsPorts};
pub use crate::error::{BoxError, ErrorCode, GameFlowError, Result};
pub use crate::gameflow::{ConnectOptions, GameFlow, Mode, ModeOption};
pub use crate::logger::{Logger, NopLogger, TracingLogger};
pub use crate::players::Players;
pub use crate::types::{ServerInfo, ServerPort};
pub use crate::watch::Subscription;
