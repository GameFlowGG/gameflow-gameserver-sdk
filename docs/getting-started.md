# Getting started

This guide integrates a Node.js dedicated server with GameFlow using `@gameflow/gameserver-sdk`. The full working example lives in [`examples/node-tcp`](../examples/node-tcp).

## 1. Install

```bash
npm install @gameflow/gameserver-sdk
```

Requires Node 18.17 or newer. The SDK has zero runtime dependencies.

## 2. Connect and signal readiness

```ts
import { GameFlow } from '@gameflow/gameserver-sdk';

const gf = await GameFlow.connect();

const server = createMyGameServer();
server.listen(gf.ports.default ?? 7777, async () => {
  await gf.ready();
});
```

Two rules:

- Listen on `gf.ports.default`. The platform assigns the port and routes players to it.
- Call `gf.ready()` only when your server can actually accept connections. Until then the platform will not send players to it.

Health reporting starts automatically when `ready()` succeeds. There is nothing else to do.

## 3. Track players

```ts
server.on('playerJoined', async (player) => {
  await gf.players.connect(player.sessionId);
});

server.on('playerLeft', async (player) => {
  await gf.players.disconnect(player.sessionId);
});
```

Use any stable unique id (session id, account id). This matters beyond dashboards: GameFlow shuts down servers that report zero players past your organization's idle timeout, so a server that never reports players will be reaped. `connect()` throws `ServerFullError` when the list is at capacity and `PlayerAlreadyConnectedError` on duplicate ids; `disconnect()` is idempotent and resolves `false` for unknown ids.

`gf.players.count()`, `gf.players.list()` and `gf.players.capacity()` are synchronous and read from a cache kept up to date by every call.

## 4. Use the launch payload (optional)

Servers can be launched with an opaque payload (match config, team rosters, anything):

```ts
const payload = await gf.payload(); // string | undefined
const match = payload ? JSON.parse(payload) : null;

gf.onPayloadChange((next) => {
  // fires when the platform assigns this server to a new match
});
```

The payload is an opaque string to the SDK; parse it however you produced it.

## 5. Shut down cleanly

```ts
process.on('SIGTERM', async () => {
  server.close();
  await gf.shutdown();
  process.exit(0);
});
```

When a server is stopped, the platform sends SIGTERM and force-kills the container about 45 seconds later. Drain and call `shutdown()` well within that budget. Also call `shutdown()` yourself when a match ends and the server is done.

## 6. Ship it

Package your server with a Dockerfile at the build root (run as UID 1000, expose your port) and upload it through the GameFlow dashboard or CLI. See [Creating a Game](https://docs.gameflow.gg/dashboard/creating-a-game) and the [example Dockerfile](../examples/node-tcp/Dockerfile).

To test everything locally first, see [Local development](local-development.md).
