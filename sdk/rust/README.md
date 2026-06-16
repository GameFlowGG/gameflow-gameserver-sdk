# GameFlow Game Server SDK for Rust

Integrate Rust dedicated game servers with [GameFlow](https://gameflow.gg):
server lifecycle, automatic health reporting and player tracking. One
engine-agnostic crate, async, built on `tokio` + `reqwest` — it works with any
Rust game framework (Bevy, Fyrox, macroquad, a bare tokio server, ...).

```toml
[dependencies]
gameflow-gameserver-sdk = "0.1"
tokio = { version = "1", features = ["macros", "rt-multi-thread"] }
```

```rust,no_run
use gameflow::GameFlow;

#[tokio::main]
async fn main() -> gameflow::Result<()> {
    let gf = GameFlow::connect().await?;
    // ... start your server on gf.ports().default() ...
    gf.ready().await?;
    gf.players().connect("session-1").await?;
    gf.shutdown().await?;
    Ok(())
}
```

## Layout

```
sdk/rust/
  gameflow/                 The SDK crate: gameflow-gameserver-sdk (lib `gameflow`)
  examples/
    tokio-tcp/              Minimal tokio TCP server (twin of examples/node-tcp)
    bevy-server/            Headless Bevy dedicated server
```

## Using it with Bevy

There is no separate `gameflow-bevy` crate to publish, and deliberately so. A
Bevy plugin must be compiled against one specific Bevy version (Bevy is pre-1.0,
so each `0.x` is a breaking release and Cargo can't mix `0.18` and `0.19`
types), which would force a new plugin release on every Bevy update. The SDK
itself is engine-agnostic, so instead the Bevy integration ships as a
**copy-paste module**: [`examples/bevy-server/src/gameflow_plugin.rs`](examples/bevy-server/src/gameflow_plugin.rs).

Copy that file into your project and you get a `GameFlowPlugin` (with its own
tokio runtime — reqwest can't run on Bevy's task pools), a `GameFlowClient`
resource, and ECS messages/events. Because it lives in your crate, it compiles
against *your* Bevy version. See [`examples/bevy-server`](examples/bevy-server)
for the full app.

## Development

Requires a stable Rust toolchain (MSRV 1.82). Node is needed for the conformance
test, which drives the shared fake-runtime fixture in `tools/conformance/`.

```bash
cargo build --workspace
cargo test --workspace        # unit + conformance tests
cargo clippy --workspace --all-targets -- -D warnings
cargo fmt --check
```

Or via [Task](https://taskfile.dev) from the repo root: `task test:rust`,
`task ci:rust`.

## License

[Apache-2.0](../../LICENSE)
