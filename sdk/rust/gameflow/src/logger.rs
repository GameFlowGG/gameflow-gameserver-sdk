//! Pluggable logging.
//!
//! The SDK logs through a [`Logger`] so output is fully injectable and
//! silenceable. The default [`TracingLogger`] forwards to the [`tracing`]
//! ecosystem under the `gameflow` target (the idiomatic Rust equivalent of the
//! `[gameflow]` line prefix every other SDK uses), so it integrates with
//! `tracing-subscriber`, Bevy's `LogPlugin`, and anything else that consumes
//! `tracing`. Pass [`NopLogger`] to silence the SDK entirely.

use std::sync::Arc;

/// A sink for the SDK's diagnostic messages.
///
/// Levels follow the cross-language spec: lifecycle transitions at `info`,
/// recoverable problems at `warn`, degraded health at `error`, everything else
/// at `debug`.
pub trait Logger: Send + Sync {
    /// Verbose diagnostic detail (off by default in most subscribers).
    fn debug(&self, message: &str);
    /// Lifecycle transitions (connect, ready, shutdown).
    fn info(&self, message: &str);
    /// Recoverable problems (a failed health ping, a malformed stream line).
    fn warn(&self, message: &str);
    /// Sustained failures (degraded health).
    fn error(&self, message: &str);
}

/// Default logger: forwards to [`tracing`] under the `gameflow` target.
#[derive(Debug, Default, Clone, Copy)]
pub struct TracingLogger;

impl Logger for TracingLogger {
    fn debug(&self, message: &str) {
        tracing::debug!(target: "gameflow", "{message}");
    }
    fn info(&self, message: &str) {
        tracing::info!(target: "gameflow", "{message}");
    }
    fn warn(&self, message: &str) {
        tracing::warn!(target: "gameflow", "{message}");
    }
    fn error(&self, message: &str) {
        tracing::error!(target: "gameflow", "{message}");
    }
}

/// A logger that drops every message. Use it to silence the SDK.
#[derive(Debug, Default, Clone, Copy)]
pub struct NopLogger;

impl Logger for NopLogger {
    fn debug(&self, _message: &str) {}
    fn info(&self, _message: &str) {}
    fn warn(&self, _message: &str) {}
    fn error(&self, _message: &str) {}
}

pub(crate) fn default_logger() -> Arc<dyn Logger> {
    Arc::new(TracingLogger)
}
