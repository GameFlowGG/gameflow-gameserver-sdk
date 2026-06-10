# GameFlow Game Server SDK: cross-language behavioral spec

Audience: SDK implementers (TypeScript, Unity, Unreal, Godot, Rust, Go, Python). Users never need this document.

The API surface is defined in [`proto/gameflow/sdk/v1/sdk.proto`](../proto/gameflow/sdk/v1/sdk.proto). This document defines the behavior that protobuf cannot capture. An implementation is conformant when it exposes the proto surface (naming adapted to its language idiom) with the behaviors below, verified against the fake runtime fixture (`sdk/typescript/test/fixtures/fake-sidecar.ts`, to be extracted to `tools/conformance/` when the second SDK lands).

## Transport (v1)

In v1 every SDK talks REST to the local Agones sidecar that GameFlow runs next to the game server container. This is an implementation detail: nothing Agones-related may appear in any public API, log line, error message or user-facing doc.

- Base URL: `http://127.0.0.1:${AGONES_SDK_HTTP_PORT}`, default port 9358.
- Endpoints: `POST /ready`, `POST /health`, `POST /shutdown` (body `{}`); `GET /gameserver`; `GET /watch/gameserver` (NDJSON stream, each line `{"result": {...}}` or `{"error": {...}}`); players list under `GET /v1beta1/lists/players`, `POST /v1beta1/lists/players:addValue` and `POST /v1beta1/lists/players:removeValue` (body `{"value": "<sessionId>"}`).
- The `:addValue`/`:removeValue` paths contain a literal `:`; do not URL-encode it.
- int64 fields (list `capacity`, timestamps) arrive as JSON **strings**; coerce to numbers.
- Per-request timeout: 3000 ms default, configurable. The watch stream has no timeout, only cancellation.
- Tested against Agones 1.54. Record the tested version per release.

## Connect

- Probe: `GET /gameserver`, retried with exponential backoff 250 ms doubling to a 4000 ms cap, plus or minus 20% jitter, until success or the connect timeout (default 30000 ms) expires. On expiry fail with the `SIDECAR_UNAVAILABLE` error carrying the last underlying cause.
- The successful probe response seeds the player list cache and the last-known payload.
- If the players list is absent from the probe response (game configured with max players = 0), log a prominent warning once: tracking is disabled, the platform cannot see player counts, idle servers may be shut down.

## Mode detection

- Explicit option (`sidecar` | `local`) wins over the `GAMEFLOW_SDK_MODE` env var, which wins over auto-detection.
- Auto: if `AGONES_SDK_HTTP_PORT` is set, use sidecar mode and **fail hard** when unreachable (a real pod silently falling back to local would pass `ready()` locally and never become ready on the platform). If unset, enter local mode with a single info log.

## Local mode

Lifecycle calls are no-ops (debug logs). Player tracking runs against an in-memory list with capacity from `GAMEFLOW_MAX_PLAYERS` (unset = unlimited, `0` = tracking disabled, mirroring production). `GAMEFLOW_PAYLOAD` simulates the launch payload. Watch emits a synthetic server object on every local mutation. The health loop does not run.

## Health heartbeat

- Starts automatically when `ready()` succeeds (sidecar mode only); users must never need to call health themselves.
- Default cadence 5000 ms, configurable, clamped to no less than 500 ms. Schedule the next ping only after the previous one settles, so pings never overlap or pile up.
- Failures: log a warning and keep pinging at the normal cadence (no backoff; the platform tolerates roughly 3 missed minutes). After 6 consecutive failures report degraded health exactly once (error log plus optional user callback) and recover silently on the next success.
- The heartbeat must never keep the host process alive (unref timers or equivalent) and must never throw.

## Players

- List key is `players`. `capacity` is set by the platform; SDKs never modify it.
- Mutations go to the runtime; the returned list becomes the local cache so count/list/capacity reads are synchronous.
- `connect(sessionId)` errors: HTTP 409 maps to `PLAYER_ALREADY_CONNECTED`; HTTP 400 with gRPC code 11 (OUT_OF_RANGE) maps to `SERVER_FULL` (enrich with cached capacity); HTTP 404 maps to `PLAYER_TRACKING_DISABLED`.
- `disconnect(sessionId)`: HTTP 404 means the value was not present; resolve `false` (idempotent). When tracking is known to be disabled, fail with `PLAYER_TRACKING_DISABLED` instead.
- Naming may be adapted per language (e.g. Godot, where `connect` collides with signals, may use `track`/`untrack`), but semantics must match.

## Payload

- The payload is the `GAMEFLOW_PAYLOAD` annotation on the server object, exposed as an opaque string (no parsing). Absent annotation = no payload.
- It can change after connect (match assignment): expose a payload-change event derived from the watch stream, firing only when the value actually differs.

## Watch

- One underlying stream shared by all subscribers, opened lazily on the first subscriber, closed when the last unsubscribes and on shutdown.
- Lines may be split across network chunks; frame by newline. Skip `{"error": ...}` lines with a warning. Reconnect on stream end or failure with the connect backoff parameters, resetting backoff after any received message.

## Shutdown

- Idempotent: repeated calls resolve immediately, only one request is sent.
- Order: stop the health loop and the watch stream first, then send shutdown. A failed shutdown request is logged, not thrown.
- After shutdown every other method fails with `NOT_CONNECTED`.

## Errors

Stable codes, identical across languages: `SIDECAR_UNAVAILABLE`, `PLAYER_ALREADY_CONNECTED`, `SERVER_FULL`, `PLAYER_TRACKING_DISABLED`, `NOT_CONNECTED`, `REQUEST_FAILED`. Error types and messages are idiomatic per language; codes are contract.

## Environment

Platform-provided: `GAMEFLOW_DEFAULT_PORT`, `GAMEFLOW_<NAME>_PORT` (name uppercased, spaces to underscores), `GAMEFLOW_TLS_DEFAULT_PORT` / `GAMEFLOW_TLS_<NAME>_PORT`, `GAMEFLOW_REGION`, `GAMEFLOW_BUILD_ID`. Port helpers return absent values rather than throwing.

SDK-read: `GAMEFLOW_SDK_MODE`, `GAMEFLOW_MAX_PLAYERS` (local), `GAMEFLOW_PAYLOAD` (local), `AGONES_SDK_HTTP_PORT` (transport detail, never documented to users).

## Logging

Prefix every line with `[gameflow]` or the platform-idiomatic equivalent. Logger must be injectable and fully silenceable. Default level: lifecycle transitions at info, recoverable problems at warn, degraded health at error, everything else at debug.
