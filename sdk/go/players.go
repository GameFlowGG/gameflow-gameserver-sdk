package gameflow

import (
	"context"
	"errors"
	"sync"
)

// Players tracks connected player sessions over the platform's players list.
//
// Mutations always go to the runtime; the returned list is kept as an
// authoritative cache so [Players.Count], [Players.List] and [Players.Capacity]
// are synchronous and cheap. Players is safe for concurrent use.
type Players struct {
	transport transport

	mu   sync.Mutex
	snap playerListSnapshot
}

func newPlayers(t transport) *Players {
	return &Players{transport: t, snap: disabledSnapshot()}
}

// Connect registers a player session. Call it when a player joins the server.
//
// It returns an error whose [CodeOf] is [CodePlayerAlreadyConnected] on a
// duplicate id, [CodeServerFull] when the list is at capacity, and
// [CodePlayerTrackingDisabled] when tracking is off.
func (p *Players) Connect(ctx context.Context, sessionID string) error {
	snap, err := p.transport.addPlayer(ctx, sessionID)
	if err == nil {
		p.set(snap)
		return nil
	}
	var e *Error
	if !errors.As(err, &e) {
		return err
	}
	switch e.Code {
	case CodePlayerTrackingDisabled:
		p.set(disabledSnapshot())
		return err
	case CodeServerFull:
		// Enrich a wire ServerFull that carried no capacity with the cache.
		if !e.HasCapacity {
			return serverFull(p.Capacity(), true)
		}
		return err
	default:
		return err
	}
}

// Disconnect unregisters a player session. It returns found=false when the
// player was not in the list, so it is safe to call on every disconnect
// (including duplicates). When tracking is disabled it returns an error whose
// [CodeOf] is [CodePlayerTrackingDisabled].
func (p *Players) Disconnect(ctx context.Context, sessionID string) (bool, error) {
	p.mu.Lock()
	exists := p.snap.exists
	p.mu.Unlock()
	if !exists {
		return false, playerTrackingDisabled()
	}
	found, snap, err := p.transport.removePlayer(ctx, sessionID)
	if err != nil {
		return false, err
	}
	if !found {
		return false, nil
	}
	p.set(snap)
	return true, nil
}

// Count returns the current number of connected players (from the local cache).
func (p *Players) Count() int {
	p.mu.Lock()
	defer p.mu.Unlock()
	return len(p.snap.values)
}

// List returns the session ids of connected players (from the local cache).
func (p *Players) List() []string {
	p.mu.Lock()
	defer p.mu.Unlock()
	out := make([]string, len(p.snap.values))
	copy(out, p.snap.values)
	return out
}

// Capacity returns the maximum players configured for this game. It returns -1
// when there is no configured limit (local mode with GAMEFLOW_MAX_PLAYERS unset).
func (p *Players) Capacity() int64 {
	p.mu.Lock()
	defer p.mu.Unlock()
	return p.snap.capacity
}

// TrackingEnabled reports whether player tracking is on. It is false when the
// game was created with max players = 0.
func (p *Players) TrackingEnabled() bool {
	p.mu.Lock()
	defer p.mu.Unlock()
	return p.snap.exists
}

// Refresh re-reads the players list from the runtime.
func (p *Players) Refresh(ctx context.Context) error {
	snap, err := p.transport.getPlayerList(ctx)
	if err != nil {
		return err
	}
	p.set(snap)
	return nil
}

// syncFromGameServer updates the cache from a game-server object (connect seed
// and watch updates).
func (p *Players) syncFromGameServer(gs *rawGameServer) {
	var raw *rawList
	if gs != nil && gs.Status != nil {
		if list, ok := gs.Status.Lists[playersList]; ok {
			raw = &list
		}
	}
	p.set(parseList(raw))
}

func (p *Players) set(snap playerListSnapshot) {
	p.mu.Lock()
	p.snap = snap
	p.mu.Unlock()
}
