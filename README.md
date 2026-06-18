# GameFlow Game Server SDK

Official SDKs for integrating dedicated game servers with [GameFlow](https://gameflow.gg).
One small library per language that handles the server side of hosting on GameFlow —
**lifecycle, automatic health reporting and player tracking** — plus a **local mode** so
the same build runs on your machine with zero configuration.

## What it does

Every SDK exposes the same lifecycle, adapted to each language's idioms:

1. **Connect** — attach to the GameFlow runtime (or local mode off-platform), with retries.
2. **Ready** — mark the server ready to accept players; the SDK then sends health
   heartbeats automatically.
3. **Track players** — register and unregister sessions as players join and leave, so the
   platform sees accurate player counts.
4. **Shutdown** — cleanly end the server when the match is over.

The SDK owns the fiddly parts you'd otherwise build yourself: health heartbeats, connect
retries and backoff, a shared server-update stream, and a local mode for off-platform
development. See each SDK's README for a language-specific quickstart.

## SDKs

| Language   | Package                                         | Status | Version |
| ---------- | ----------------------------------------------- | ------ | ------- |
| TypeScript | [`@gameflow.gg/gameserver-sdk`](sdk/typescript) | Stable | 0.1.2   |
| Godot      | [`gameflow` addon](sdk/godot)                   | Beta   | 0.1.2   |
| Rust       | [`gameflow-gameserver-sdk`](sdk/rust)           | Beta   | 0.1.0   |
| Go         | [`gameflow-gameserver-sdk/sdk/go`](sdk/go)      | Beta   | 0.1.0   |
| Unity      | [`gg.gameflow.gameserver`](sdk/unity)           | Beta   | 0.1.0   |
| Unreal     | Planned                                         | —      | —       |

## How it works

All SDKs implement one cross-language contract, so behavior is identical no matter the
language:

- [`proto/gameflow/sdk/v1`](proto/gameflow/sdk/v1) — the canonical API surface.
- [`docs/spec.md`](docs/spec.md) — the behavior protobuf can't capture (health cadence,
  retry parameters, local mode, stable error codes).
- [`tools/conformance`](tools/conformance) — a fake-runtime fixture that every SDK's test
  suite runs against, keeping the implementations honest.

Transport is an implementation detail: in v1 each SDK talks REST to the local GameFlow
runtime, and simulates that runtime in local mode.

## Documentation

- [Getting started](docs/getting-started.md)
- [Local development](docs/local-development.md)
- [Cross-language SDK spec](docs/spec.md) (for SDK implementers)
- [GameFlow docs](https://docs.gameflow.gg)

## Repository layout

```
proto/gameflow/sdk/v1/   Canonical cross-language API contract
sdk/typescript/          TypeScript SDK (@gameflow.gg/gameserver-sdk)
sdk/godot/               Godot 4 SDK (GDScript addon)
sdk/rust/                Rust SDK (gameflow-gameserver-sdk)
sdk/go/                  Go SDK (stdlib-only, engine-agnostic)
sdk/unity/               Unity 2022.3+ SDK (pure C# core + thin Unity layer)
docs/                    Guides and the SDK behavioral spec
tools/conformance/       Fake runtime fixture shared by every SDK test suite
```

Each SDK keeps its runnable examples under its own directory — `sdk/typescript/examples/`,
`sdk/rust/examples/`, `sdk/godot/example/`, `sdk/go/examples/`, and the Unity package's
`Samples~/`.

## Development

Each SDK builds and tests independently via [Task](https://taskfile.dev):

```bash
task install      # install JS workspace deps (pnpm)
task ci:sdk       # TypeScript SDK CI (format, typecheck, build, test)
task ci:go        # Go SDK CI         (also: task test:go)
task ci:rust      # Rust SDK CI       (also: task test:rust)
task ci:unity     # Unity SDK CI      (also: task test:unity)
task test:godot   # Godot SDK tests
task ci:proto     # buf lint + format check
```

The Go and Rust suites need their toolchains, the Unity suite needs a .NET SDK, and the
conformance tests need Node.

Releases are tagged per SDK from the release workflow — `typescript-v0.1.2`,
`godot-v0.1.2`, `rust-v0.1.0`, `sdk/go/v0.1.0`, and `sdk/unity/v0.1.0` (the Go and Unity
packages live in subdirectories, so their tags are path-prefixed for consumers).

## License

[Apache-2.0](LICENSE)
