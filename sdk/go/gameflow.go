package gameflow

import (
	"context"
	"fmt"
	"math/rand"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

const (
	payloadAnnotation          = "GAMEFLOW_PAYLOAD"
	defaultSidecarPort  uint16 = 9358
	minHealthInterval          = 500 * time.Millisecond
	initialProbeBackoff        = 250 * time.Millisecond
	maxProbeBackoff            = 4000 * time.Millisecond
)

// Lifecycle state, ordered so guards can compare with >=.
const (
	stateConnected int32 = iota
	stateReady
	stateShuttingDown
	stateShutdown
)

// GameFlow is the game-server SDK handle. Create one with [Connect], then drive
// the server lifecycle and player tracking through it. It is safe for concurrent
// use across goroutines.
type GameFlow struct {
	mode           Mode
	logger         Logger
	transport      transport
	players        *Players
	watcher        *watcher
	health         *healthLoop
	env            envSource
	requestTimeout time.Duration

	state      atomic.Int32
	baseCancel context.CancelFunc

	payloadMu          sync.Mutex
	payloadListeners   map[uint64]func(payload string, present bool)
	payloadNextID      uint64
	lastPayload        string
	lastPayloadPresent bool
}

// Connect connects to the GameFlow runtime (or local mode off-platform) and
// returns a ready-to-use handle. It probes the runtime with retries until it
// responds or the connect timeout elapses; pass [WithConnectTimeout] to change
// the budget and ctx to cancel early.
func Connect(ctx context.Context, opts ...Option) (*GameFlow, error) {
	o := defaultOptions()
	for _, opt := range opts {
		opt(&o)
	}
	if o.logger == nil {
		o.logger = nopLogger{}
	}
	if o.env == nil {
		o.env = osEnv
	}

	mode := resolveMode(o.mode, o.env, o.logger)
	baseCtx, baseCancel := context.WithCancel(context.Background())

	g := &GameFlow{
		mode:             mode,
		logger:           o.logger,
		env:              o.env,
		requestTimeout:   o.requestTimeout,
		baseCancel:       baseCancel,
		payloadListeners: make(map[uint64]func(string, bool)),
	}

	var raw transport
	switch mode {
	case ModeLocal:
		raw = newLocalTransport(o.logger, o.env)
	default: // ModeSidecar
		port := sidecarPort(o.env)
		raw = newSidecarTransport(fmt.Sprintf("http://127.0.0.1:%d", port), o.requestTimeout, o.logger)
	}
	g.transport = &guardedTransport{
		inner:      raw,
		isShutdown: func() bool { return g.state.Load() >= stateShutdown },
	}
	g.players = newPlayers(g.transport)
	g.watcher = newWatcher(baseCtx, g.transport, o.logger)
	interval := max(o.healthInterval, minHealthInterval)
	g.health = newHealthLoop(g.transport, interval, o.requestTimeout, o.logger, o.onHealthDegraded)
	g.watcher.setOnUpdate(g.handleUpdate)

	if err := g.init(ctx, o.connectTimeout); err != nil {
		baseCancel()
		return nil, err
	}
	return g, nil
}

func (g *GameFlow) init(ctx context.Context, connectTimeout time.Duration) error {
	gs, err := g.probeWithRetry(ctx, connectTimeout)
	if err != nil {
		return err
	}
	g.players.syncFromGameServer(gs)
	payload, present := payloadOf(gs)
	g.payloadMu.Lock()
	g.lastPayload, g.lastPayloadPresent = payload, present
	g.payloadMu.Unlock()
	if g.mode == ModeSidecar && !g.players.TrackingEnabled() {
		g.logger.Warn("player tracking is disabled for this server (max players is 0). The platform " +
			"cannot see player counts and idle servers with no trackable players may be shut down. Set " +
			"\"Max Players per Server\" in your game settings to enable tracking.")
	}
	g.logger.Info(fmt.Sprintf("connected (mode: %s)", g.mode))
	return nil
}

func (g *GameFlow) probeWithRetry(ctx context.Context, connectTimeout time.Duration) (*rawGameServer, error) {
	deadline := time.Now().Add(connectTimeout)
	backoff := initialProbeBackoff
	for {
		gs, err := g.transport.getGameServer(ctx)
		if err == nil {
			return gs, nil
		}
		wait := jitter(backoff)
		if !time.Now().Add(wait).Before(deadline) {
			return nil, sidecarUnavailable(
				fmt.Sprintf("could not connect to the GameFlow runtime within %dms", connectTimeout.Milliseconds()),
				err,
			)
		}
		g.logger.Debug(fmt.Sprintf("runtime not reachable yet; retrying in %dms", wait.Milliseconds()))
		timer := time.NewTimer(wait)
		select {
		case <-ctx.Done():
			timer.Stop()
			return nil, sidecarUnavailable(
				"connect cancelled before the GameFlow runtime became reachable", ctx.Err())
		case <-timer.C:
		}
		backoff = min(backoff*2, maxProbeBackoff)
	}
}

// Ready marks the server ready to accept players and starts the automatic health
// heartbeat. Call it once your server is listening. It is idempotent.
func (g *GameFlow) Ready(ctx context.Context) error {
	if err := g.assertUsable(); err != nil {
		return err
	}
	if g.state.Load() == stateReady {
		g.logger.Debug("ready() called more than once; ignoring")
		return nil
	}
	if err := g.transport.ready(ctx); err != nil {
		return err
	}
	// Transition CONNECTED -> READY atomically so a shutdown that raced the
	// request above can't be clobbered back to READY (which would start a zombie
	// health loop after shutdown).
	if !g.state.CompareAndSwap(stateConnected, stateReady) {
		if g.state.Load() == stateReady {
			return nil
		}
		return notConnected()
	}
	if g.mode == ModeSidecar {
		g.health.start()
	}
	g.logger.Info("server marked ready")
	return nil
}

// Shutdown shuts the server down cleanly. It is idempotent: repeated calls return
// immediately and only one request is sent. After it returns every other method
// fails with [CodeNotConnected].
func (g *GameFlow) Shutdown(ctx context.Context) error {
	// Claim the shutdown exactly once.
	for {
		cur := g.state.Load()
		if cur >= stateShuttingDown {
			return nil
		}
		if g.state.CompareAndSwap(cur, stateShuttingDown) {
			break
		}
	}
	// Stop the health loop and watch stream first, then send shutdown.
	g.health.stop()
	g.watcher.stop()
	if err := g.transport.shutdown(ctx); err != nil {
		g.logger.Warn(fmt.Sprintf("shutdown request failed: %v", err))
	}
	g.baseCancel()
	g.state.Store(stateShutdown)
	g.logger.Info("server shut down")
	return nil
}

// Payload returns the launch payload for this server (an opaque string), whether
// one is present, and any error. It may change when the server is assigned to a
// new match; use [GameFlow.OnPayloadChange] to react.
func (g *GameFlow) Payload(ctx context.Context) (payload string, present bool, err error) {
	if err := g.assertUsable(); err != nil {
		return "", false, err
	}
	gs, err := g.transport.getGameServer(ctx)
	if err != nil {
		return "", false, err
	}
	payload, present = payloadOf(gs)
	g.payloadMu.Lock()
	g.lastPayload, g.lastPayloadPresent = payload, present
	g.payloadMu.Unlock()
	return payload, present, nil
}

// Info returns the current server details (name, state, address, ports).
func (g *GameFlow) Info(ctx context.Context) (ServerInfo, error) {
	if err := g.assertUsable(); err != nil {
		return ServerInfo{}, err
	}
	gs, err := g.transport.getGameServer(ctx)
	if err != nil {
		return ServerInfo{}, err
	}
	return toInfo(gs), nil
}

// Watch subscribes to server updates. The stream is shared across subscribers
// and reconnects automatically. Call [Subscription.Unsubscribe] to stop.
func (g *GameFlow) Watch(listener func(ServerInfo)) (*Subscription, error) {
	if err := g.assertUsable(); err != nil {
		return nil, err
	}
	return g.watcher.subscribe(func(gs *rawGameServer) { listener(toInfo(gs)) }), nil
}

// OnPayloadChange registers a listener that fires when the launch payload changes
// (e.g. on match assignment). Call [Subscription.Unsubscribe] to stop.
func (g *GameFlow) OnPayloadChange(listener func(payload string, present bool)) (*Subscription, error) {
	if err := g.assertUsable(); err != nil {
		return nil, err
	}
	g.payloadMu.Lock()
	id := g.payloadNextID
	g.payloadNextID++
	g.payloadListeners[id] = listener
	g.payloadMu.Unlock()

	// Keep the stream open with a no-op watch subscription.
	watchSub := g.watcher.subscribe(func(*rawGameServer) {})
	return &Subscription{unsub: func() {
		g.payloadMu.Lock()
		delete(g.payloadListeners, id)
		g.payloadMu.Unlock()
		watchSub.Unsubscribe()
	}}, nil
}

// Players returns the player tracking handle.
func (g *GameFlow) Players() *Players { return g.players }

// Ports returns the network ports assigned to this server.
func (g *GameFlow) Ports() Ports { return Ports{env: g.env} }

// Region returns the region this server runs in, when provided by the platform.
func (g *GameFlow) Region() (string, bool) { return g.env.get("GAMEFLOW_REGION") }

// BuildID returns the build id of the running image, when provided by the
// platform.
func (g *GameFlow) BuildID() (string, bool) { return g.env.get("GAMEFLOW_BUILD_ID") }

// Mode reports the resolved transport: [ModeSidecar] on GameFlow, [ModeLocal]
// otherwise.
func (g *GameFlow) Mode() Mode { return g.mode }

func (g *GameFlow) assertUsable() error {
	if g.state.Load() >= stateShuttingDown {
		return notConnected()
	}
	return nil
}

// handleUpdate is the watcher's cache-sync hook: it syncs the players cache and
// fires payload-change listeners when the payload actually differs.
func (g *GameFlow) handleUpdate(gs *rawGameServer) {
	g.players.syncFromGameServer(gs)
	payload, present := payloadOf(gs)
	g.payloadMu.Lock()
	if payload == g.lastPayload && present == g.lastPayloadPresent {
		g.payloadMu.Unlock()
		return
	}
	g.lastPayload, g.lastPayloadPresent = payload, present
	listeners := make([]func(string, bool), 0, len(g.payloadListeners))
	for _, l := range g.payloadListeners {
		listeners = append(listeners, l)
	}
	g.payloadMu.Unlock()
	for _, l := range listeners {
		l(payload, present)
	}
}

func resolveMode(option Mode, env envSource, logger Logger) Mode {
	switch option {
	case ModeSidecar:
		return ModeSidecar
	case ModeLocal:
		return ModeLocal
	}
	switch env.getOr("GAMEFLOW_SDK_MODE") {
	case "sidecar":
		return ModeSidecar
	case "local":
		return ModeLocal
	}
	// Any non-empty value selects sidecar (mirrors the TS truthiness check): a
	// real pod with a malformed port must still fail hard rather than silently
	// fall back to local mode.
	if p, ok := env.get("AGONES_SDK_HTTP_PORT"); ok && p != "" {
		return ModeSidecar
	}
	logger.Info("no GameFlow runtime detected; running in local mode (lifecycle and player tracking are simulated)")
	return ModeLocal
}

// sidecarPort resolves the sidecar port from AGONES_SDK_HTTP_PORT, falling back
// to the default for unset/empty/zero/invalid values.
func sidecarPort(env envSource) uint16 {
	raw := strings.TrimSpace(env.getOr("AGONES_SDK_HTTP_PORT"))
	if n, err := strconv.ParseUint(raw, 10, 16); err == nil && n > 0 {
		return uint16(n)
	}
	return defaultSidecarPort
}

// jitter applies +/-20% jitter to a backoff duration.
func jitter(d time.Duration) time.Duration {
	factor := 0.8 + rand.Float64()*0.4 // 0.8..1.2
	return time.Duration(float64(d) * factor)
}
