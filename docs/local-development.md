# Local development

The SDK is designed so the exact same server code runs on GameFlow and on your machine.

## How mode detection works

`GameFlow.connect()` picks a transport:

- Running on GameFlow: the platform injects the runtime environment; the SDK connects to it (with retries) and fails hard if it cannot, so broken pods are visible instead of silently degraded.
- Running anywhere else: the SDK logs one line and enters **local mode**. Lifecycle calls are simulated, player tracking works against an in-memory list, and `watch()`/`onPayloadChange()` emit synthetic updates. No network, no configuration.

You can force a mode when needed:

```ts
await GameFlow.connect({ mode: 'local' }); // or 'sidecar'
```

or with the env var `GAMEFLOW_SDK_MODE=local|sidecar` (the option wins over the env var, both win over auto-detection).

## Simulating platform behavior

Local mode reads a few env vars so you can exercise real scenarios:

```bash
# Simulate the assigned port (otherwise use your own fallback)
GAMEFLOW_DEFAULT_PORT=7777 \
# Simulate max players (connect() throws ServerFullError beyond it)
GAMEFLOW_MAX_PLAYERS=8 \
# Simulate a launch payload for gf.payload()
GAMEFLOW_PAYLOAD='{"match":"test-1"}' \
node server.js
```

`GAMEFLOW_MAX_PLAYERS=0` simulates a game with player tracking disabled, which is what happens in production when "Max Players per Server" is 0.

## Trying the example

```bash
pnpm install
node sdk/typescript/examples/node-tcp/server.js
# in another terminal:
nc 127.0.0.1 7777
```

Type lines to chat between connections, `/who` to list players, Ctrl+C on the server to watch a clean shutdown.

## Checklist before uploading a build

- `gf.ready()` is called only after the server is listening.
- Players are reported on every join and leave. Idle servers (zero reported players) are shut down after your organization's idle timeout.
- SIGTERM triggers a drain plus `gf.shutdown()` within 45 seconds.
- The server listens on `GAMEFLOW_DEFAULT_PORT` when present.
