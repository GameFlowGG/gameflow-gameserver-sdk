package gameflow

import (
	"context"
	"fmt"
	"sync"
	"sync/atomic"
	"time"
)

const (
	watchInitialBackoff = 250 * time.Millisecond
	watchMaxBackoff     = 5000 * time.Millisecond
)

// Subscription is a handle to a watch or payload-change subscription. Call
// [Subscription.Unsubscribe] to stop receiving updates; it is safe to call more
// than once and from any goroutine.
type Subscription struct {
	once  sync.Once
	unsub func()
}

// Unsubscribe stops the subscription. After it returns the listener will not be
// called again.
func (s *Subscription) Unsubscribe() {
	if s == nil {
		return
	}
	s.once.Do(func() {
		if s.unsub != nil {
			s.unsub()
		}
	})
}

type watchListener func(*rawGameServer)

// watcher multiplexes a single underlying watch stream across all subscribers.
// The stream is opened lazily on the first subscriber, shared by all, kept alive
// with reconnect + backoff, and closed when the last subscriber unsubscribes (or
// on shutdown).
type watcher struct {
	transport transport
	logger    Logger
	baseCtx   context.Context

	mu        sync.Mutex
	listeners map[uint64]watchListener
	latest    *rawGameServer
	onUpdate  watchListener
	nextID    uint64

	streamCancel context.CancelFunc
	stopped      bool
}

func newWatcher(baseCtx context.Context, t transport, logger Logger) *watcher {
	return &watcher{
		transport: t,
		logger:    logger,
		baseCtx:   baseCtx,
		listeners: make(map[uint64]watchListener),
	}
}

// setOnUpdate installs the internal cache-sync hook, invoked before user
// listeners on every update.
func (w *watcher) setOnUpdate(hook watchListener) {
	w.mu.Lock()
	w.onUpdate = hook
	w.mu.Unlock()
}

// subscribe adds a listener and returns a handle that unsubscribes on
// [Subscription.Unsubscribe].
func (w *watcher) subscribe(listener watchListener) *Subscription {
	w.mu.Lock()
	id := w.nextID
	w.nextID++
	w.listeners[id] = listener
	latest := w.latest
	w.mu.Unlock()

	// Replay the latest known state to the newcomer, outside the lock.
	if latest != nil {
		listener(latest)
	}
	w.ensureStream()

	return &Subscription{unsub: func() {
		w.mu.Lock()
		delete(w.listeners, id)
		empty := len(w.listeners) == 0
		w.mu.Unlock()
		if empty {
			w.closeStream()
		}
	}}
}

// stop ends the stream permanently and drops all listeners (shutdown).
func (w *watcher) stop() {
	w.mu.Lock()
	w.stopped = true
	if w.streamCancel != nil {
		w.streamCancel()
		w.streamCancel = nil
	}
	w.listeners = make(map[uint64]watchListener)
	w.mu.Unlock()
}

func (w *watcher) ensureStream() {
	w.mu.Lock()
	if w.stopped || w.streamCancel != nil {
		w.mu.Unlock()
		return
	}
	ctx, cancel := context.WithCancel(w.baseCtx)
	w.streamCancel = cancel
	w.mu.Unlock()
	go w.run(ctx)
}

func (w *watcher) closeStream() {
	w.mu.Lock()
	if w.streamCancel != nil {
		w.streamCancel()
		w.streamCancel = nil
	}
	w.mu.Unlock()
}

func (w *watcher) run(ctx context.Context) {
	backoff := watchInitialBackoff
	for ctx.Err() == nil {
		var gotMessage atomic.Bool
		sink := func(gs *rawGameServer) {
			gotMessage.Store(true)
			w.dispatch(gs)
		}

		err := w.transport.watchGameServer(ctx, sink)
		if ctx.Err() != nil {
			return
		}
		if err != nil {
			w.logger.Warn(fmt.Sprintf("watch stream failed (retrying in %dms): %v", backoff.Milliseconds(), err))
		} else {
			w.logger.Debug("watch stream ended; reconnecting")
		}

		// Reset backoff after a stream that delivered at least one message.
		if gotMessage.Load() {
			backoff = watchInitialBackoff
		}
		select {
		case <-ctx.Done():
			return
		case <-time.After(backoff):
		}
		backoff = min(backoff*2, watchMaxBackoff)
	}
}

func (w *watcher) dispatch(gs *rawGameServer) {
	w.mu.Lock()
	w.latest = gs
	hook := w.onUpdate
	listeners := make([]watchListener, 0, len(w.listeners))
	for _, l := range w.listeners {
		listeners = append(listeners, l)
	}
	w.mu.Unlock()

	// Invoke the hook + listeners outside the lock so user code never runs while
	// the lock is held.
	if hook != nil {
		hook(gs)
	}
	for _, l := range listeners {
		l(gs)
	}
}
