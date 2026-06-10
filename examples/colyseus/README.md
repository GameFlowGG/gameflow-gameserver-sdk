# Colyseus + GameFlow example

A minimal [Colyseus](https://colyseus.io) server integrated with the GameFlow SDK: lifecycle, automatic health reporting and player tracking. Built to be uploaded to GameFlow as-is using the default Colyseus build template (no Dockerfile needed).

The SDK is vendored at `src/gameflow-sdk.js` (the built zero-dependency bundle) until it is published to npm.

## Run locally

```bash
npm install
npm start                # local mode, listens on 8081
```

Simulate players from another terminal:

```bash
npm run player:join      # connects a player and stays connected
npm run player:join      # connect as many as you want
npm run player:leave     # disconnects the last joined player
node scripts/leave.mjs all   # disconnects everyone
```

The server logs every join/leave with the current count. `GAMEFLOW_MAX_PLAYERS=2 npm start` simulates capacity limits locally.

## Upload to GameFlow

1. Zip the project (no `node_modules`, no Dockerfile):
   `zip -r build.zip package.json package-lock.json src scripts README.md`
2. Create a game with engine **Colyseus**, port **8081 TCP**, and "Max Players per Server" > 0 (player tracking needs it).
3. Upload the zip as a build and start a server.
4. Point the test scripts at the assigned address:
   `node scripts/join.mjs ws://<server-address>:<port>`

The server reads `GAMEFLOW_DEFAULT_PORT`, so whatever port you configure in the dashboard is the one it listens on (8081 is only the local fallback).

## Testing idle shutdown

GameFlow shuts down servers that report zero players past your organization's idle timeout:

1. `node scripts/join.mjs ws://<server>:<port>`: dashboard shows 1/N players.
2. `node scripts/leave.mjs`: count returns to 0 and the idle timer arms.
3. Wait for the org's idle timeout: the platform terminates the server and the SIGTERM handler shuts it down cleanly.
