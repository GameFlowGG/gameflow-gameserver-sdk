# GameFlow Dedicated Server sample (Unreal)

A minimal C++ GameMode that wires up the full GameFlow lifecycle:
connect → ready → track players → auto-shutdown.

Off GameFlow the code runs in **local mode** automatically — no sidecar, no
configuration required.

## Files

| File                                                         | Role                                                       |
| ------------------------------------------------------------ | ---------------------------------------------------------- |
| [`GameFlowServerBootstrap.h`](GameFlowServerBootstrap.h)     | GameMode subclass declaration.                             |
| [`GameFlowServerBootstrap.cpp`](GameFlowServerBootstrap.cpp) | `BeginPlay` → Start + Ready; `PostLogin`/`Logout` → track. |

## Drop it into your project

1. Add the GameFlow plugin to your project (see
   [`sdk/unreal/GameFlow/README.md`](../../GameFlow/README.md) for install
   instructions — copy or symlink `sdk/unreal/GameFlow` into `Plugins/`).

2. Copy `GameFlowServerBootstrap.h` and `GameFlowServerBootstrap.cpp` into
   your project's `Source/<YourModule>/` directory.

3. In your `.Build.cs` add `"GameFlowUnreal"` to `PublicDependencyModuleNames`:

   ```csharp
   PublicDependencyModuleNames.AddRange(new string[] {
       "Core", "CoreUObject", "Engine",
       "GameFlowUnreal",   // <-- add this
   });
   ```

4. Set `AGameFlowServerBootstrap` (or your subclass) as the GameMode for your
   dedicated-server map (World Settings → GameMode Override).

5. Build and run as a Dedicated Server target — the subsystem connects
   automatically when the map loads.

## How it works

| Step          | Where                                       | What happens                                                |
| ------------- | ------------------------------------------- | ----------------------------------------------------------- |
| Connect       | `BeginPlay` → `Start()`                     | Probes the runtime; falls back to local mode off-platform.  |
| Ready         | Start callback → `Ready()`                  | Platform routes players to this server; health loop starts. |
| Player joins  | `PostLogin` → `ConnectPlayer(SessionId)`    | Session registered with the platform player list.           |
| Player leaves | `Logout` → `DisconnectPlayer(SessionId)`    | Session removed; idempotent if they never finished joining. |
| Shutdown      | `UGameFlowSubsystem::Deinitialize()` (auto) | Health loop stops, watch closes, `POST /shutdown` is sent.  |

## Run it locally (local mode)

```bash
# Cap the player list to 4; unset = unlimited, 0 = tracking disabled
GAMEFLOW_MAX_PLAYERS=4 ./YourServer -game -dedicated -log
```

The payload for the match can be injected as:

```bash
GAMEFLOW_PAYLOAD='{"mapId":"dust2","mode":"tdm"}' ./YourServer -game -dedicated -log
```

## Ship it to GameFlow

1. Build as a **Dedicated Server** / **Linux** target.
2. Package into a Docker image and upload through the GameFlow dashboard or CLI.
3. On the platform the server connects to the GameFlow runtime automatically
   (sidecar mode) — player tracking then flows to the platform.

See [`docs/getting-started.md`](../../../../docs/getting-started.md) for the
full deploy flow.
