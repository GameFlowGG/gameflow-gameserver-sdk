//! In-memory transport for local development (no GameFlow runtime involved).
//!
//! Lifecycle calls are no-ops; player tracking runs against an in-memory list
//! so the rest of the SDK behaves exactly as in production. Simulation knobs
//! come from env vars: `GAMEFLOW_MAX_PLAYERS` (unset = unlimited, `0` = tracking
//! disabled) and `GAMEFLOW_PAYLOAD` (the launch payload).

use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};

use async_trait::async_trait;
use tokio_util::sync::CancellationToken;

use super::{Transport, WatchSink, PLAYERS_LIST};
use crate::env::Env;
use crate::error::{GameFlowError, Result};
use crate::logger::Logger;
use crate::types::{PlayerListSnapshot, RawGameServer, RawList, RawObjectMeta, RawStatus};

/// Capacity sentinel meaning "no configured limit".
const UNLIMITED: i64 = -1;

pub(crate) struct LocalTransport {
    logger: Arc<dyn Logger>,
    capacity: i64,
    payload: Option<String>,
    state: Mutex<State>,
    next_id: AtomicU64,
}

struct State {
    players: Vec<String>,
    lifecycle: String,
    watchers: HashMap<u64, WatchSink>,
}

impl LocalTransport {
    pub(crate) fn new(logger: Arc<dyn Logger>, env: &Env) -> Self {
        let capacity = match env.get("GAMEFLOW_MAX_PLAYERS") {
            None => UNLIMITED,
            Some(raw) if raw.trim().is_empty() => UNLIMITED,
            // A positive value caps the list; zero, negative or malformed values
            // disable tracking (mirrors the TS reference, where `NaN > 0` and
            // `n <= 0` are both falsy).
            Some(raw) => match raw.trim().parse::<i64>() {
                Ok(n) if n > 0 => n,
                _ => 0,
            },
        };
        LocalTransport {
            logger,
            capacity,
            payload: env.get("GAMEFLOW_PAYLOAD"),
            state: Mutex::new(State {
                players: Vec::new(),
                lifecycle: "Scheduled".to_string(),
                watchers: HashMap::new(),
            }),
            next_id: AtomicU64::new(0),
        }
    }

    fn tracking_enabled(&self) -> bool {
        self.capacity != 0
    }

    fn is_full(&self, count: usize) -> bool {
        self.capacity >= 0 && count as i64 >= self.capacity
    }

    fn snapshot(&self, players: &[String]) -> PlayerListSnapshot {
        if self.tracking_enabled() {
            PlayerListSnapshot {
                exists: true,
                capacity: self.capacity,
                values: players.to_vec(),
            }
        } else {
            PlayerListSnapshot::disabled()
        }
    }

    fn build_game_server(&self, state: &State) -> RawGameServer {
        let mut annotations = HashMap::new();
        if let Some(payload) = &self.payload {
            annotations.insert("GAMEFLOW_PAYLOAD".to_string(), payload.clone());
        }
        let mut lists = HashMap::new();
        if self.tracking_enabled() {
            lists.insert(
                PLAYERS_LIST.to_string(),
                RawList {
                    capacity: self.capacity,
                    values: state.players.clone(),
                },
            );
        }
        RawGameServer {
            object_meta: Some(RawObjectMeta {
                name: Some("local-gameserver".to_string()),
                annotations,
                labels: HashMap::new(),
            }),
            status: Some(RawStatus {
                state: Some(state.lifecycle.clone()),
                address: Some("127.0.0.1".to_string()),
                ports: Vec::new(),
                lists,
            }),
        }
    }

    /// Builds the current game server plus a snapshot of the watch sinks, to be
    /// emitted *after* releasing the state lock. User listeners must never run
    /// while `self.state` is held: the std `Mutex` is not reentrant (a listener
    /// that re-enters the transport would deadlock) and a panicking listener
    /// would poison the lock and brick the transport.
    fn prepare_emit(&self, state: &State) -> (RawGameServer, Vec<WatchSink>) {
        (
            self.build_game_server(state),
            state.watchers.values().cloned().collect(),
        )
    }
}

/// Invokes every watch sink with the given game server. Called only after the
/// state lock has been dropped (see [`LocalTransport::prepare_emit`]).
fn dispatch_sinks(gs: &RawGameServer, sinks: &[WatchSink]) {
    for sink in sinks {
        sink(gs.clone());
    }
}

#[async_trait]
impl Transport for LocalTransport {
    async fn ready(&self) -> Result<()> {
        self.logger.debug("local: ready()");
        let (gs, sinks) = {
            let mut state = self.state.lock().unwrap();
            state.lifecycle = "Ready".to_string();
            self.prepare_emit(&state)
        };
        dispatch_sinks(&gs, &sinks);
        Ok(())
    }

    async fn health(&self) -> Result<()> {
        self.logger.debug("local: health ping");
        Ok(())
    }

    async fn shutdown(&self) -> Result<()> {
        self.logger.debug("local: shutdown()");
        let (gs, sinks) = {
            let mut state = self.state.lock().unwrap();
            state.lifecycle = "Shutdown".to_string();
            self.prepare_emit(&state)
        };
        dispatch_sinks(&gs, &sinks);
        Ok(())
    }

    async fn get_game_server(&self) -> Result<RawGameServer> {
        let state = self.state.lock().unwrap();
        Ok(self.build_game_server(&state))
    }

    async fn get_player_list(&self) -> Result<PlayerListSnapshot> {
        let state = self.state.lock().unwrap();
        Ok(self.snapshot(&state.players))
    }

    async fn add_player(&self, session_id: &str) -> Result<PlayerListSnapshot> {
        let (snapshot, gs, sinks) = {
            let mut state = self.state.lock().unwrap();
            if !self.tracking_enabled() {
                return Err(GameFlowError::PlayerTrackingDisabled);
            }
            if state.players.iter().any(|p| p == session_id) {
                return Err(GameFlowError::PlayerAlreadyConnected {
                    session_id: session_id.to_string(),
                });
            }
            if self.is_full(state.players.len()) {
                return Err(GameFlowError::ServerFull {
                    capacity: Some(self.capacity),
                });
            }
            state.players.push(session_id.to_string());
            let snapshot = self.snapshot(&state.players);
            let (gs, sinks) = self.prepare_emit(&state);
            (snapshot, gs, sinks)
        };
        dispatch_sinks(&gs, &sinks);
        Ok(snapshot)
    }

    async fn remove_player(&self, session_id: &str) -> Result<Option<PlayerListSnapshot>> {
        let (snapshot, gs, sinks) = {
            let mut state = self.state.lock().unwrap();
            if !self.tracking_enabled() {
                return Ok(None);
            }
            let Some(index) = state.players.iter().position(|p| p == session_id) else {
                return Ok(None);
            };
            state.players.remove(index);
            let snapshot = self.snapshot(&state.players);
            let (gs, sinks) = self.prepare_emit(&state);
            (snapshot, gs, sinks)
        };
        dispatch_sinks(&gs, &sinks);
        Ok(Some(snapshot))
    }

    async fn watch_game_server(&self, sink: WatchSink, cancel: CancellationToken) -> Result<()> {
        let id = self.next_id.fetch_add(1, Ordering::Relaxed);
        let initial = {
            let mut state = self.state.lock().unwrap();
            state.watchers.insert(id, sink.clone());
            self.build_game_server(&state)
        };
        // Emit the current state immediately (outside the lock), like a fresh
        // sidecar stream.
        sink(initial);
        cancel.cancelled().await;
        self.state.lock().unwrap().watchers.remove(&id);
        Ok(())
    }
}
