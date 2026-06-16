package gameflow

import "context"

// playersList is the list key the platform uses for player tracking.
const playersList = "players"

// watchSink receives each parsed game-server update from a watch stream.
type watchSink func(*rawGameServer)

// transport is the seam between the real GameFlow runtime and local mode.
// Everything above it (GameFlow, Players, the health loop, the watcher) is
// mode-agnostic.
type transport interface {
	ready(ctx context.Context) error
	health(ctx context.Context) error
	shutdown(ctx context.Context) error
	getGameServer(ctx context.Context) (*rawGameServer, error)
	getPlayerList(ctx context.Context) (playerListSnapshot, error)
	addPlayer(ctx context.Context, sessionID string) (playerListSnapshot, error)
	// removePlayer reports found=false (with no error) when the player was not in
	// the list, so disconnect is idempotent.
	removePlayer(ctx context.Context, sessionID string) (found bool, snap playerListSnapshot, err error)
	// watchGameServer streams updates into sink until ctx is cancelled or the
	// stream ends; it returns an error only on transport failure.
	watchGameServer(ctx context.Context, sink watchSink) error
}

// guardedTransport wraps a transport so every call (except shutdown, which
// performs the transition) fails loudly with NOT_CONNECTED once the SDK has been
// shut down.
type guardedTransport struct {
	inner      transport
	isShutdown func() bool
}

func (g *guardedTransport) guard() error {
	if g.isShutdown() {
		return notConnected()
	}
	return nil
}

func (g *guardedTransport) ready(ctx context.Context) error {
	if err := g.guard(); err != nil {
		return err
	}
	return g.inner.ready(ctx)
}

func (g *guardedTransport) health(ctx context.Context) error {
	if err := g.guard(); err != nil {
		return err
	}
	return g.inner.health(ctx)
}

func (g *guardedTransport) shutdown(ctx context.Context) error {
	// Unguarded: shutdown is what transitions the state.
	return g.inner.shutdown(ctx)
}

func (g *guardedTransport) getGameServer(ctx context.Context) (*rawGameServer, error) {
	if err := g.guard(); err != nil {
		return nil, err
	}
	return g.inner.getGameServer(ctx)
}

func (g *guardedTransport) getPlayerList(ctx context.Context) (playerListSnapshot, error) {
	if err := g.guard(); err != nil {
		return playerListSnapshot{}, err
	}
	return g.inner.getPlayerList(ctx)
}

func (g *guardedTransport) addPlayer(ctx context.Context, sessionID string) (playerListSnapshot, error) {
	if err := g.guard(); err != nil {
		return playerListSnapshot{}, err
	}
	return g.inner.addPlayer(ctx, sessionID)
}

func (g *guardedTransport) removePlayer(ctx context.Context, sessionID string) (bool, playerListSnapshot, error) {
	if err := g.guard(); err != nil {
		return false, playerListSnapshot{}, err
	}
	return g.inner.removePlayer(ctx, sessionID)
}

func (g *guardedTransport) watchGameServer(ctx context.Context, sink watchSink) error {
	if err := g.guard(); err != nil {
		return err
	}
	return g.inner.watchGameServer(ctx, sink)
}
