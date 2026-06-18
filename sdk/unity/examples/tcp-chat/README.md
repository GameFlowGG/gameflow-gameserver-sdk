# Unity TCP chat example

A minimal TCP line-chat where every connection is one tracked player — the Unity twin of the
[`node-tcp`](../../../typescript/examples/node-tcp), [`tokio-tcp`](../../../rust/examples/tokio-tcp)
and [`go tcp-server`](../../../go/examples/tcp-server) examples. It exercises the full GameFlow
lifecycle: connect → ready → track players → shutdown, plus chat broadcast.

Off GameFlow the server runs in **local mode** automatically (no sidecar, no configuration), so
the same build runs on your machine and on the platform.

## Files

| File                                                           | Role                                                                                                                                 |
| -------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| [`Server/GameFlowTcpServer.cs`](Server/GameFlowTcpServer.cs)   | Headless dedicated server. Uses the SDK; every TCP connection becomes a tracked player. Build as a **Dedicated Server**.             |
| [`Client/GameFlowChatClient.cs`](Client/GameFlowChatClient.cs) | Throwaway test client with a self-contained IMGUI chat UI. **Not** part of the server SDK — a real game uses its own netcode and UI. |

## Run it locally

1. Create a Unity project (2022.3+ / Unity 6) and add the package:
   `Window → Package Manager → + → Add package from git URL`
   ```
   https://github.com/GameFlowGG/gameflow-gameserver-sdk.git?path=/sdk/unity/Packages/gg.gameflow.gameserver#sdk/unity/v0.1.0
   ```
2. **Server:** drop `GameFlowTcpServer.cs` into `Assets/`, put it on a GameObject in a scene, and
   press Play (or build it). It logs `listening on :7777 (mode=Local …)`.
   Cap capacity in local mode with `GAMEFLOW_MAX_PLAYERS` (unset = unlimited, `0` = tracking off).
3. **Client:** drop `GameFlowChatClient.cs` on a GameObject in a second scene. Build it (the Editor
   only plays one scene at a time, so run the server and the client as separate processes), press
   **Connect**, and chat. Open several clients to see the player count climb.

Any TCP client works too — `nc 127.0.0.1 7777` is enough to register a player.

## Ship it to GameFlow

1. Switch the build target to **Dedicated Server** / **Linux** (IL2CPP) and build the server scene
   into a `Build/` folder next to the [`Dockerfile`](Dockerfile).
2. `docker build` the image (it runs as UID 1000, headless, logs to stdout) and upload it through
   the GameFlow dashboard or CLI.
3. On the platform the server reads `GAMEFLOW_DEFAULT_PORT` for its listen port and connects to the
   GameFlow runtime automatically (sidecar mode) — player tracking then flows to the platform.

See the repo [`docs/getting-started.md`](../../../../docs/getting-started.md) for the full deploy flow.
