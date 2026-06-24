# Changelog

All notable changes to the GameFlow Unreal SDK are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## 0.1.0 — 2026-06-22

Initial beta release.

### Added

- `GameFlowCore` module: engine-free C++ library with full GameFlow lifecycle
  support — `FGameFlowClient` (connect with retries + exponential backoff,
  ready, shutdown, payload, info, watch, player tracking), `FGameFlowPlayers`
  (connect / disconnect / synchronous cache reads), `FGfWatcher` (shared NDJSON
  watch stream over `FSocket`, lazy open/close, automatic reconnect),
  `FGfHealthLoop` (5 s cadence, no-overlap, degrade-once after 6 failures),
  `FGfModeDetection` (explicit option → `GAMEFLOW_SDK_MODE` → auto-detect),
  `FGameFlowEnvReader`, `FGameFlowError` with 6 stable error codes
  (`SIDECAR_UNAVAILABLE`, `PLAYER_ALREADY_CONNECTED`, `SERVER_FULL`,
  `PLAYER_TRACKING_DISABLED`, `NOT_CONNECTED`, `REQUEST_FAILED`),
  local-mode stub (`GAMEFLOW_MAX_PLAYERS`, `GAMEFLOW_PAYLOAD`).

- `GameFlowUnreal` module: `UGameFlowSubsystem : UGameInstanceSubsystem` with
  Blueprint-assignable events (`OnServerInfo`, `OnPayloadChanged`,
  `OnHealthDegraded`), `BlueprintPure` reads (`PlayerCount`,
  `PlayerSessionIds`, `PlayerCapacity`, `PlayersTracked`, `GetMode`), and
  C++ lifecycle methods (`Start`, `Ready`, `Shutdown`, `GetPayload`,
  `GetInfo`, `ConnectPlayer`, `DisconnectPlayer`).

- 35 automation conformance tests (green) covering the full spec: transport
  round-trips, retry/backoff, mode detection, local mode, health cadence,
  player-tracking error codes, payload change events, watch reconnect, and
  shutdown idempotency.

- `examples/DedicatedServerSample/` — copyable `AGameModeBase` subclass
  showing the full lifecycle pattern.

- Targets Unreal Engine 5.3+ (developed and tested on UE 5.8.0).
