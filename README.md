# GameFlow Game Server SDK

Official SDKs for integrating dedicated game servers with [GameFlow](https://gameflow.gg): server lifecycle, automatic health reporting and player tracking.

```ts
import { GameFlow } from '@gameflow.gg/gameserver-sdk';

const gf = await GameFlow.connect();
await gf.ready(); // health reporting starts automatically

gf.players.connect(sessionId); // when a player joins
gf.players.disconnect(sessionId); // when a player leaves

await gf.shutdown(); // when the match ends
```

## SDKs

| Language   | Package                                         | Status | Version |
| ---------- | ----------------------------------------------- | ------ | ------- |
| TypeScript | [`@gameflow.gg/gameserver-sdk`](sdk/typescript) | Stable | 0.1.0   |
| Unity      | Planned                                         |        |         |
| Unreal     | Planned                                         |        |         |
| Godot      | Planned                                         |        |         |
| Rust       | Planned                                         |        |         |
| Go         | Planned                                         |        |         |
| Python     | Planned                                         |        |         |

## Why an SDK?

Servers hosted on GameFlow report readiness, health and connected players to the platform. The SDK handles all of it with a few calls, including details you would otherwise own yourself: health heartbeats, retries, and a local mode so your server runs on your machine with zero configuration.

## Documentation

- [Getting started](docs/getting-started.md)
- [Local development](docs/local-development.md)
- [Cross-language SDK spec](docs/spec.md) (for SDK implementers)
- [GameFlow docs](https://docs.gameflow.gg)

## Repository layout

```
proto/gameflow/sdk/v1/   Canonical cross-language API contract
sdk/typescript/          TypeScript SDK (@gameflow.gg/gameserver-sdk)
examples/node-tcp/       Minimal TCP server using the SDK
docs/                    Guides and the SDK behavioral spec
```

## Development

Requires Node 24 (`.nvmrc`), pnpm 10 and [Task](https://taskfile.dev).

```bash
task install   # pnpm install
task test      # SDK test suite
task ci:sdk    # full CI pipeline (format, typecheck, build, test)
task ci:proto  # buf lint + format check
```

Releases are tagged per SDK (`typescript-v0.1.0`) and published from the release workflow.

## License

[Apache-2.0](LICENSE)
