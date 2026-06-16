//! Automatic health heartbeat.
//!
//! Starts when `ready()` succeeds (sidecar mode only). A self-scheduling loop
//! pings, waits for the ping to settle, then sleeps — so pings never overlap or
//! pile up. Failures are tolerated (logged, no backoff); after a sustained run
//! of failures it reports degraded health once and recovers silently.

use std::sync::{Arc, Mutex};
use std::time::Duration;

use tokio::task::JoinHandle;
use tokio_util::sync::CancellationToken;

use crate::logger::Logger;
use crate::transport::Transport;

/// Consecutive failures before reporting degraded health.
const DEGRADED_THRESHOLD: u32 = 6;

type DegradedCallback = Arc<dyn Fn() + Send + Sync>;

pub(crate) struct HealthLoop {
    transport: Arc<dyn Transport>,
    interval: Duration,
    logger: Arc<dyn Logger>,
    on_degraded: Option<DegradedCallback>,
    handle: Mutex<Option<(CancellationToken, JoinHandle<()>)>>,
}

impl HealthLoop {
    pub(crate) fn new(
        transport: Arc<dyn Transport>,
        interval: Duration,
        logger: Arc<dyn Logger>,
        on_degraded: Option<DegradedCallback>,
    ) -> Self {
        HealthLoop {
            transport,
            interval,
            logger,
            on_degraded,
            handle: Mutex::new(None),
        }
    }

    pub(crate) fn start(&self) {
        // The handle mutex is the single source of truth and the whole of
        // start()/stop() runs inside it, so concurrent ready()/shutdown() can
        // never interleave to leave a spawned-but-uncancellable loop.
        let mut handle = self.handle.lock().unwrap();
        if handle.is_some() {
            return; // already running
        }
        let token = CancellationToken::new();
        let task = tokio::spawn(run(
            self.transport.clone(),
            self.interval,
            self.logger.clone(),
            self.on_degraded.clone(),
            token.clone(),
        ));
        *handle = Some((token, task));
    }

    pub(crate) fn stop(&self) {
        if let Some((token, task)) = self.handle.lock().unwrap().take() {
            token.cancel();
            task.abort();
        }
    }
}

async fn run(
    transport: Arc<dyn Transport>,
    interval: Duration,
    logger: Arc<dyn Logger>,
    on_degraded: Option<DegradedCallback>,
    token: CancellationToken,
) {
    let mut consecutive_failures: u32 = 0;
    let mut degraded = false;
    loop {
        // Ping, racing it against cancellation so a hung ping can't block shutdown.
        tokio::select! {
            biased;
            _ = token.cancelled() => break,
            result = transport.health() => match result {
                Ok(()) => {
                    if degraded {
                        logger.info("health pings recovered");
                    }
                    consecutive_failures = 0;
                    degraded = false;
                }
                Err(cause) => {
                    consecutive_failures += 1;
                    logger.warn(&format!(
                        "health ping failed ({consecutive_failures} consecutive): {cause}"
                    ));
                    if consecutive_failures >= DEGRADED_THRESHOLD && !degraded {
                        degraded = true;
                        logger.error(
                            "health pings have been failing for a sustained period; the server may \
                             be marked unhealthy",
                        );
                        if let Some(callback) = &on_degraded {
                            callback();
                        }
                    }
                }
            }
        }
        // Sleep after the ping settles (no overlap), also cancellable.
        tokio::select! {
            _ = token.cancelled() => break,
            _ = tokio::time::sleep(interval) => {}
        }
    }
    logger.debug("health loop stopped");
}
