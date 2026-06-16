# GameFlow Game Server SDK for Go

Integrate Go dedicated game servers with [GameFlow](https://gameflow.gg): server
lifecycle, automatic health reporting and player tracking. One small,
engine-agnostic package with **no third-party dependencies** — built entirely on
the Go standard library.

```bash
go get github.com/GameFlowGG/gameflow-gameserver-sdk/sdk/go
```

```go
package main

import (
	"context"
	"log"

	gameflow "github.com/GameFlowGG/gameflow-gameserver-sdk/sdk/go"
)

func main() {
	ctx := context.Background()

	gf, err := gameflow.Connect(ctx)
	if err != nil {
		log.Fatal(err)
	}

	// ... start your server on gf.Ports().Default() ...

	if err := gf.Ready(ctx); err != nil { // health reporting starts automatically
		log.Fatal(err)
	}
	gf.Players().Connect(ctx, "session-1")    // when a player joins
	gf.Players().Disconnect(ctx, "session-1") // when a player leaves
	gf.Shutdown(ctx)                          // when the match ends
}
```

The same binary runs on GameFlow (sidecar mode, auto-detected) and on your
machine (local mode, zero config).

## API

| Call                                                        | What it does                                                      |
| ----------------------------------------------------------- | ----------------------------------------------------------------- |
| `Connect(ctx, ...Option)`                                   | Connect to the runtime (with retries) or local mode off-platform. |
| `gf.Ready(ctx)`                                             | Mark ready; starts the automatic health heartbeat.                |
| `gf.Shutdown(ctx)`                                          | Clean, idempotent shutdown.                                       |
| `gf.Players().Connect/Disconnect(ctx, id)`                  | Track a player session.                                           |
| `gf.Players().Count/List/Capacity()`                        | Synchronous reads from the local cache.                           |
| `gf.Info(ctx)` / `gf.Payload(ctx)`                          | Current server details / launch payload.                          |
| `gf.Watch(...)` / `gf.OnPayloadChange(...)`                 | Subscribe to live updates; returns a `*Subscription`.             |
| `gf.Ports()` / `gf.Region()` / `gf.BuildID()` / `gf.Mode()` | Platform-provided server metadata.                                |

Errors carry a stable [`ErrorCode`]; branch on it with `gameflow.CodeOf(err)` or
`errors.Is(err, gameflow.ErrServerFull)`. Options are functional
(`gameflow.WithMode`, `WithHealthInterval`, `WithLogger`, `WithSilent`, ...).

## Configuration

`Connect` reads its options, then the environment. Useful knobs:

- `WithMode(gameflow.ModeSidecar | ModeLocal)` — force a transport (default
  auto-detect). The `GAMEFLOW_SDK_MODE` env var overrides auto-detection.
- `WithHealthInterval`, `WithConnectTimeout`, `WithRequestTimeout` — timers.
- `WithLogger` / `WithSlogLogger` / `WithSilent` — diagnostics. The default
  routes through `log/slog` under a `gameflow` component attribute.

Local mode simulates the runtime: `GAMEFLOW_MAX_PLAYERS` sets the players-list
capacity (unset = unlimited, `0` = tracking disabled) and `GAMEFLOW_PAYLOAD`
simulates the launch payload.

## Development

Requires Go 1.23+. Node is needed only for the conformance test, which drives the
shared fake-runtime fixture in `tools/conformance/`.

```bash
go build ./...
go test -race ./...   # unit + conformance tests
go vet ./...
gofmt -l .            # should print nothing
```

Or via [Task](https://taskfile.dev) from the repo root: `task test:go`,
`task ci:go`.

## License

[Apache-2.0](../../LICENSE)
