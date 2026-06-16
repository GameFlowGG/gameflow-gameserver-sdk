# GameFlow Game Server SDK for Godot

Integrates Godot 4 dedicated servers with [GameFlow](https://gameflow.gg): server lifecycle, automatic health reporting and player tracking. Pure GDScript, no dependencies; works in headless/server builds.

```gdscript
var gf := GameFlow.new()

func _ready() -> void:
    var res := await gf.start()
    if not res.ok:
        push_error("gameflow: %s" % res.message)
        return
    # start listening on gf.ports.default, then:
    await gf.ready()  # health reporting starts automatically

    await gf.players.track(session_id)    # when a player joins
    await gf.players.untrack(session_id)  # when a player leaves

    await gf.shutdown()  # when the match ends
```

## Install

Copy `addons/gameflow/` into your project's `addons/` directory (and optionally enable the plugin in Project Settings → Plugins; the SDK classes are plain `class_name` scripts, so the plugin toggle is not required).

Requires Godot 4.4+ (CI-tested on 4.6).

## Usage

Create one `GameFlow` instance for the lifetime of the server process — an [autoload](https://docs.godotengine.org/en/stable/tutorials/scripting/singletons_autoload.html) is the natural place. Off GameFlow (your machine, CI) the SDK runs in local mode automatically: lifecycle calls are simulated and player tracking works against an in-memory list, so the same code runs everywhere with zero configuration.

### Results instead of exceptions

GDScript has no exceptions, so every fallible call returns a [`GameFlowResult`](addons/gameflow/gameflow_result.gd). `res.ok` tells you whether it worked; `res.code` carries the same stable error codes as every other GameFlow SDK:

```gdscript
var res := await gf.players.track(session_id)
if not res.ok:
    match res.code:
        GameFlowResult.SERVER_FULL:
            kick(session_id, "server full (capacity %d)" % res.capacity)
        GameFlowResult.PLAYER_ALREADY_CONNECTED:
            pass  # reconnect of a live session
        _:
            push_warning("track failed: %s" % res.message)
```

Codes: `SIDECAR_UNAVAILABLE`, `PLAYER_ALREADY_CONNECTED`, `SERVER_FULL`, `PLAYER_TRACKING_DISABLED`, `NOT_CONNECTED`, `REQUEST_FAILED`.

### API

| Member                                                             | Notes                                                                                   |
| ------------------------------------------------------------------ | --------------------------------------------------------------------------------------- |
| `start(options := {}) -> GameFlowResult`                           | Connects (with retries) or falls back to local mode. Call once.                         |
| `ready() -> GameFlowResult`                                        | Marks the server ready; the automatic health heartbeat starts here.                     |
| `shutdown() -> GameFlowResult`                                     | Clean shutdown. Idempotent; afterwards every call fails with `NOT_CONNECTED`.           |
| `payload() -> GameFlowResult`                                      | Launch payload as an opaque string in `res.value` (`null` when none).                   |
| `info() -> GameFlowResult`                                         | `GameFlowServerInfo` (name, state, address, ports, labels, annotations) in `res.value`. |
| `watch(listener) -> Callable`                                      | Server updates (`GameFlowServerInfo`); returns an unsubscribe callable.                 |
| `on_payload_change(listener) -> Callable`                          | Fires with the new payload when it changes (e.g. match assignment).                     |
| `players.track(id)` / `players.untrack(id)`                        | Player tracking (named track/untrack because `connect` collides with Godot signals).    |
| `players.count()` / `list()` / `capacity()` / `tracking_enabled()` | Synchronous reads from the local cache.                                                 |
| `ports.default` / `ports.by_name(n)` / `ports.tls`                 | Assigned ports from `GAMEFLOW_*_PORT` env vars (`null` when absent).                    |
| `mode` / `region` / `build_id`                                     | `"sidecar"` or `"local"`; platform region and build id (`""` when absent).              |
| `health_degraded` (signal)                                         | Fired once when health pings have been failing for a sustained period.                  |

`start()` options (all optional): `mode` (`"auto"`/`"sidecar"`/`"local"`), `health_interval_ms` (default 5000, min 500), `connect_timeout_ms` (default 30000), `request_timeout_ms` (default 3000), `logger` (a `GameFlowLogger`; `null` silences the SDK).

### Local development

Simulation knobs (env vars, only read in local mode):

- `GAMEFLOW_MAX_PLAYERS` — capacity of the in-memory players list. Unset = unlimited (`capacity()` returns -1), `0` = tracking disabled, mirroring production.
- `GAMEFLOW_PAYLOAD` — simulates the launch payload.

## Example

[`example/tcp_server.gd`](example/tcp_server.gd) is a minimal TCP server (the Godot twin of the TS node-tcp example):

```bash
godot --headless --path sdk/godot --script res://example/tcp_server.gd
```

## Development

Tests run headless against the shared conformance fixture (`tools/conformance/`):

```bash
GODOT=/path/to/godot4 node sdk/godot/test/run.mjs   # or: task test:godot
```
