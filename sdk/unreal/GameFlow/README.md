# GameFlow Game Server SDK for Unreal Engine

Integrates Unreal Engine dedicated servers with [GameFlow](https://gameflow.gg):
server lifecycle, automatic health reporting and player tracking. A C++ core
module (`GameFlowCore`, no engine-specific dependencies) with a thin Unreal layer
(`GameFlowUnreal`) that exposes a `UGameInstanceSubsystem` with Blueprint events
and pure reads; targets **Unreal Engine 5.3+** (tested on UE 5.8).

```cpp
// In BeginPlay (server only):
UGameFlowSubsystem* GF = GetGameInstance()->GetSubsystem<UGameFlowSubsystem>();

GF->Start([GF](FGameFlowError Err) {
    if (!Err.IsOk()) { return; }
    GF->Ready([](FGameFlowError Err) { /* server ready, health loop running */ });
});

// In PostLogin:
GF->ConnectPlayer(SessionId, [](FGameFlowError Err) { /* ... */ });

// In Logout:
GF->DisconnectPlayer(SessionId, [](FGameFlowError Err, bool) { /* ... */ });

// Shutdown is automatic — UGameFlowSubsystem::Deinitialize() handles it.
```

## Install

There is no UE Marketplace listing; install directly from this repository.

### Option A — copy the plugin into your project

```bash
cp -r sdk/unreal/GameFlow /path/to/YourProject/Plugins/GameFlow
```

Then regenerate project files and build.

### Option B — add as a git submodule

```bash
cd YourProject
git submodule add https://github.com/GameFlowGG/gameflow-gameserver-sdk.git \
    Plugins/GameFlowSDKSource
# Symlink or copy sdk/unreal/GameFlow into Plugins/GameFlow
```

### Enable the plugin

In the Unreal Editor: **Edit → Plugins → Project → GameFlow → Enable**.
Or add to your `.uproject`:

```json
{
  "Plugins": [{ "Name": "GameFlow", "Enabled": true }]
}
```

Add `"GameFlowUnreal"` (and optionally `"GameFlowCore"`) to
`PublicDependencyModuleNames` in your module's `.Build.cs`:

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine",
    "GameFlowUnreal",
});
```

Requires **Unreal Engine 5.3+** and a **Dedicated Server** build target for
production. The Dedicated Server build target is recommended — the plugin works
in PIE and Editor builds too, but the server-only path (`HasAuthority()`) is
what talks to the runtime.

## Quickstart

```cpp
// 1. Get the subsystem (valid for the GameInstance lifetime).
UGameFlowSubsystem* GF = GetGameInstance()->GetSubsystem<UGameFlowSubsystem>();

// 2. Connect — with automatic retries in sidecar mode, or instant in local mode.
GF->Start([GF](FGameFlowError Err) {
    if (!Err.IsOk()) {
        UE_LOG(LogTemp, Error, TEXT("[gameflow] Start failed: %s"), *Err.Message);
        return;
    }

    // 3. Mark ready — health heartbeats start automatically in sidecar mode.
    GF->Ready([](FGameFlowError Err) {
        // Server is now live on GameFlow.
    });
});

// 4. Track players.
GF->ConnectPlayer(SessionId, [](FGameFlowError Err) { /* handle errors */ });
GF->DisconnectPlayer(SessionId, [](FGameFlowError Err, bool bWasTracked) { /* ... */ });

// 5. Shutdown — called automatically by Deinitialize() on map unload / process exit.
//    Call explicitly if you need to end early:
GF->Shutdown([](FGameFlowError Err) { /* ... */ });
```

See [`examples/DedicatedServerSample/`](../examples/DedicatedServerSample/) for
a complete, copyable `AGameModeBase` subclass that shows the full pattern.

## API

### UGameFlowSubsystem — lifecycle (C++)

All lifecycle methods accept a completion delegate that fires on the game thread.

| Method                                         | Description                                                                   |
| ---------------------------------------------- | ----------------------------------------------------------------------------- |
| `Start(FGfVoidResult)`                         | Connect to the runtime with retries, or fall back to local mode.              |
| `Ready(FGfVoidResult)`                         | Mark the server ready; starts the health heartbeat in sidecar mode.           |
| `Shutdown(FGfVoidResult)`                      | End the match. Idempotent; afterwards calls return `NOT_CONNECTED`.           |
| `GetPayload(FGfPayloadResult)`                 | Fetch the opaque launch payload string (empty when none).                     |
| `GetInfo(FGfInfoResult)`                       | Fetch the current `FServerInfo` snapshot.                                     |
| `ConnectPlayer(SessionId, FGfVoidResult)`      | Register a player session. Errors: `SERVER_FULL`, `PLAYER_ALREADY_CONNECTED`. |
| `DisconnectPlayer(SessionId, FGfRemoveResult)` | Remove a player session. Idempotent.                                          |

### UGameFlowSubsystem — Blueprint events (UPROPERTY BlueprintAssignable)

| Event              | Signature                          | When it fires                                  |
| ------------------ | ---------------------------------- | ---------------------------------------------- |
| `OnServerInfo`     | `(FServerInfo Info)`               | Every server-info frame from the watch stream. |
| `OnPayloadChanged` | `(FString Payload, bool bPresent)` | Only when the launch payload changes.          |
| `OnHealthDegraded` | `()`                               | After repeated health-ping failures.           |

### UGameFlowSubsystem — synchronous reads (BlueprintPure)

| Property / Method    | Type              | Description                                     |
| -------------------- | ----------------- | ----------------------------------------------- |
| `PlayerCount()`      | `int32`           | Number of currently connected players.          |
| `PlayerSessionIds()` | `TArray<FString>` | Session IDs of all connected players.           |
| `PlayerCapacity()`   | `int64`           | Max capacity reported by the runtime.           |
| `PlayersTracked()`   | `bool`            | Whether player tracking is enabled.             |
| `GetMode()`          | `EGameFlowMode`   | `Sidecar` or `Local`, resolved after `Start()`. |

### FGameFlowOptions (C++ only)

Pass to `FGameFlowClient` directly for advanced configuration. `UGameFlowSubsystem`
uses defaults; override by constructing `FGameFlowClient` manually and calling
`Initialize()` before using the subsystem.

| Field              | Default     | Description                                                  |
| ------------------ | ----------- | ------------------------------------------------------------ |
| `Mode`             | auto-detect | Force `Sidecar` or `Local`.                                  |
| `ConnectTimeoutMs` | `30000`     | Max time to connect before `SIDECAR_UNAVAILABLE`.            |
| `RequestTimeoutMs` | `3000`      | Per-request HTTP timeout.                                    |
| `HealthIntervalMs` | `5000`      | Health ping cadence (clamped to ≥ 500 ms).                   |
| `Logger`           | UE_LOG      | Inject a custom `IGameFlowLogger`.                           |
| `Dispatcher`       | AsyncTask   | Custom `IGameFlowDispatcher` for callback delivery.          |
| `OnHealthDegraded` | —           | C++ callback for health-degraded (alternative to the event). |

## Error codes

All fallible operations return an `FGameFlowError` with one of these stable codes:

| Code                       | When                                                              |
| -------------------------- | ----------------------------------------------------------------- |
| `SIDECAR_UNAVAILABLE`      | `Start()` timed out probing the runtime.                          |
| `PLAYER_ALREADY_CONNECTED` | `ConnectPlayer()` with an already-tracked session ID.             |
| `SERVER_FULL`              | `ConnectPlayer()` when the player list is at capacity.            |
| `PLAYER_TRACKING_DISABLED` | Player operation when the runtime has tracking turned off.        |
| `NOT_CONNECTED`            | Any call before `Start()` or after `Shutdown()`.                  |
| `REQUEST_FAILED`           | Any HTTP-level failure (unexpected status, timeout, parse error). |

Check `Error.IsOk()` before proceeding, and branch on `Error.Code` for
recoverable conditions.

## Local mode

Off GameFlow (your machine, CI, playtesting) the SDK runs in **local mode**
automatically — lifecycle calls succeed immediately, the health loop is a
no-op, and player tracking works against an in-memory list.

Configure via environment variables before launching the server:

| Variable               | Effect                                                               |
| ---------------------- | -------------------------------------------------------------------- |
| `GAMEFLOW_SDK_MODE`    | Set to `local` to force local mode; `sidecar` to force sidecar mode. |
| `GAMEFLOW_MAX_PLAYERS` | Player-list capacity (unset = unlimited, `0` = tracking disabled).   |
| `GAMEFLOW_PAYLOAD`     | Simulates the launch payload string (match config, map name, etc.).  |

The same binary runs on GameFlow and on your machine with no code changes.

## Dedicated server build

Build with the **Dedicated Server** target to strip the editor and client
modules. The plugin itself has no client-only code, but running
`HasAuthority()` before calling SDK methods is good practice to ensure
SDK calls happen only on the server process.

## Development

Conformance tests (35 automation tests) run via:

```bash
bash sdk/unreal/scripts/run-tests.sh GameFlow
# or via Task:
task ci:unreal
task test:unreal
```

Tests require a Node runtime for the shared fake-runtime fixture in
`tools/conformance/`.

## License

[Apache-2.0](LICENSE.md)
