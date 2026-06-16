//! The seam between the real GameFlow runtime and local mode.
//!
//! Everything above this trait ([`crate::GameFlow`], [`crate::Players`], the
//! health loop, the watcher) is mode-agnostic.

mod local;
mod sidecar;

use std::sync::Arc;

use async_trait::async_trait;
use tokio_util::sync::CancellationToken;

use crate::error::{GameFlowError, Result};
use crate::types::{PlayerListSnapshot, RawGameServer, RawList};

pub(crate) use local::LocalTransport;
pub(crate) use sidecar::SidecarTransport;

/// The list key the platform uses for player tracking.
pub(crate) const PLAYERS_LIST: &str = "players";

/// A sink the watch stream calls for each parsed game-server update.
pub(crate) type WatchSink = Arc<dyn Fn(RawGameServer) + Send + Sync>;

#[async_trait]
pub(crate) trait Transport: Send + Sync {
    async fn ready(&self) -> Result<()>;
    async fn health(&self) -> Result<()>;
    async fn shutdown(&self) -> Result<()>;
    async fn get_game_server(&self) -> Result<RawGameServer>;
    async fn get_player_list(&self) -> Result<PlayerListSnapshot>;
    async fn add_player(&self, session_id: &str) -> Result<PlayerListSnapshot>;
    /// Resolves `None` when the player was not in the list (idempotent remove).
    async fn remove_player(&self, session_id: &str) -> Result<Option<PlayerListSnapshot>>;
    /// Resolves when the stream ends or is cancelled; errors on transport failure.
    async fn watch_game_server(&self, sink: WatchSink, cancel: CancellationToken) -> Result<()>;
}

/// Coerces an optional raw list into a snapshot. Capacity arrives as a JSON
/// string from grpc-gateway; [`RawList`] already coerced it to `i64`.
pub(crate) fn parse_list(raw: Option<&RawList>) -> PlayerListSnapshot {
    match raw {
        None => PlayerListSnapshot::disabled(),
        Some(list) => PlayerListSnapshot {
            exists: true,
            capacity: list.capacity,
            values: list.values.clone(),
        },
    }
}

/// Wraps a transport so that every call (except `shutdown`, which performs the
/// transition) fails loudly with [`GameFlowError::NotConnected`] once the SDK
/// has been shut down.
pub(crate) struct GuardedTransport {
    inner: Arc<dyn Transport>,
    is_shutdown: Arc<dyn Fn() -> bool + Send + Sync>,
}

impl GuardedTransport {
    pub(crate) fn new(
        inner: Arc<dyn Transport>,
        is_shutdown: Arc<dyn Fn() -> bool + Send + Sync>,
    ) -> Self {
        GuardedTransport { inner, is_shutdown }
    }

    fn guard(&self) -> Result<()> {
        if (self.is_shutdown)() {
            Err(GameFlowError::not_connected())
        } else {
            Ok(())
        }
    }
}

#[async_trait]
impl Transport for GuardedTransport {
    async fn ready(&self) -> Result<()> {
        self.guard()?;
        self.inner.ready().await
    }

    async fn health(&self) -> Result<()> {
        self.guard()?;
        self.inner.health().await
    }

    async fn shutdown(&self) -> Result<()> {
        // Unguarded: shutdown is what transitions the state.
        self.inner.shutdown().await
    }

    async fn get_game_server(&self) -> Result<RawGameServer> {
        self.guard()?;
        self.inner.get_game_server().await
    }

    async fn get_player_list(&self) -> Result<PlayerListSnapshot> {
        self.guard()?;
        self.inner.get_player_list().await
    }

    async fn add_player(&self, session_id: &str) -> Result<PlayerListSnapshot> {
        self.guard()?;
        self.inner.add_player(session_id).await
    }

    async fn remove_player(&self, session_id: &str) -> Result<Option<PlayerListSnapshot>> {
        self.guard()?;
        self.inner.remove_player(session_id).await
    }

    async fn watch_game_server(&self, sink: WatchSink, cancel: CancellationToken) -> Result<()> {
        self.guard()?;
        self.inner.watch_game_server(sink, cancel).await
    }
}
