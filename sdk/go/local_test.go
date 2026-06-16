package gameflow

import (
	"context"
	"errors"
	"sync"
	"testing"
	"time"
)

func connectLocal(t *testing.T, env map[string]string) *GameFlow {
	t.Helper()
	gf, err := Connect(context.Background(), WithMode(ModeLocal), WithSilent(), withEnv(mapEnv(env)))
	if err != nil {
		t.Fatalf("connect local: %v", err)
	}
	return gf
}

func wantCode(t *testing.T, err error, code ErrorCode) {
	t.Helper()
	if err == nil {
		t.Fatalf("expected error with code %s, got nil", code)
	}
	if got := CodeOf(err); got != code {
		t.Fatalf("error code = %s, want %s (err: %v)", got, code, err)
	}
}

func TestConnectsInLocalModeWithUnlimitedTracking(t *testing.T) {
	gf := connectLocal(t, nil)
	if gf.Mode() != ModeLocal {
		t.Errorf("mode = %s, want local", gf.Mode())
	}
	if !gf.Players().TrackingEnabled() {
		t.Error("tracking should be enabled")
	}
	if cap := gf.Players().Capacity(); cap != -1 {
		t.Errorf("capacity = %d, want -1 (unlimited)", cap)
	}
	if c := gf.Players().Count(); c != 0 {
		t.Errorf("count = %d, want 0", c)
	}
	if err := gf.Ready(context.Background()); err != nil {
		t.Errorf("ready (no-op) should succeed: %v", err)
	}
}

func TestTracksPlayersAgainstInMemoryList(t *testing.T) {
	gf := connectLocal(t, nil)
	ctx := context.Background()
	for _, id := range []string{"a", "b", "c"} {
		if err := gf.Players().Connect(ctx, id); err != nil {
			t.Fatalf("connect %s: %v", id, err)
		}
	}
	if c := gf.Players().Count(); c != 3 {
		t.Errorf("count = %d, want 3", c)
	}
	if list := gf.Players().List(); len(list) != 3 || list[0] != "a" || list[2] != "c" {
		t.Errorf("list = %v", list)
	}

	wantCode(t, gf.Players().Connect(ctx, "b"), CodePlayerAlreadyConnected)

	if found, err := gf.Players().Disconnect(ctx, "a"); err != nil || !found {
		t.Errorf("disconnect a = (%v, %v), want (true, nil)", found, err)
	}
	if found, err := gf.Players().Disconnect(ctx, "a"); err != nil || found {
		t.Errorf("idempotent disconnect a = (%v, %v), want (false, nil)", found, err)
	}
	if c := gf.Players().Count(); c != 2 {
		t.Errorf("count = %d, want 2", c)
	}
}

func TestEnforcesCapacity(t *testing.T) {
	gf := connectLocal(t, map[string]string{"GAMEFLOW_MAX_PLAYERS": "2"})
	ctx := context.Background()
	if gf.Players().Capacity() != 2 {
		t.Fatalf("capacity = %d, want 2", gf.Players().Capacity())
	}
	if err := gf.Players().Connect(ctx, "a"); err != nil {
		t.Fatal(err)
	}
	if err := gf.Players().Connect(ctx, "b"); err != nil {
		t.Fatal(err)
	}
	err := gf.Players().Connect(ctx, "c")
	wantCode(t, err, CodeServerFull)
	var e *Error
	if errors.As(err, &e); !e.HasCapacity || e.Capacity != 2 {
		t.Errorf("ServerFull capacity = (%d, %v), want (2, true)", e.Capacity, e.HasCapacity)
	}
	if !errors.Is(err, ErrServerFull) {
		t.Error("errors.Is(err, ErrServerFull) should be true")
	}
}

func TestTrackingDisabledWhenMaxPlayersIsZero(t *testing.T) {
	gf := connectLocal(t, map[string]string{"GAMEFLOW_MAX_PLAYERS": "0"})
	ctx := context.Background()
	if gf.Players().TrackingEnabled() {
		t.Error("tracking should be disabled")
	}
	if gf.Players().Capacity() != 0 {
		t.Errorf("capacity = %d, want 0", gf.Players().Capacity())
	}
	wantCode(t, gf.Players().Connect(ctx, "a"), CodePlayerTrackingDisabled)
	_, err := gf.Players().Disconnect(ctx, "a")
	wantCode(t, err, CodePlayerTrackingDisabled)
}

func TestMalformedMaxPlayersDisablesTracking(t *testing.T) {
	for _, raw := range []string{"-5", "abc"} {
		gf := connectLocal(t, map[string]string{"GAMEFLOW_MAX_PLAYERS": raw})
		if gf.Players().TrackingEnabled() {
			t.Errorf("max_players=%q should disable tracking", raw)
		}
		wantCode(t, gf.Players().Connect(context.Background(), "a"), CodePlayerTrackingDisabled)
	}
}

func TestExposesLaunchPayload(t *testing.T) {
	gf := connectLocal(t, map[string]string{"GAMEFLOW_PAYLOAD": `{"match":"test-1"}`})
	payload, present, err := gf.Payload(context.Background())
	if err != nil || !present || payload != `{"match":"test-1"}` {
		t.Errorf("payload = (%q, %v, %v)", payload, present, err)
	}

	gfNone := connectLocal(t, nil)
	if payload, present, err := gfNone.Payload(context.Background()); err != nil || present || payload != "" {
		t.Errorf("absent payload = (%q, %v, %v), want (\"\", false, nil)", payload, present, err)
	}
}

func TestExposesServerInfo(t *testing.T) {
	gf := connectLocal(t, nil)
	info, err := gf.Info(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if info.Name != "local-gameserver" || info.Address != "127.0.0.1" {
		t.Errorf("info = %+v", info)
	}
}

func TestReadsPortsAndMetadataFromEnv(t *testing.T) {
	gf := connectLocal(t, map[string]string{
		"GAMEFLOW_DEFAULT_PORT": "7777",
		"GAMEFLOW_REGION":       "eu-west-1",
		"GAMEFLOW_BUILD_ID":     "build-42",
	})
	if got, ok := gf.Ports().Default(); !ok || got != 7777 {
		t.Errorf("default port = (%d, %v)", got, ok)
	}
	if got, ok := gf.Region(); !ok || got != "eu-west-1" {
		t.Errorf("region = (%q, %v)", got, ok)
	}
	if got, ok := gf.BuildID(); !ok || got != "build-42" {
		t.Errorf("buildID = (%q, %v)", got, ok)
	}
}

func TestWatchEmitsSyntheticUpdates(t *testing.T) {
	gf := connectLocal(t, nil)
	var mu sync.Mutex
	var states []string
	sub, err := gf.Watch(func(info ServerInfo) {
		mu.Lock()
		states = append(states, info.State)
		mu.Unlock()
	})
	if err != nil {
		t.Fatal(err)
	}
	defer sub.Unsubscribe()

	// Let the lazily-opened stream deliver the initial state, then mutate.
	time.Sleep(50 * time.Millisecond)
	if err := gf.Ready(context.Background()); err != nil {
		t.Fatal(err)
	}
	time.Sleep(50 * time.Millisecond)

	mu.Lock()
	defer mu.Unlock()
	saw := false
	for _, s := range states {
		if s == "Ready" {
			saw = true
		}
	}
	if !saw {
		t.Errorf("expected a Ready update, saw %v", states)
	}
}

func TestShutdownIsIdempotentAndDisablesFurtherCalls(t *testing.T) {
	gf := connectLocal(t, nil)
	ctx := context.Background()
	if err := gf.Ready(ctx); err != nil {
		t.Fatal(err)
	}
	if err := gf.Shutdown(ctx); err != nil {
		t.Fatal(err)
	}
	if err := gf.Shutdown(ctx); err != nil {
		t.Fatalf("shutdown should be idempotent: %v", err)
	}

	wantCode(t, gf.Ready(ctx), CodeNotConnected)
	_, infoErr := gf.Info(ctx)
	wantCode(t, infoErr, CodeNotConnected)
	wantCode(t, gf.Players().Connect(ctx, "a"), CodeNotConnected)
}

func TestExplicitModeOptionOverridesEnv(t *testing.T) {
	// AGONES port is set, but the explicit Local option must win.
	gf := connectLocal(t, map[string]string{"AGONES_SDK_HTTP_PORT": "9999"})
	if gf.Mode() != ModeLocal {
		t.Errorf("mode = %s, want local", gf.Mode())
	}
}
