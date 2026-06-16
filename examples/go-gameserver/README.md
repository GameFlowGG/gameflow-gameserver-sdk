# gameflow-go-gameserver

A minimal **production-ready** GameFlow dedicated server in Go, using the
[`gameflow-gameserver-sdk/sdk/go`](https://github.com/GameFlowGG/gameflow-gameserver-sdk/tree/main/sdk/go)
module. It's a TCP line-chat where **every connection is one tracked player**, so
you can watch the player count in the GameFlow dashboard as clients join.

The same binary runs on GameFlow (sidecar mode, auto-detected) and on your
machine (local mode, zero config). It depends only on the Go standard library
plus the GameFlow SDK.

## Port

- Inside the container the server listens on **`GAMEFLOW_DEFAULT_PORT`** (default
  **7777**), bound to `0.0.0.0`. The `Dockerfile` `EXPOSE`s 7777.
- The **public port is assigned by the platform and differs from the container
  port** (e.g. the server binds 7777 inside the container but is reachable at
  `<public-ip>:7254`). Always read the exact `IP:port` from the GameFlow
  dashboard / allocation response — don't assume it equals 7777.
- Set the container port and **Max Players** in your game settings on GameFlow
  (Max Players becomes the players-list capacity).
- **Protocol must be TCP.** This example is a TCP server and GameFlow defaults
  the protocol to **UDP**, so set the game's protocol to **TCP** or the server
  won't be reachable.

## Run locally

The repo-root `go.work` resolves the SDK from `../../sdk/go`, so this runs
straight from the source tree — no published module required:

```bash
# local mode (no GameFlow runtime); capacity 10
GAMEFLOW_MAX_PLAYERS=10 go run .

# in another terminal, inject 8 players for 120s:
node inject-players.mjs 127.0.0.1 7777 8 120
# or just: nc 127.0.0.1 7777
```

You'll see `player joined`/`player left` and the count climb and drop.

## Deploy to GameFlow

This module depends on the **published** SDK from the Go module proxy, so
deployment is available once the Go SDK is released (mirroring how the Rust
example depends on crates.io). At that point:

1. **Build the upload zip** (Dockerfile + source at the zip root):
   ```bash
   zip -r gameflow-go-gameserver.zip Dockerfile go.mod go.sum main.go .dockerignore
   ```
   (`go.sum` is created by `go mod tidy` once the SDK is published.)
2. In the GameFlow dashboard create a game with **engine = custom**, set the
   **container port to 7777** and your **Max Players**.
3. Upload `gameflow-go-gameserver.zip`. GameFlow extracts it, finds the
   `Dockerfile` at the root, builds the image and pushes it.
4. Deploy a fleet from that build, then allocate a server. Note the public
   **IP and port** it gives you.
5. Inject players against the real server:
   ```bash
   node inject-players.mjs <public-ip> <public-port> 8 120
   ```
   Watch the player count rise in the dashboard, then fall when the script
   disconnects (Ctrl-C to end early).

> To build the Docker image **before** the SDK is published, add a local replace
> and vendor the SDK into the build context:
> `go mod edit -replace github.com/GameFlowGG/gameflow-gameserver-sdk/sdk/go=../../sdk/go`.

## What it exercises

`gameflow.Connect` → `Ready()` (health heartbeat starts automatically) →
`Players().Connect()/Disconnect()/Count()/List()/Capacity()` → graceful
`Shutdown()` on SIGTERM. If the SDK works here, it works.
