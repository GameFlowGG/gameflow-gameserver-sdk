# gameflow-gameserver-sdk

Official [GameFlow](https://gameflow.gg) SDK for Rust dedicated game servers:
server lifecycle, automatic health reporting and player tracking. Async, built
on `tokio` + `reqwest`, and engine-agnostic — it works with any Rust game
framework. For [Bevy](https://bevyengine.org), copy the ready-made bridge module
from the [`bevy-server` example](https://github.com/GameFlowGG/gameflow-gameserver-sdk/tree/main/sdk/rust/examples/bevy-server)
into your project.

```toml
[dependencies]
gameflow-gameserver-sdk = "0.1"
```

The crate is imported as `gameflow`:

```rust,no_run
use gameflow::GameFlow;

#[tokio::main]
async fn main() -> gameflow::Result<()> {
    let gf = GameFlow::connect().await?;
    println!("mode: {}", gf.mode());

    // Listen on the assigned port, then signal readiness.
    // Health reporting starts automatically; you never ping anything yourself.
    let port = gf.ports().default().unwrap_or(7777);
    // ... start your server on `port` ...
    gf.ready().await?;

    // Track players as they join and leave.
    gf.players().connect("session-1").await?;
    gf.players().disconnect("session-1").await?;

    gf.players().count();    // current player count (synchronous)
    gf.players().list();     // connected session ids (synchronous)

    gf.shutdown().await?;    // when the match ends
    Ok(())
}
```

## Local development

The exact same code runs on GameFlow and on your machine. Off-platform the SDK
enters **local mode** automatically: lifecycle calls are simulated, player
tracking works against an in-memory list, and `watch()` / `on_payload_change()`
emit synthetic updates. No network, no configuration.

Force a mode when needed:

```rust,no_run
# use gameflow::{ConnectOptions, GameFlow, ModeOption};
# async fn run() -> gameflow::Result<()> {
let gf = GameFlow::connect_with(ConnectOptions::new().mode(ModeOption::Local)).await?;
# Ok(()) }
```

or set `GAMEFLOW_SDK_MODE=local|sidecar` (the option wins over the env var, both
win over auto-detection).

Local mode reads a few env vars so you can exercise real scenarios:

- `GAMEFLOW_DEFAULT_PORT` — simulate the assigned port.
- `GAMEFLOW_MAX_PLAYERS` — simulate capacity (`connect()` fails with
  `ServerFull` beyond it). `0` simulates a game with player tracking disabled;
  unset means unlimited (`capacity()` returns `-1`).
- `GAMEFLOW_PAYLOAD` — simulate the launch payload returned by `payload()`.

## Errors

Every fallible call returns [`GameFlowError`]. Match on `error.code()` for the
stable [`ErrorCode`] shared by every GameFlow SDK:

```rust,no_run
# use gameflow::{ErrorCode, GameFlow};
# async fn run(gf: GameFlow) {
match gf.players().connect("session-1").await {
    Ok(()) => {}
    Err(error) => match error.code() {
        ErrorCode::ServerFull => { /* reject: server is at capacity */ }
        ErrorCode::PlayerAlreadyConnected => { /* reconnect of a live session */ }
        _ => eprintln!("connect failed: {error}"),
    },
}
# }
```

Codes: `SIDECAR_UNAVAILABLE`, `PLAYER_ALREADY_CONNECTED`, `SERVER_FULL`,
`PLAYER_TRACKING_DISABLED`, `NOT_CONNECTED`, `REQUEST_FAILED`.

## Logging

The SDK logs through [`tracing`] under the `gameflow` target. Install any
`tracing` subscriber to see output; pass `ConnectOptions::silent()` or a custom
[`Logger`] to change it.

## Example

[`examples/tokio-tcp`](https://github.com/GameFlowGG/gameflow-gameserver-sdk/tree/main/sdk/rust/examples/tokio-tcp)
is a minimal TCP chat server (the Rust twin of the Node `node-tcp` example):

```bash
cargo run -p gameflow-example-tokio-tcp
# in another terminal:
nc 127.0.0.1 7777
```

## License

[Apache-2.0](https://github.com/GameFlowGG/gameflow-gameserver-sdk/blob/main/LICENSE)

[`tracing`]: https://crates.io/crates/tracing
