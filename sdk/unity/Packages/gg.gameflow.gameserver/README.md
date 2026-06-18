# GameFlow Game Server SDK for Unity

Integrates Unity dedicated servers with [GameFlow](https://gameflow.gg): server lifecycle, automatic
health reporting and player tracking. A pure C# core (no `UnityEngine` dependency) with a thin Unity
layer; targets Unity 2022.3+ and the Dedicated Server build target.

```csharp
var runner = GameFlowRunner.Create();
var gf = new GameFlowClient(new GameFlowOptions {
    Logger = new UnityDebugLogger(),
    Dispatcher = runner.Dispatcher,
});
runner.Bind(gf);

await gf.Start();                       // connects (with retries) or falls back to local mode
// start listening on your transport, then:
await gf.Ready();                       // health reporting starts automatically

await gf.Players.Connect(sessionId);    // when a player joins
await gf.Players.Disconnect(sessionId); // when a player leaves

await gf.Shutdown();                    // when the match ends
```

## Install

Add the package via the Unity Package Manager (Window → Package Manager → **+** → _Add package from git URL_):

```
https://github.com/gameflow-gg/gameflow-gameserver-sdk.git?path=/sdk/unity/Packages/gg.gameflow.gameserver#sdk/unity/v0.1.0
```

or add it directly to `Packages/manifest.json`:

```json
"gg.gameflow.gameserver": "https://github.com/gameflow-gg/gameflow-gameserver-sdk.git?path=/sdk/unity/Packages/gg.gameflow.gameserver#sdk/unity/v0.1.0"
```

Requires Unity 2022.3+ with **Api Compatibility Level = .NET Standard 2.1**. A `Dedicated Server`
build is recommended for production. Import the **Dedicated Server** sample from the package page for
a copyable bootstrap.

## Usage

Create one `GameFlowClient` for the lifetime of the server process. Off GameFlow (your machine, CI)
the SDK runs in local mode automatically: lifecycle calls are simulated and player tracking works
against an in-memory list, so the same code runs everywhere with zero configuration.

The optional `GameFlowRunner` is a `MonoBehaviour` that delivers watch/payload callbacks on Unity's
main thread and sends a clean shutdown on quit. Headless code can skip it and use `GameFlowClient`
directly.

> Never block on the returned `Task` (`.Wait()` / `.Result`) from the main thread — `await` it.

### Errors

Fallible calls throw typed exceptions deriving from `GameFlowException`, each carrying a stable
`Code` shared across every GameFlow SDK:

```csharp
try {
    await gf.Players.Connect(sessionId);
} catch (ServerFullException e) {
    Kick(sessionId, $"server full (capacity {e.Capacity})");
} catch (PlayerAlreadyConnectedException) {
    // reconnect of a live session
}
```

Codes: `SidecarUnavailable`, `PlayerAlreadyConnected`, `ServerFull`, `PlayerTrackingDisabled`,
`NotConnected`, `RequestFailed`.

### API

| Member                                                          | Notes                                                                             |
| --------------------------------------------------------------- | --------------------------------------------------------------------------------- |
| `Start(CancellationToken)`                                      | Connects (with retries) or falls back to local mode. Call once.                   |
| `Ready()`                                                       | Marks the server ready; the automatic health heartbeat starts here.               |
| `Shutdown()`                                                    | Clean shutdown. Idempotent; afterwards every call throws `NotConnectedException`. |
| `Payload()`                                                     | Launch payload as an opaque string (`null` when none).                            |
| `Info()`                                                        | Current `ServerInfo` (name, state, address, ports, labels, annotations, players). |
| `Watch(listener)`                                               | Server updates; returns an `IDisposable` to unsubscribe.                          |
| `OnPayloadChange(listener)`                                     | Fires with the new payload when it changes (e.g. match assignment).               |
| `Players.Connect(id)` / `Players.Disconnect(id)`                | Register / unregister a session.                                                  |
| `Players.Count` / `List` / `Capacity` / `TrackingEnabled`       | Synchronous reads from the local cache.                                           |
| `GameFlowEnv.DefaultPort` / `Port(name)` / `Region` / `BuildId` | Platform-provided values; absent values return `null`.                            |

### Configuration

`GameFlowOptions` fields: `Mode` (auto / `Sidecar` / `Local`), `ConnectTimeoutMs` (30000),
`RequestTimeoutMs` (3000), `HealthIntervalMs` (5000, clamped ≥ 500), `Port`, `Logger`, `Dispatcher`,
`OnHealthDegraded`. Local mode reads `GAMEFLOW_MAX_PLAYERS` and `GAMEFLOW_PAYLOAD`.

## License

[Apache-2.0](LICENSE.md)
