package gameflow

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"os/exec"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"
)

// This suite drives the full SDK against the shared fake-runtime fixture
// (tools/conformance/serve.mjs), the same fixture every GameFlow SDK is tested
// against. It requires Node on PATH and skips cleanly when it is absent.

func nodeAvailable() bool {
	return exec.Command("node", "--version").Run() == nil
}

type fixture struct {
	cmd         *exec.Cmd
	port        int
	controlPort int
}

func startFixture(t *testing.T, args ...string) *fixture {
	t.Helper()
	cmd := exec.Command("node", append([]string{"../../tools/conformance/serve.mjs"}, args...)...)
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		t.Fatalf("stdout pipe: %v", err)
	}
	cmd.Stderr = nil
	if err := cmd.Start(); err != nil {
		t.Fatalf("spawn fixture: %v", err)
	}

	f := &fixture{cmd: cmd}
	reader := bufio.NewReader(stdout)
	for f.port == 0 || f.controlPort == 0 {
		line, err := reader.ReadString('\n')
		if err != nil {
			t.Fatalf("fixture exited before announcing its ports: %v", err)
		}
		line = strings.TrimSpace(line)
		if v, ok := strings.CutPrefix(line, "PORT="); ok {
			f.port, _ = strconv.Atoi(v)
		} else if v, ok := strings.CutPrefix(line, "CONTROL_PORT="); ok {
			f.controlPort, _ = strconv.Atoi(v)
		}
	}
	// Drain the rest of stdout so the child never blocks on a full pipe.
	go func() { _, _ = io.Copy(io.Discard, stdout) }()

	t.Cleanup(func() {
		_ = cmd.Process.Kill()
		_ = cmd.Wait()
	})
	return f
}

func (f *fixture) controlPOST(t *testing.T, path string) {
	t.Helper()
	res, err := http.Post("http://127.0.0.1:"+strconv.Itoa(f.controlPort)+path, "", nil)
	if err != nil {
		t.Fatalf("control POST %s: %v", path, err)
	}
	drainClose(res)
}

func (f *fixture) countRequests(t *testing.T, path string) int {
	t.Helper()
	res, err := http.Get("http://127.0.0.1:" + strconv.Itoa(f.controlPort) + "/requests")
	if err != nil {
		t.Fatalf("control /requests: %v", err)
	}
	defer drainClose(res)
	var recorded []struct {
		Path string `json:"path"`
	}
	if err := json.NewDecoder(res.Body).Decode(&recorded); err != nil {
		t.Fatalf("decode /requests: %v", err)
	}
	n := 0
	for _, r := range recorded {
		if r.Path == path {
			n++
		}
	}
	return n
}

func TestConformanceAgainstFakeRuntime(t *testing.T) {
	if !nodeAvailable() {
		t.Skip("skipping conformance suite: Node not found on PATH")
	}

	// capacity 2, one seeded player, the first two requests fail (connect must retry).
	f := startFixture(t, "--players-capacity=2", "--players-seed=seeded", "--fail-first=2")
	ctx := context.Background()

	gf, err := Connect(ctx,
		WithMode(ModeAuto),
		WithHealthInterval(500*time.Millisecond),
		WithSilent(),
		withEnv(mapEnv(map[string]string{"AGONES_SDK_HTTP_PORT": strconv.Itoa(f.port)})),
	)
	if err != nil {
		t.Fatalf("connect (with retries): %v", err)
	}
	if gf.Mode() != ModeSidecar {
		t.Fatalf("mode = %s, want sidecar", gf.Mode())
	}

	// The successful probe seeds the players cache.
	if !gf.Players().TrackingEnabled() || gf.Players().Capacity() != 2 || gf.Players().Count() != 1 {
		t.Fatalf("seeded cache wrong: tracking=%v cap=%d count=%d",
			gf.Players().TrackingEnabled(), gf.Players().Capacity(), gf.Players().Count())
	}
	if list := gf.Players().List(); len(list) != 1 || list[0] != "seeded" {
		t.Fatalf("seeded list = %v", list)
	}

	if err := gf.Ready(ctx); err != nil {
		t.Fatalf("ready: %v", err)
	}

	// Watch + payload-change subscriptions.
	var mu sync.Mutex
	updates := 0
	watchSub, err := gf.Watch(func(ServerInfo) {
		mu.Lock()
		updates++
		mu.Unlock()
	})
	if err != nil {
		t.Fatalf("watch: %v", err)
	}
	var payloads []string
	payloadSub, err := gf.OnPayloadChange(func(payload string, present bool) {
		if present {
			mu.Lock()
			payloads = append(payloads, payload)
			mu.Unlock()
		}
	})
	if err != nil {
		t.Fatalf("on payload change: %v", err)
	}

	// Player tracking against the runtime.
	if err := gf.Players().Connect(ctx, "p1"); err != nil {
		t.Fatalf("connect p1: %v", err)
	}
	if gf.Players().Count() != 2 {
		t.Fatalf("count after p1 = %d, want 2", gf.Players().Count())
	}

	full := gf.Players().Connect(ctx, "p2")
	wantCode(t, full, CodeServerFull)
	var fullErr *Error
	if !errors.As(full, &fullErr) || !fullErr.HasCapacity || fullErr.Capacity != 2 {
		t.Fatalf("ServerFull should carry capacity 2, got %+v", fullErr)
	}

	wantCode(t, gf.Players().Connect(ctx, "p1"), CodePlayerAlreadyConnected)

	if found, err := gf.Players().Disconnect(ctx, "seeded"); err != nil || !found {
		t.Fatalf("disconnect seeded = (%v, %v)", found, err)
	}
	if found, err := gf.Players().Disconnect(ctx, "does-not-exist"); err != nil || found {
		t.Fatalf("idempotent disconnect = (%v, %v)", found, err)
	}
	if gf.Players().Count() != 1 {
		t.Fatalf("count = %d, want 1", gf.Players().Count())
	}

	// Watch fires on a pushed update.
	f.controlPOST(t, "/push-update")
	time.Sleep(100 * time.Millisecond)
	mu.Lock()
	gotUpdates := updates
	mu.Unlock()
	if gotUpdates < 1 {
		t.Fatalf("watch listener should have fired, updates=%d", gotUpdates)
	}

	// Payload change propagates through the watch stream.
	f.controlPOST(t, "/set-payload?value=match-7")
	time.Sleep(100 * time.Millisecond)
	payload, present, err := gf.Payload(ctx)
	if err != nil || !present || payload != "match-7" {
		t.Fatalf("payload = (%q, %v, %v), want match-7", payload, present, err)
	}
	mu.Lock()
	sawPayload := false
	for _, p := range payloads {
		if p == "match-7" {
			sawPayload = true
		}
	}
	mu.Unlock()
	if !sawPayload {
		t.Fatal("payload-change listener should have fired with match-7")
	}

	// Health heartbeat ticks at least twice.
	time.Sleep(1400 * time.Millisecond)

	watchSub.Unsubscribe()
	payloadSub.Unsubscribe()

	if err := gf.Shutdown(ctx); err != nil {
		t.Fatalf("shutdown: %v", err)
	}
	wantCode(t, gf.Players().Connect(ctx, "late"), CodeNotConnected)

	// Assert behavior only observable from the runtime side.
	if n := f.countRequests(t, "/ready"); n != 1 {
		t.Errorf("/ready posted %d times, want 1", n)
	}
	if n := f.countRequests(t, "/health"); n < 2 {
		t.Errorf("/health posted %d times, want >= 2", n)
	}
	if n := f.countRequests(t, "/shutdown"); n != 1 {
		t.Errorf("/shutdown posted %d times, want 1", n)
	}
	if n := f.countRequests(t, "/gameserver"); n < 3 {
		t.Errorf("/gameserver requested %d times, want >= 3 (connect retried)", n)
	}
}
