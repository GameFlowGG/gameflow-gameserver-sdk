package gameflow

import (
	"context"
	"slices"
	"strconv"
	"strings"
	"sync"
)

// unlimited is the capacity sentinel meaning "no configured limit".
const unlimited int64 = -1

// localTransport is an in-memory transport for local development (no GameFlow
// runtime involved). Lifecycle calls are no-ops; player tracking runs against an
// in-memory list so the rest of the SDK behaves exactly as in production.
// Simulation knobs come from env vars: GAMEFLOW_MAX_PLAYERS (unset = unlimited,
// 0 = tracking disabled) and GAMEFLOW_PAYLOAD (the launch payload).
type localTransport struct {
	logger         Logger
	capacity       int64
	payload        string
	payloadPresent bool

	mu       sync.Mutex
	players  []string
	state    string
	watchers map[uint64]watchSink
	nextID   uint64
}

func newLocalTransport(logger Logger, env envSource) *localTransport {
	capacity := localCapacity(env)
	payload, payloadPresent := env.get("GAMEFLOW_PAYLOAD")
	return &localTransport{
		logger:         logger,
		capacity:       capacity,
		payload:        payload,
		payloadPresent: payloadPresent,
		players:        nil,
		state:          "Scheduled",
		watchers:       make(map[uint64]watchSink),
	}
}

// localCapacity resolves the simulated capacity from GAMEFLOW_MAX_PLAYERS. A
// positive value caps the list; zero, negative or malformed values disable
// tracking (mirroring the TS reference, where NaN > 0 and n <= 0 are both
// falsy); unset/empty means unlimited.
func localCapacity(env envSource) int64 {
	raw, ok := env.get("GAMEFLOW_MAX_PLAYERS")
	if !ok || strings.TrimSpace(raw) == "" {
		return unlimited
	}
	n, err := strconv.ParseInt(strings.TrimSpace(raw), 10, 64)
	if err != nil || n <= 0 {
		return 0
	}
	return n
}

func (l *localTransport) trackingEnabled() bool { return l.capacity != 0 }

func (l *localTransport) isFull(count int) bool {
	return l.capacity >= 0 && int64(count) >= l.capacity
}

// snapshotLocked builds a snapshot from the current players (caller holds mu).
func (l *localTransport) snapshotLocked() playerListSnapshot {
	if !l.trackingEnabled() {
		return disabledSnapshot()
	}
	values := make([]string, len(l.players))
	copy(values, l.players)
	return playerListSnapshot{exists: true, capacity: l.capacity, values: values}
}

// buildGameServerLocked builds the synthetic game server object (caller holds mu).
func (l *localTransport) buildGameServerLocked() *rawGameServer {
	annotations := map[string]string{}
	if l.payloadPresent {
		annotations[payloadAnnotation] = l.payload
	}
	status := &rawStatus{
		State:   l.state,
		Address: "127.0.0.1",
		Ports:   nil,
		Lists:   map[string]rawList{},
	}
	if l.trackingEnabled() {
		values := make([]string, len(l.players))
		copy(values, l.players)
		status.Lists[playersList] = rawList{Capacity: flexInt64(l.capacity), Values: values}
	}
	return &rawGameServer{
		ObjectMeta: &rawObjectMeta{
			Name:        "local-gameserver",
			Annotations: annotations,
			Labels:      map[string]string{},
		},
		Status: status,
	}
}

// snapshotSinksLocked collects the watch sinks to notify after the lock is
// released (caller holds mu). User listeners must never run while mu is held:
// the mutex is not reentrant (a listener that re-enters the transport would
// deadlock).
func (l *localTransport) snapshotSinksLocked() []watchSink {
	sinks := make([]watchSink, 0, len(l.watchers))
	for _, s := range l.watchers {
		sinks = append(sinks, s)
	}
	return sinks
}

func dispatchSinks(gs *rawGameServer, sinks []watchSink) {
	for _, s := range sinks {
		s(gs)
	}
}

func (l *localTransport) ready(context.Context) error {
	l.logger.Debug("local: ready()")
	l.mu.Lock()
	l.state = "Ready"
	gs := l.buildGameServerLocked()
	sinks := l.snapshotSinksLocked()
	l.mu.Unlock()
	dispatchSinks(gs, sinks)
	return nil
}

func (l *localTransport) health(context.Context) error {
	l.logger.Debug("local: health ping")
	return nil
}

func (l *localTransport) shutdown(context.Context) error {
	l.logger.Debug("local: shutdown()")
	l.mu.Lock()
	l.state = "Shutdown"
	gs := l.buildGameServerLocked()
	sinks := l.snapshotSinksLocked()
	l.mu.Unlock()
	dispatchSinks(gs, sinks)
	return nil
}

func (l *localTransport) getGameServer(context.Context) (*rawGameServer, error) {
	l.mu.Lock()
	defer l.mu.Unlock()
	return l.buildGameServerLocked(), nil
}

func (l *localTransport) getPlayerList(context.Context) (playerListSnapshot, error) {
	l.mu.Lock()
	defer l.mu.Unlock()
	return l.snapshotLocked(), nil
}

func (l *localTransport) addPlayer(_ context.Context, sessionID string) (playerListSnapshot, error) {
	l.mu.Lock()
	if !l.trackingEnabled() {
		l.mu.Unlock()
		return playerListSnapshot{}, playerTrackingDisabled()
	}
	if slices.Contains(l.players, sessionID) {
		l.mu.Unlock()
		return playerListSnapshot{}, playerAlreadyConnected(sessionID)
	}
	if l.isFull(len(l.players)) {
		l.mu.Unlock()
		return playerListSnapshot{}, serverFull(l.capacity, true)
	}
	l.players = append(l.players, sessionID)
	snap := l.snapshotLocked()
	gs := l.buildGameServerLocked()
	sinks := l.snapshotSinksLocked()
	l.mu.Unlock()
	dispatchSinks(gs, sinks)
	return snap, nil
}

func (l *localTransport) removePlayer(_ context.Context, sessionID string) (bool, playerListSnapshot, error) {
	l.mu.Lock()
	if !l.trackingEnabled() {
		l.mu.Unlock()
		return false, playerListSnapshot{}, nil
	}
	index := slices.Index(l.players, sessionID)
	if index < 0 {
		l.mu.Unlock()
		return false, playerListSnapshot{}, nil
	}
	l.players = append(l.players[:index], l.players[index+1:]...)
	snap := l.snapshotLocked()
	gs := l.buildGameServerLocked()
	sinks := l.snapshotSinksLocked()
	l.mu.Unlock()
	dispatchSinks(gs, sinks)
	return true, snap, nil
}

func (l *localTransport) watchGameServer(ctx context.Context, sink watchSink) error {
	l.mu.Lock()
	id := l.nextID
	l.nextID++
	l.watchers[id] = sink
	initial := l.buildGameServerLocked()
	l.mu.Unlock()

	// Emit the current state immediately (outside the lock), like a fresh
	// sidecar stream.
	sink(initial)

	<-ctx.Done()
	l.mu.Lock()
	delete(l.watchers, id)
	l.mu.Unlock()
	return nil
}
