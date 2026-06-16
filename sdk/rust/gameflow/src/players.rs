//! Player tracking.

use std::sync::{Arc, Mutex};

use crate::error::{GameFlowError, Result};
use crate::transport::{parse_list, Transport, PLAYERS_LIST};
use crate::types::{PlayerListSnapshot, RawGameServer};

/// Player tracking over the platform's players list.
///
/// Mutations always go to the runtime; the returned list is kept as an
/// authoritative cache so [`Players::count`], [`Players::list`] and
/// [`Players::capacity`] are synchronous.
pub struct Players {
    transport: Arc<dyn Transport>,
    snapshot: Mutex<PlayerListSnapshot>,
}

impl Players {
    pub(crate) fn new(transport: Arc<dyn Transport>) -> Self {
        Players {
            transport,
            snapshot: Mutex::new(PlayerListSnapshot::disabled()),
        }
    }

    /// Registers a player session. Call when a player joins the server.
    ///
    /// Fails with [`GameFlowError::PlayerAlreadyConnected`] on a duplicate id,
    /// [`GameFlowError::ServerFull`] when the list is at capacity, and
    /// [`GameFlowError::PlayerTrackingDisabled`] when tracking is off.
    pub async fn connect(&self, session_id: &str) -> Result<()> {
        match self.transport.add_player(session_id).await {
            Ok(snapshot) => {
                *self.snapshot.lock().unwrap() = snapshot;
                Ok(())
            }
            Err(GameFlowError::PlayerTrackingDisabled) => {
                *self.snapshot.lock().unwrap() = PlayerListSnapshot::disabled();
                Err(GameFlowError::PlayerTrackingDisabled)
            }
            // Enrich a wire ServerFull that carried no capacity with the cache.
            Err(GameFlowError::ServerFull { capacity: None }) => {
                let capacity = self.snapshot.lock().unwrap().capacity;
                Err(GameFlowError::ServerFull {
                    capacity: Some(capacity),
                })
            }
            Err(other) => Err(other),
        }
    }

    /// Unregisters a player session. Resolves `false` when the player was not in
    /// the list (safe to call on every disconnect, including duplicates).
    pub async fn disconnect(&self, session_id: &str) -> Result<bool> {
        if !self.snapshot.lock().unwrap().exists {
            return Err(GameFlowError::PlayerTrackingDisabled);
        }
        match self.transport.remove_player(session_id).await? {
            None => Ok(false),
            Some(snapshot) => {
                *self.snapshot.lock().unwrap() = snapshot;
                Ok(true)
            }
        }
    }

    /// Current number of connected players (from the local cache).
    pub fn count(&self) -> usize {
        self.snapshot.lock().unwrap().values.len()
    }

    /// Session ids of connected players (from the local cache).
    pub fn list(&self) -> Vec<String> {
        self.snapshot.lock().unwrap().values.clone()
    }

    /// Maximum players configured for this game. `-1` means no configured limit
    /// (local mode with `GAMEFLOW_MAX_PLAYERS` unset).
    pub fn capacity(&self) -> i64 {
        self.snapshot.lock().unwrap().capacity
    }

    /// `false` when the game was created with max players = 0.
    pub fn tracking_enabled(&self) -> bool {
        self.snapshot.lock().unwrap().exists
    }

    /// Re-reads the list from the runtime.
    pub async fn refresh(&self) -> Result<()> {
        let snapshot = self.transport.get_player_list().await?;
        *self.snapshot.lock().unwrap() = snapshot;
        Ok(())
    }

    /// Updates the cache from a game-server object (connect seed and watch).
    pub(crate) fn sync_from_game_server(&self, gs: &RawGameServer) {
        let raw = gs.status.as_ref().and_then(|s| s.lists.get(PLAYERS_LIST));
        *self.snapshot.lock().unwrap() = parse_list(raw);
    }
}
