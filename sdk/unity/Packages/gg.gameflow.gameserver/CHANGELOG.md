# Changelog

All notable changes to this package are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.0.0/) and the package uses semantic versioning.

## [0.1.0] - 2026-06-17

Initial beta.

### Added

- `GameFlowClient` lifecycle: `Start` (connect with retries or local fallback), `Ready`
  (automatic health heartbeat), `Shutdown` (idempotent).
- Player tracking via `GameFlowClient.Players` with a synchronous local cache.
- `Payload`, `Info`, `Watch`, and `OnPayloadChange`.
- Typed exceptions with stable, cross-SDK error codes.
- `GameFlowEnv` port/region/build helpers.
- Local mode for off-platform development (`GAMEFLOW_MAX_PLAYERS`, `GAMEFLOW_PAYLOAD`).
- Optional `GameFlowRunner` MonoBehaviour (main-thread dispatch + shutdown-on-quit) and
  `UnityDebugLogger`.
- Dedicated Server sample.

Verified against the shared cross-SDK conformance fixture. Tested with Agones 1.54 transport
semantics.
