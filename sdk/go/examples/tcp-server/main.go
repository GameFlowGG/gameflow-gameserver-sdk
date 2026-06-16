// A minimal GameFlow game server: a TCP line-chat built only on the standard
// library and the GameFlow SDK. The Go twin of the TypeScript node-tcp example.
//
// Every TCP connection is one tracked player, so the player count climbs in the
// GameFlow dashboard as clients join. The same binary runs on GameFlow (sidecar
// mode, auto-detected) and on your machine (local mode, zero config).
//
// Run it locally, then connect with `nc 127.0.0.1 7777` and type lines.
// Commands: /who lists players, /quit disconnects.
//
//	GAMEFLOW_MAX_PLAYERS=10 go run ./examples/tcp-server
package main

import (
	"bufio"
	"context"
	"errors"
	"fmt"
	"log"
	"net"
	"os/signal"
	"strconv"
	"sync"
	"sync/atomic"
	"syscall"

	gameflow "github.com/GameFlowGG/gameflow-gameserver-sdk/sdk/go"
)

var nextSession atomic.Uint64

func main() {
	// Stop on the first OS shutdown signal. GameFlow stops servers with SIGTERM;
	// locally you'll usually hit Ctrl-C (SIGINT).
	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGTERM, syscall.SIGINT)
	defer stop()

	gf, err := gameflow.Connect(ctx)
	if err != nil {
		log.Fatalf("gameflow: connect failed: %v", err)
	}
	region, _ := gf.Region()
	build, _ := gf.BuildID()
	log.Printf("gameflow: mode=%s region=%s build=%s", gf.Mode(), orDash(region), orDash(build))

	if payload, present, err := gf.Payload(ctx); err == nil && present {
		log.Printf("gameflow: launch payload: %s", payload)
	}
	sub, _ := gf.OnPayloadChange(func(payload string, present bool) {
		if present {
			log.Printf("gameflow: payload changed: %s", payload)
		} else {
			log.Printf("gameflow: payload cleared")
		}
	})
	defer sub.Unsubscribe()

	// Listen on the platform-assigned port, bound to 0.0.0.0 so the server is
	// reachable from outside the container.
	port := uint16(7777)
	if p, ok := gf.Ports().Default(); ok {
		port = p
	}
	listener, err := net.Listen("tcp", ":"+strconv.Itoa(int(port)))
	if err != nil {
		log.Fatalf("listen on :%d failed: %v", port, err)
	}
	defer listener.Close()

	// Only signal ready once we can accept connections. Health heartbeats start
	// automatically from here — you never ping anything.
	if err := gf.Ready(ctx); err != nil {
		log.Fatalf("gameflow: ready failed: %v", err)
	}
	log.Printf("listening on 0.0.0.0:%d (capacity: %d)", port, gf.Players().Capacity())

	hub := &hub{conns: make(map[net.Conn]struct{})}
	go func() {
		<-ctx.Done()
		listener.Close()
	}()
	acceptLoop(ctx, listener, gf, hub)

	// The platform sends SIGTERM and force-kills the container ~45s later, so
	// shut down cleanly well within that budget.
	log.Println("shutting down")
	hub.closeAll()
	if err := gf.Shutdown(context.Background()); err != nil {
		log.Printf("gameflow: shutdown error: %v", err)
	}
	log.Println("shut down cleanly")
}

func acceptLoop(ctx context.Context, listener net.Listener, gf *gameflow.GameFlow, hub *hub) {
	for {
		conn, err := listener.Accept()
		if err != nil {
			return // listener closed during shutdown
		}
		go handleConnection(ctx, conn, gf, hub)
	}
}

func handleConnection(ctx context.Context, conn net.Conn, gf *gameflow.GameFlow, hub *hub) {
	sessionID := fmt.Sprintf("session-%d", nextSession.Add(1))

	// Register the player with GameFlow. A rejection (server full, tracking
	// disabled, ...) closes the connection with a reason.
	if err := gf.Players().Connect(ctx, sessionID); err != nil {
		switch gameflow.CodeOf(err) {
		case gameflow.CodeServerFull:
			fmt.Fprint(conn, "server full\n")
		case gameflow.CodePlayerTrackingDisabled:
			fmt.Fprint(conn, "player tracking disabled\n")
		default:
			fmt.Fprint(conn, "cannot join\n")
		}
		conn.Close()
		log.Printf("rejected %s: %v", sessionID, err)
		return
	}
	log.Printf("player joined: %s (now %d connected)", sessionID, gf.Players().Count())

	hub.add(conn)
	fmt.Fprintf(conn, "welcome %s (%d/%d players)\n", sessionID, gf.Players().Count(), gf.Players().Capacity())

	scanner := bufio.NewScanner(conn)
	for scanner.Scan() {
		switch line := scanner.Text(); line {
		case "/quit":
			fmt.Fprint(conn, "bye\n")
			goto leave
		case "/who":
			fmt.Fprintf(conn, "players: %v\n", gf.Players().List())
		default:
			hub.broadcast(conn, fmt.Sprintf("%s: %s\n", sessionID, line))
		}
	}

leave:
	hub.remove(conn)
	conn.Close()
	// Unregister on disconnect (idempotent). Use a fresh context so a player
	// leaving during shutdown is still recorded.
	if _, err := gf.Players().Disconnect(context.Background(), sessionID); err != nil &&
		!errors.Is(err, gameflow.ErrNotConnected) {
		log.Printf("disconnect failed for %s: %v", sessionID, err)
	} else {
		log.Printf("player left: %s (now %d connected)", sessionID, gf.Players().Count())
	}
}

// hub is a tiny broadcast registry of connected sockets.
type hub struct {
	mu    sync.Mutex
	conns map[net.Conn]struct{}
}

func (h *hub) add(c net.Conn) {
	h.mu.Lock()
	h.conns[c] = struct{}{}
	h.mu.Unlock()
}

func (h *hub) remove(c net.Conn) {
	h.mu.Lock()
	delete(h.conns, c)
	h.mu.Unlock()
}

func (h *hub) broadcast(from net.Conn, msg string) {
	h.mu.Lock()
	defer h.mu.Unlock()
	for c := range h.conns {
		if c != from {
			fmt.Fprint(c, msg)
		}
	}
}

func (h *hub) closeAll() {
	h.mu.Lock()
	defer h.mu.Unlock()
	for c := range h.conns {
		fmt.Fprint(c, "server shutting down\n")
		c.Close()
	}
	clear(h.conns)
}

func orDash(s string) string {
	if s == "" {
		return "-"
	}
	return s
}
