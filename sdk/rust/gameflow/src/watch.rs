//! Shared watch stream multiplexing.
//!
//! A single underlying stream is opened lazily on the first subscriber, shared
//! by all subscribers, kept alive with reconnect + backoff, and closed when the
//! last subscriber unsubscribes (or on shutdown).

use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use tokio_util::sync::CancellationToken;

use crate::logger::Logger;
use crate::transport::{Transport, WatchSink};
use crate::types::RawGameServer;

const INITIAL_BACKOFF: Duration = Duration::from_millis(250);
const MAX_BACKOFF: Duration = Duration::from_millis(5000);

/// A listener invoked with every game-server update.
pub(crate) type WatchListener = Arc<dyn Fn(&RawGameServer) + Send + Sync>;

/// An RAII handle to a subscription. Dropping it unsubscribes; call
/// [`Subscription::detach`] to keep the subscription alive for the rest of the
/// program.
#[must_use = "dropping the Subscription immediately unsubscribes; keep it alive or call detach()"]
pub struct Subscription {
    on_drop: Option<Box<dyn FnOnce() + Send + Sync>>,
}

impl Subscription {
    pub(crate) fn new(on_drop: impl FnOnce() + Send + Sync + 'static) -> Self {
        Subscription {
            on_drop: Some(Box::new(on_drop)),
        }
    }

    /// Keeps the subscription active for the lifetime of the program instead of
    /// until this handle is dropped.
    pub fn detach(mut self) {
        self.on_drop = None;
    }
}

impl Drop for Subscription {
    fn drop(&mut self) {
        if let Some(on_drop) = self.on_drop.take() {
            on_drop();
        }
    }
}

pub(crate) struct Watcher {
    inner: Arc<WatcherInner>,
}

struct WatcherInner {
    transport: Arc<dyn Transport>,
    logger: Arc<dyn Logger>,
    on_update: Mutex<Option<WatchListener>>,
    /// The listener map and the last-seen state share one lock so that
    /// registering a subscriber and replaying `latest` to it is atomic against
    /// a concurrent `dispatch` — otherwise a new subscriber could receive a
    /// duplicate or an out-of-order (stale) replay.
    dispatch: Mutex<DispatchState>,
    control: Mutex<Control>,
    next_id: AtomicU64,
}

#[derive(Default)]
struct DispatchState {
    listeners: HashMap<u64, WatchListener>,
    latest: Option<RawGameServer>,
}

#[derive(Default)]
struct Control {
    token: Option<CancellationToken>,
    stopped: bool,
}

impl Watcher {
    pub(crate) fn new(transport: Arc<dyn Transport>, logger: Arc<dyn Logger>) -> Self {
        Watcher {
            inner: Arc::new(WatcherInner {
                transport,
                logger,
                on_update: Mutex::new(None),
                dispatch: Mutex::new(DispatchState::default()),
                control: Mutex::new(Control::default()),
                next_id: AtomicU64::new(0),
            }),
        }
    }

    /// Sets the internal cache-sync hook, invoked before user listeners.
    pub(crate) fn set_on_update(&self, hook: WatchListener) {
        *self.inner.on_update.lock().unwrap() = Some(hook);
    }

    /// Adds a listener and returns a handle that unsubscribes on drop.
    pub(crate) fn subscribe(&self, listener: WatchListener) -> Subscription {
        let id = self.inner.next_id.fetch_add(1, Ordering::Relaxed);
        // Register and read the replay value atomically against dispatch so the
        // newcomer gets the latest state exactly once and in order.
        let latest = {
            let mut dispatch = self.inner.dispatch.lock().unwrap();
            dispatch.listeners.insert(id, listener.clone());
            dispatch.latest.clone()
        };
        // Replay the latest known state outside the lock.
        if let Some(latest) = latest {
            listener(&latest);
        }
        self.inner.ensure_stream();

        let inner = self.inner.clone();
        Subscription::new(move || {
            let mut dispatch = inner.dispatch.lock().unwrap();
            dispatch.listeners.remove(&id);
            if dispatch.listeners.is_empty() {
                drop(dispatch);
                inner.close_stream();
            }
        })
    }

    /// Stops the stream permanently and drops all listeners (shutdown).
    pub(crate) fn stop(&self) {
        {
            let mut control = self.inner.control.lock().unwrap();
            control.stopped = true;
            if let Some(token) = control.token.take() {
                token.cancel();
            }
        }
        self.inner.dispatch.lock().unwrap().listeners.clear();
    }
}

impl WatcherInner {
    fn ensure_stream(self: &Arc<Self>) {
        let mut control = self.control.lock().unwrap();
        if control.stopped || control.token.is_some() {
            return;
        }
        let token = CancellationToken::new();
        control.token = Some(token.clone());
        drop(control);

        let inner = self.clone();
        tokio::spawn(async move { inner.run(token).await });
    }

    fn close_stream(&self) {
        let mut control = self.control.lock().unwrap();
        if let Some(token) = control.token.take() {
            token.cancel();
        }
    }

    async fn run(self: Arc<Self>, cancel: CancellationToken) {
        let mut backoff = INITIAL_BACKOFF;
        while !cancel.is_cancelled() {
            let got_message = Arc::new(std::sync::atomic::AtomicBool::new(false));
            let sink: WatchSink = {
                let inner = self.clone();
                let got = got_message.clone();
                Arc::new(move |gs: RawGameServer| {
                    got.store(true, Ordering::Relaxed);
                    inner.dispatch(gs);
                })
            };

            match self.transport.watch_game_server(sink, cancel.clone()).await {
                Ok(()) => self.logger.debug("watch stream ended; reconnecting"),
                Err(cause) => {
                    if cancel.is_cancelled() {
                        return;
                    }
                    self.logger.warn(&format!(
                        "watch stream failed (retrying in {}ms): {cause}",
                        backoff.as_millis()
                    ));
                }
            }
            if cancel.is_cancelled() {
                return;
            }
            // Reset backoff after a stream that delivered at least one message.
            if got_message.load(Ordering::Relaxed) {
                backoff = INITIAL_BACKOFF;
            }
            tokio::select! {
                _ = cancel.cancelled() => return,
                _ = tokio::time::sleep(backoff) => {}
            }
            backoff = (backoff * 2).min(MAX_BACKOFF);
        }
    }

    fn dispatch(&self, gs: RawGameServer) {
        // Record latest and snapshot the listeners atomically (see `dispatch`
        // field), then invoke hook + listeners outside the lock so user code
        // never runs while the lock is held.
        let listeners: Vec<WatchListener> = {
            let mut dispatch = self.dispatch.lock().unwrap();
            dispatch.latest = Some(gs.clone());
            dispatch.listeners.values().cloned().collect()
        };
        if let Some(hook) = self.on_update.lock().unwrap().clone() {
            hook(&gs);
        }
        for listener in listeners {
            listener(&gs);
        }
    }
}
