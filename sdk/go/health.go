package gameflow

import (
	"context"
	"fmt"
	"sync"
	"time"
)

// degradedThreshold is the number of consecutive failures before reporting
// degraded health.
const degradedThreshold = 6

// healthLoop runs the automatic health heartbeat. It starts when Ready succeeds
// (sidecar mode only). A self-scheduling loop pings, waits for the ping to
// settle, then sleeps — so pings never overlap or pile up. Failures are
// tolerated (logged, no backoff); after a sustained run of failures it reports
// degraded health once and recovers silently.
type healthLoop struct {
	transport      transport
	interval       time.Duration
	requestTimeout time.Duration
	logger         Logger
	onDegraded     func()

	mu     sync.Mutex
	cancel context.CancelFunc
	done   chan struct{}
}

func newHealthLoop(t transport, interval, requestTimeout time.Duration, logger Logger, onDegraded func()) *healthLoop {
	return &healthLoop{
		transport:      t,
		interval:       interval,
		requestTimeout: requestTimeout,
		logger:         logger,
		onDegraded:     onDegraded,
	}
}

func (h *healthLoop) start() {
	h.mu.Lock()
	defer h.mu.Unlock()
	if h.cancel != nil {
		return // already running
	}
	ctx, cancel := context.WithCancel(context.Background())
	h.cancel = cancel
	h.done = make(chan struct{})
	go h.run(ctx, h.done)
}

func (h *healthLoop) stop() {
	h.mu.Lock()
	cancel := h.cancel
	done := h.done
	h.cancel = nil
	h.done = nil
	h.mu.Unlock()
	if cancel != nil {
		cancel()
		<-done // wait for the loop to exit so it never outlives shutdown
	}
}

func (h *healthLoop) run(ctx context.Context, done chan struct{}) {
	defer close(done)
	// A panic in the loop (e.g. from a user onDegraded callback) must never take
	// down the host process.
	defer func() {
		if r := recover(); r != nil {
			h.logger.Error(fmt.Sprintf("health loop panicked: %v", r))
		}
	}()

	timer := time.NewTimer(h.interval)
	defer timer.Stop()

	consecutiveFailures := 0
	degraded := false
	for {
		h.ping(ctx, &consecutiveFailures, &degraded)
		// Sleep after the ping settles (no overlap), cancellable.
		if !timer.Stop() {
			select {
			case <-timer.C:
			default:
			}
		}
		timer.Reset(h.interval)
		select {
		case <-ctx.Done():
			h.logger.Debug("health loop stopped")
			return
		case <-timer.C:
		}
	}
}

func (h *healthLoop) ping(ctx context.Context, consecutiveFailures *int, degraded *bool) {
	pingCtx, cancel := context.WithTimeout(ctx, h.requestTimeout)
	defer cancel()
	err := h.transport.health(pingCtx)
	if ctx.Err() != nil {
		return // cancelled during the ping; the loop will exit
	}
	if err == nil {
		if *degraded {
			h.logger.Info("health pings recovered")
		}
		*consecutiveFailures = 0
		*degraded = false
		return
	}
	*consecutiveFailures++
	h.logger.Warn(fmt.Sprintf("health ping failed (%d consecutive): %v", *consecutiveFailures, err))
	if *consecutiveFailures >= degradedThreshold && !*degraded {
		*degraded = true
		h.logger.Error(
			"health pings have been failing for a sustained period; the server may be marked unhealthy",
		)
		if h.onDegraded != nil {
			h.onDegraded()
		}
	}
}
