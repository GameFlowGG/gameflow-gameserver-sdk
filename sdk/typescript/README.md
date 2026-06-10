# @gameflow.gg/gameserver-sdk

Official [GameFlow](https://gameflow.gg) SDK for Node.js game servers: server lifecycle, automatic health reporting and player tracking. Zero runtime dependencies, Node 18.17+.

```bash
npm install @gameflow.gg/gameserver-sdk
```

## Quickstart

```ts
import { GameFlow, ServerFullError } from '@gameflow.gg/gameserver-sdk';

const gf = await GameFlow.connect();

// Start your server on the assigned port, then signal readiness.
// Health reporting starts automatically; you never ping anything yourself.
myServer.listen(gf.ports.default ?? 7777);
await gf.ready();

// Track players as they join and leave
try {
  await gf.players.connect(client.sessionId);
} catch (error) {
  if (error instanceof ServerFullError) client.reject('server full');
}
await gf.players.disconnect(client.sessionId);

gf.players.count(); // current player count (synchronous)
gf.players.list(); // connected session ids
gf.players.capacity(); // max players configured for your game

// Shut down cleanly when the match ends (or on SIGTERM)
process.on('SIGTERM', async () => {
  await gf.shutdown();
  process.exit(0);
});
```

## Local development

Run your server on your machine with no extra setup: when it is not running on GameFlow the SDK switches to local mode automatically. Lifecycle calls are simulated and player tracking works against an in-memory list, so the same code runs unchanged in both places. See env vars below to simulate payloads and capacity.

## API

| Member                                    | Description                                                            |
| ----------------------------------------- | ---------------------------------------------------------------------- |
| `GameFlow.connect(options?)`              | Connects to the GameFlow runtime (with retries) or enters local mode   |
| `gf.ready()`                              | Marks the server ready for players and starts the health heartbeat     |
| `gf.shutdown()`                           | Cleanly shuts the server down (idempotent)                             |
| `gf.players.connect(id)`                  | Registers a player session                                             |
| `gf.players.disconnect(id)`               | Unregisters a player session, resolves `false` if it was not connected |
| `gf.players.count/list/capacity`          | Synchronous reads of the player list                                   |
| `gf.payload()`                            | The launch payload (opaque string) set when the server was requested   |
| `gf.onPayloadChange(cb)`                  | Fires when the payload changes (e.g. on match assignment)              |
| `gf.watch(cb)`                            | Streams server updates (state, address, ports)                         |
| `gf.info()`                               | Current server details                                                 |
| `gf.ports.default` / `gf.ports.get(name)` | Ports assigned by the platform                                         |
| `gf.region`, `gf.buildId`, `gf.mode`      | Server environment details                                             |

### Connect options

```ts
GameFlow.connect({
  mode: 'auto', // 'auto' | 'sidecar' | 'local'
  healthIntervalMs: 5000,
  connectTimeoutMs: 30000,
  requestTimeoutMs: 3000,
  logger: null, // silence the SDK, or pass your own Logger
  onHealthDegraded: () => {}, // sustained health reporting failures
});
```

### Errors

All SDK errors extend `GameFlowError` and carry a stable `code`:

| Error                         | Code                       | When                                  |
| ----------------------------- | -------------------------- | ------------------------------------- |
| `PlayerAlreadyConnectedError` | `PLAYER_ALREADY_CONNECTED` | Session id already in the player list |
| `ServerFullError`             | `SERVER_FULL`              | Player list at capacity               |
| `PlayerTrackingDisabledError` | `PLAYER_TRACKING_DISABLED` | Game configured with max players = 0  |
| `SidecarUnavailableError`     | `SIDECAR_UNAVAILABLE`      | GameFlow runtime unreachable          |
| `NotConnectedError`           | `NOT_CONNECTED`            | SDK used after `shutdown()`           |
| `RequestFailedError`          | `REQUEST_FAILED`           | Unexpected runtime response           |

### Environment variables

Set by the platform on every server:

| Variable                | Meaning                       |
| ----------------------- | ----------------------------- |
| `GAMEFLOW_DEFAULT_PORT` | Primary port to listen on     |
| `GAMEFLOW_<NAME>_PORT`  | Additional named ports        |
| `GAMEFLOW_REGION`       | Region the server runs in     |
| `GAMEFLOW_BUILD_ID`     | Build id of the running image |

Read by the SDK for local development:

| Variable               | Meaning                                              |
| ---------------------- | ---------------------------------------------------- |
| `GAMEFLOW_SDK_MODE`    | Force `local` or `sidecar` mode                      |
| `GAMEFLOW_MAX_PLAYERS` | Simulated player capacity in local mode (0 disables) |
| `GAMEFLOW_PAYLOAD`     | Simulated launch payload in local mode               |

## Important: player tracking keeps your server alive

GameFlow shuts down servers that report zero connected players past your organization's idle timeout. Call `gf.players.connect()` as soon as a real player joins. If your game has "Max Players per Server" set to 0, player tracking is disabled entirely and the SDK logs a warning at startup.

## License

Apache-2.0
