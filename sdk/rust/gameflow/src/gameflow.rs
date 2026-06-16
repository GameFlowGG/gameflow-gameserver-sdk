//! The top-level [`GameFlow`] handle and connection options.

use std::collections::HashMap;
use std::fmt;
use std::sync::atomic::{AtomicU64, AtomicU8, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use crate::env::{Env, GameFlowEnv, Ports};
use crate::error::{BoxError, GameFlowError, Result};
use crate::health::HealthLoop;
use crate::logger::{default_logger, Logger};
use crate::players::Players;
use crate::transport::{GuardedTransport, LocalTransport, SidecarTransport, Transport};
use crate::types::{to_info, RawGameServer, ServerInfo};
use crate::watch::{Subscription, Watcher};

const PAYLOAD_ANNOTATION: &str = "GAMEFLOW_PAYLOAD";
const DEFAULT_SIDECAR_PORT: u16 = 9358;
const MIN_HEALTH_INTERVAL: Duration = Duration::from_millis(500);
const MAX_PROBE_BACKOFF: Duration = Duration::from_millis(4000);
const INITIAL_PROBE_BACKOFF: Duration = Duration::from_millis(250);

// Lifecycle state, ordered so guards can compare with `>=`.
const STATE_CONNECTED: u8 = 0;
const STATE_READY: u8 = 1;
const STATE_SHUTTING_DOWN: u8 = 2;
const STATE_SHUTDOWN: u8 = 3;

/// Which transport the SDK is using.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Mode {
    /// Running on GameFlow, talking to the runtime.
    Sidecar,
    /// Running off-platform with a simulated runtime.
    Local,
}

impl fmt::Display for Mode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            Mode::Sidecar => "sidecar",
            Mode::Local => "local",
        })
    }
}

/// Transport selection passed to [`GameFlow::connect_with`].
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum ModeOption {
    /// Use the runtime on GameFlow, fall back to local mode otherwise.
    #[default]
    Auto,
    /// Force sidecar mode.
    Sidecar,
    /// Force local mode.
    Local,
}

/// Options for [`GameFlow::connect_with`].
///
/// Construct with [`ConnectOptions::new`] and the builder methods; the struct is
/// `#[non_exhaustive]` so new options can be added without a breaking change.
#[derive(Clone)]
#[non_exhaustive]
pub struct ConnectOptions {
    /// Transport selection. The `GAMEFLOW_SDK_MODE` env var overrides `Auto`.
    pub mode: ModeOption,
    /// Interval between automatic health pings. Default 5s, clamped to >= 500ms.
    pub health_interval: Duration,
    /// Total budget for `connect` including retries. Default 30s.
    pub connect_timeout: Duration,
    /// Per-request timeout. Default 3s.
    pub request_timeout: Duration,
    /// Logger. Defaults to [`crate::TracingLogger`]; pass [`crate::NopLogger`]
    /// to silence the SDK.
    pub logger: Arc<dyn Logger>,
    /// Called once when health pings have been failing for a sustained period.
    pub on_health_degraded: Option<Arc<dyn Fn() + Send + Sync>>,
}

impl Default for ConnectOptions {
    fn default() -> Self {
        ConnectOptions {
            mode: ModeOption::Auto,
            health_interval: Duration::from_secs(5),
            connect_timeout: Duration::from_secs(30),
            request_timeout: Duration::from_secs(3),
            logger: default_logger(),
            on_health_degraded: None,
        }
    }
}

impl fmt::Debug for ConnectOptions {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("ConnectOptions")
            .field("mode", &self.mode)
            .field("health_interval", &self.health_interval)
            .field("connect_timeout", &self.connect_timeout)
            .field("request_timeout", &self.request_timeout)
            .finish_non_exhaustive()
    }
}

impl ConnectOptions {
    /// A fresh set of default options.
    pub fn new() -> Self {
        Self::default()
    }

    /// Forces a transport mode.
    pub fn mode(mut self, mode: ModeOption) -> Self {
        self.mode = mode;
        self
    }

    /// Sets the health ping interval (clamped to >= 500ms at connect time).
    pub fn health_interval(mut self, interval: Duration) -> Self {
        self.health_interval = interval;
        self
    }

    /// Sets the total connect budget.
    pub fn connect_timeout(mut self, timeout: Duration) -> Self {
        self.connect_timeout = timeout;
        self
    }

    /// Sets the per-request timeout.
    pub fn request_timeout(mut self, timeout: Duration) -> Self {
        self.request_timeout = timeout;
        self
    }

    /// Installs a custom logger.
    pub fn logger(mut self, logger: Arc<dyn Logger>) -> Self {
        self.logger = logger;
        self
    }

    /// Silences the SDK entirely.
    pub fn silent(mut self) -> Self {
        self.logger = Arc::new(crate::logger::NopLogger);
        self
    }

    /// Sets the degraded-health callback.
    pub fn on_health_degraded(mut self, callback: Arc<dyn Fn() + Send + Sync>) -> Self {
        self.on_health_degraded = Some(callback);
        self
    }
}

type PayloadListener = Arc<dyn Fn(Option<&str>) + Send + Sync>;

struct Inner {
    mode: Mode,
    logger: Arc<dyn Logger>,
    transport: Arc<dyn Transport>,
    players: Arc<Players>,
    watcher: Watcher,
    health: HealthLoop,
    env: GameFlowEnv,
    payload_listeners: Mutex<HashMap<u64, PayloadListener>>,
    payload_next_id: AtomicU64,
    last_payload: Mutex<Option<String>>,
    state: Arc<AtomicU8>,
}

impl Inner {
    fn handle_update(&self, gs: &RawGameServer) {
        self.players.sync_from_game_server(gs);
        let payload = payload_of(gs);
        let mut last = self.last_payload.lock().unwrap();
        if *last != payload {
            *last = payload.clone();
            drop(last);
            let listeners: Vec<PayloadListener> = self
                .payload_listeners
                .lock()
                .unwrap()
                .values()
                .cloned()
                .collect();
            for listener in listeners {
                listener(payload.as_deref());
            }
        }
    }
}

/// The GameFlow game-server SDK handle.
///
/// Cheap to clone (it is an `Arc` internally) so it can be shared across tasks.
/// Connect once with [`GameFlow::connect`], then drive the server lifecycle and
/// player tracking through this handle.
#[derive(Clone)]
pub struct GameFlow {
    inner: Arc<Inner>,
}

impl GameFlow {
    /// Connects with default options.
    pub async fn connect() -> Result<GameFlow> {
        Self::connect_with(ConnectOptions::default()).await
    }

    /// Connects with explicit options.
    pub async fn connect_with(options: ConnectOptions) -> Result<GameFlow> {
        Self::connect_with_env(options, Env::process()).await
    }

    pub(crate) async fn connect_with_env(options: ConnectOptions, env: Env) -> Result<GameFlow> {
        let logger = options.logger.clone();
        let mode = resolve_mode(options.mode, &env, logger.as_ref());
        let state = Arc::new(AtomicU8::new(STATE_CONNECTED));

        let raw_transport: Arc<dyn Transport> = match mode {
            Mode::Local => Arc::new(LocalTransport::new(logger.clone(), &env)),
            Mode::Sidecar => {
                let port = sidecar_port(&env);
                Arc::new(SidecarTransport::new(
                    format!("http://127.0.0.1:{port}"),
                    options.request_timeout,
                    logger.clone(),
                )?)
            }
        };

        let is_shutdown: Arc<dyn Fn() -> bool + Send + Sync> = {
            let state = state.clone();
            Arc::new(move || state.load(Ordering::SeqCst) >= STATE_SHUTDOWN)
        };
        let transport: Arc<dyn Transport> =
            Arc::new(GuardedTransport::new(raw_transport, is_shutdown));

        let players = Arc::new(Players::new(transport.clone()));
        let watcher = Watcher::new(transport.clone(), logger.clone());
        let health = HealthLoop::new(
            transport.clone(),
            options.health_interval.max(MIN_HEALTH_INTERVAL),
            logger.clone(),
            options.on_health_degraded.clone(),
        );

        let inner = Arc::new(Inner {
            mode,
            logger,
            transport,
            players,
            watcher,
            health,
            env: GameFlowEnv::new(env),
            payload_listeners: Mutex::new(HashMap::new()),
            payload_next_id: AtomicU64::new(0),
            last_payload: Mutex::new(None),
            state,
        });

        // Wire the cache-sync hook with a weak ref to avoid a reference cycle.
        let weak = Arc::downgrade(&inner);
        inner
            .watcher
            .set_on_update(Arc::new(move |gs: &RawGameServer| {
                if let Some(inner) = weak.upgrade() {
                    inner.handle_update(gs);
                }
            }));

        let gameflow = GameFlow { inner };
        gameflow.init(options.connect_timeout).await?;
        Ok(gameflow)
    }

    async fn init(&self, connect_timeout: Duration) -> Result<()> {
        let gs = self.probe_with_retry(connect_timeout).await?;
        self.inner.players.sync_from_game_server(&gs);
        *self.inner.last_payload.lock().unwrap() = payload_of(&gs);
        if self.inner.mode == Mode::Sidecar && !self.inner.players.tracking_enabled() {
            self.inner.logger.warn(
                "player tracking is disabled for this server (max players is 0). The platform \
                 cannot see player counts and idle servers with no trackable players may be shut \
                 down. Set \"Max Players per Server\" in your game settings to enable tracking.",
            );
        }
        self.inner
            .logger
            .info(&format!("connected (mode: {})", self.inner.mode));
        Ok(())
    }

    async fn probe_with_retry(&self, connect_timeout: Duration) -> Result<RawGameServer> {
        let deadline = Instant::now() + connect_timeout;
        let mut backoff = INITIAL_PROBE_BACKOFF;
        loop {
            let error = match self.inner.transport.get_game_server().await {
                Ok(gs) => return Ok(gs),
                Err(error) => error,
            };
            let wait = jitter(backoff);
            if Instant::now() + wait >= deadline {
                return Err(GameFlowError::sidecar_unavailable_with(
                    format!(
                        "could not connect to the GameFlow runtime within {}ms",
                        connect_timeout.as_millis()
                    ),
                    Some(Box::new(error) as BoxError),
                ));
            }
            self.inner.logger.debug(&format!(
                "runtime not reachable yet; retrying in {}ms",
                wait.as_millis()
            ));
            tokio::time::sleep(wait).await;
            backoff = (backoff * 2).min(MAX_PROBE_BACKOFF);
        }
    }

    /// Marks the server ready to accept players and starts the automatic health
    /// heartbeat. Call once your server is listening.
    pub async fn ready(&self) -> Result<()> {
        self.assert_usable()?;
        if self.inner.state.load(Ordering::SeqCst) == STATE_READY {
            self.inner
                .logger
                .debug("ready() called more than once; ignoring");
            return Ok(());
        }
        self.inner.transport.ready().await?;
        // Transition CONNECTED -> READY atomically so a shutdown that raced the
        // request above can't be clobbered back to READY (which would start a
        // zombie health loop after shutdown).
        match self.inner.state.compare_exchange(
            STATE_CONNECTED,
            STATE_READY,
            Ordering::SeqCst,
            Ordering::SeqCst,
        ) {
            Ok(_) => {}
            Err(actual) if actual == STATE_READY => return Ok(()),
            Err(_) => return Err(GameFlowError::not_connected()),
        }
        if self.inner.mode == Mode::Sidecar {
            self.inner.health.start();
        }
        self.inner.logger.info("server marked ready");
        Ok(())
    }

    /// Shuts the server down cleanly. Idempotent: repeated calls return
    /// immediately and only one request is sent. After this resolves every
    /// other method fails with [`GameFlowError::NotConnected`].
    pub async fn shutdown(&self) -> Result<()> {
        // Claim the shutdown exactly once.
        loop {
            let current = self.inner.state.load(Ordering::SeqCst);
            if current >= STATE_SHUTTING_DOWN {
                return Ok(());
            }
            if self
                .inner
                .state
                .compare_exchange(
                    current,
                    STATE_SHUTTING_DOWN,
                    Ordering::SeqCst,
                    Ordering::SeqCst,
                )
                .is_ok()
            {
                break;
            }
        }
        // Stop the health loop and watch stream first, then send shutdown.
        self.inner.health.stop();
        self.inner.watcher.stop();
        if let Err(cause) = self.inner.transport.shutdown().await {
            self.inner
                .logger
                .warn(&format!("shutdown request failed: {cause}"));
        }
        self.inner.state.store(STATE_SHUTDOWN, Ordering::SeqCst);
        self.inner.logger.info("server shut down");
        Ok(())
    }

    /// The launch payload for this server (an opaque string), or `None`.
    ///
    /// May change when the server is assigned to a new match; use
    /// [`GameFlow::on_payload_change`] to react.
    pub async fn payload(&self) -> Result<Option<String>> {
        self.assert_usable()?;
        let gs = self.inner.transport.get_game_server().await?;
        let payload = payload_of(&gs);
        *self.inner.last_payload.lock().unwrap() = payload.clone();
        Ok(payload)
    }

    /// Current server details (name, state, address, ports).
    pub async fn info(&self) -> Result<ServerInfo> {
        self.assert_usable()?;
        Ok(to_info(&self.inner.transport.get_game_server().await?))
    }

    /// Subscribes to server updates. The stream is shared across subscribers and
    /// reconnects automatically. The returned [`Subscription`] unsubscribes when
    /// dropped.
    pub fn watch(
        &self,
        listener: impl Fn(ServerInfo) + Send + Sync + 'static,
    ) -> Result<Subscription> {
        self.assert_usable()?;
        Ok(self
            .inner
            .watcher
            .subscribe(Arc::new(move |gs: &RawGameServer| listener(to_info(gs)))))
    }

    /// Fires when the launch payload changes (e.g. on match assignment).
    pub fn on_payload_change(
        &self,
        listener: impl Fn(Option<String>) + Send + Sync + 'static,
    ) -> Result<Subscription> {
        self.assert_usable()?;
        let id = self.inner.payload_next_id.fetch_add(1, Ordering::Relaxed);
        self.inner.payload_listeners.lock().unwrap().insert(
            id,
            Arc::new(move |payload: Option<&str>| listener(payload.map(str::to_string))),
        );
        // Keep the stream open with a no-op watch subscription.
        let watch_sub = self
            .inner
            .watcher
            .subscribe(Arc::new(|_: &RawGameServer| {}));
        let inner = self.inner.clone();
        Ok(Subscription::new(move || {
            inner.payload_listeners.lock().unwrap().remove(&id);
            drop(watch_sub);
        }))
    }

    /// Player tracking.
    pub fn players(&self) -> &Players {
        &self.inner.players
    }

    /// Ports assigned to this server.
    pub fn ports(&self) -> Ports {
        self.inner.env.ports()
    }

    /// Region this server runs in, when provided by the platform.
    pub fn region(&self) -> Option<String> {
        self.inner.env.region()
    }

    /// Build id of the running image, when provided by the platform.
    pub fn build_id(&self) -> Option<String> {
        self.inner.env.build_id()
    }

    /// Transport in use: [`Mode::Sidecar`] on GameFlow, [`Mode::Local`] otherwise.
    pub fn mode(&self) -> Mode {
        self.inner.mode
    }

    fn assert_usable(&self) -> Result<()> {
        if self.inner.state.load(Ordering::SeqCst) >= STATE_SHUTTING_DOWN {
            return Err(GameFlowError::not_connected());
        }
        Ok(())
    }
}

fn resolve_mode(option: ModeOption, env: &Env, logger: &dyn Logger) -> Mode {
    match option {
        ModeOption::Sidecar => return Mode::Sidecar,
        ModeOption::Local => return Mode::Local,
        ModeOption::Auto => {}
    }
    match env.get("GAMEFLOW_SDK_MODE").as_deref() {
        Some("sidecar") => Mode::Sidecar,
        Some("local") => Mode::Local,
        _ => {
            // Any non-empty value selects sidecar (mirrors the TS truthiness
            // check): a real pod with a malformed port must still fail hard
            // rather than silently fall back to local mode.
            if env
                .get("AGONES_SDK_HTTP_PORT")
                .is_some_and(|p| !p.is_empty())
            {
                Mode::Sidecar
            } else {
                logger.info(
                    "no GameFlow runtime detected; running in local mode (lifecycle and player \
                     tracking are simulated)",
                );
                Mode::Local
            }
        }
    }
}

/// Resolves the sidecar port from `AGONES_SDK_HTTP_PORT`, falling back to the
/// default for unset/empty/zero/invalid values (matching the TS truthiness
/// fallback `Number(...) || DEFAULT`).
fn sidecar_port(env: &Env) -> u16 {
    env.get("AGONES_SDK_HTTP_PORT")
        .and_then(|raw| raw.trim().parse::<u16>().ok())
        .filter(|port| *port > 0)
        .unwrap_or(DEFAULT_SIDECAR_PORT)
}

fn payload_of(gs: &RawGameServer) -> Option<String> {
    gs.object_meta
        .as_ref()?
        .annotations
        .get(PAYLOAD_ANNOTATION)
        .cloned()
}

/// Applies +/- 20% jitter to a backoff duration.
fn jitter(duration: Duration) -> Duration {
    let nanos = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.subsec_nanos())
        .unwrap_or(0);
    let fraction = (nanos % 1000) as f64 / 1000.0; // 0.0..1.0
    let factor = 0.8 + fraction * 0.4; // 0.8..1.2
    Duration::from_secs_f64(duration.as_secs_f64() * factor)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::logger::NopLogger;

    fn mode(option: ModeOption, pairs: &[(&str, &str)]) -> Mode {
        resolve_mode(option, &Env::from_pairs(pairs), &NopLogger)
    }

    #[test]
    fn mode_detection_precedence() {
        // Explicit option wins over everything.
        assert_eq!(
            mode(ModeOption::Local, &[("AGONES_SDK_HTTP_PORT", "9358")]),
            Mode::Local
        );
        assert_eq!(mode(ModeOption::Sidecar, &[]), Mode::Sidecar);

        // GAMEFLOW_SDK_MODE wins over auto-detection.
        assert_eq!(
            mode(
                ModeOption::Auto,
                &[
                    ("GAMEFLOW_SDK_MODE", "local"),
                    ("AGONES_SDK_HTTP_PORT", "9358")
                ]
            ),
            Mode::Local
        );
        assert_eq!(
            mode(ModeOption::Auto, &[("GAMEFLOW_SDK_MODE", "sidecar")]),
            Mode::Sidecar
        );

        // Any non-empty AGONES port (including whitespace) selects sidecar and
        // fails hard; only empty/unset falls back to local.
        assert_eq!(
            mode(ModeOption::Auto, &[("AGONES_SDK_HTTP_PORT", "9358")]),
            Mode::Sidecar
        );
        assert_eq!(
            mode(ModeOption::Auto, &[("AGONES_SDK_HTTP_PORT", " ")]),
            Mode::Sidecar
        );
        assert_eq!(
            mode(ModeOption::Auto, &[("AGONES_SDK_HTTP_PORT", "")]),
            Mode::Local
        );
        assert_eq!(mode(ModeOption::Auto, &[]), Mode::Local);
    }

    #[test]
    fn sidecar_port_falls_back_for_invalid_values() {
        assert_eq!(
            sidecar_port(&Env::from_pairs(&[("AGONES_SDK_HTTP_PORT", "9358")])),
            9358
        );
        assert_eq!(
            sidecar_port(&Env::from_pairs(&[("AGONES_SDK_HTTP_PORT", " 9358 ")])),
            9358
        );
        assert_eq!(
            sidecar_port(&Env::from_pairs(&[("AGONES_SDK_HTTP_PORT", "0")])),
            DEFAULT_SIDECAR_PORT
        );
        assert_eq!(
            sidecar_port(&Env::from_pairs(&[("AGONES_SDK_HTTP_PORT", "abc")])),
            DEFAULT_SIDECAR_PORT
        );
        assert_eq!(sidecar_port(&Env::from_pairs(&[])), DEFAULT_SIDECAR_PORT);
    }
}
