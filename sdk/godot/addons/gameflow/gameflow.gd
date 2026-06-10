class_name GameFlow
extends RefCounted

## GameFlow game server SDK: server lifecycle, automatic health reporting and
## player tracking for dedicated servers hosted on https://gameflow.gg.
##
## [codeblock]
## var gf := GameFlow.new()
##
## func _ready() -> void:
##     var res := await gf.start()
##     if not res.ok:
##         push_error("gameflow: %s" % res.message)
##         return
##     # start listening on gf.ports.default, then:
##     await gf.ready()  # health reporting starts automatically
##
##     await gf.players.track(session_id)    # when a player joins
##     await gf.players.untrack(session_id)  # when a player leaves
##
##     await gf.shutdown()  # when the match ends
## [/codeblock]
##
## All fallible methods return a [GameFlowResult] instead of failing; check
## [code]res.ok[/code] / [code]res.code[/code].

## Fired once when health pings have been failing for a sustained period.
signal health_degraded

const MODE_SIDECAR := "sidecar"
const MODE_LOCAL := "local"

const DEFAULT_SIDECAR_PORT := 9358
const DEFAULT_HEALTH_INTERVAL_MS := 5000
const MIN_HEALTH_INTERVAL_MS := 500
const DEFAULT_CONNECT_TIMEOUT_MS := 30000
const DEFAULT_REQUEST_TIMEOUT_MS := 3000

const _Raw := preload("internal/raw.gd")
const _Env := preload("internal/env.gd")
const _SidecarTransport := preload("internal/transport_sidecar.gd")
const _LocalTransport := preload("internal/transport_local.gd")
const _HealthLoop := preload("internal/health_loop.gd")
const _Watcher := preload("internal/watcher.gd")

## Player tracking.
var players: GameFlowPlayers

## Transport in use after start(): "sidecar" when running on GameFlow,
## "local" otherwise.
var mode := ""

## Ports assigned to this server (from GAMEFLOW_*_PORT env vars).
var ports: GameFlowPorts = GameFlowPorts.create()

## Region this server runs in ("" when not provided by the platform).
var region: String:
	get:
		return _Env.region()

## Build id of the running image ("" when not provided by the platform).
var build_id: String:
	get:
		return _Env.build_id()

var _logger: GameFlowLogger = GameFlowLogger.new()
var _transport
var _health
var _watcher
var _shared := {"state": "idle"}
var _started := false
var _last_payload: Variant = null
var _payload_listeners: Array[Callable] = []


func _init() -> void:
	players = GameFlowPlayers.new()
	players._shared = _shared


## Connects to the GameFlow runtime (with retries) or falls back to local
## mode when the server is not running on GameFlow.
##
## Options (all optional):
## [codeblock]
## {
##     "mode": "auto",                # "auto" | "sidecar" | "local"
##     "health_interval_ms": 5000,    # minimum 500
##     "connect_timeout_ms": 30000,   # total budget including retries
##     "request_timeout_ms": 3000,    # per-request timeout
##     "logger": GameFlowLogger.new() # null silences the SDK
## }
## [/codeblock]
func start(options: Dictionary = {}) -> GameFlowResult:
	if _started:
		return GameFlowResult.failure(
			GameFlowResult.REQUEST_FAILED, "start() was already called on this instance"
		)
	_started = true
	_logger = _resolve_logger(options)
	mode = _resolve_mode(options)

	var request_timeout_ms := int(options.get("request_timeout_ms", DEFAULT_REQUEST_TIMEOUT_MS))
	if mode == MODE_LOCAL:
		_transport = _LocalTransport.new(_logger)
	else:
		var sidecar_port := DEFAULT_SIDECAR_PORT
		var env_port := OS.get_environment("AGONES_SDK_HTTP_PORT").strip_edges()
		if env_port.is_valid_int() and env_port.to_int() > 0:
			sidecar_port = env_port.to_int()
		_transport = _SidecarTransport.new(sidecar_port, request_timeout_ms, _logger)
	players._transport = _transport
	_watcher = _Watcher.new(_transport, _logger)
	_watcher.on_update = _handle_update
	var interval_ms := maxi(
		MIN_HEALTH_INTERVAL_MS, int(options.get("health_interval_ms", DEFAULT_HEALTH_INTERVAL_MS))
	)
	_health = _HealthLoop.new(
		_transport, interval_ms, _logger, func() -> void: health_degraded.emit()
	)

	var probe: GameFlowResult = await _probe_with_retry(
		int(options.get("connect_timeout_ms", DEFAULT_CONNECT_TIMEOUT_MS))
	)
	if not probe.ok:
		return probe
	_shared.state = "connected"
	var gs: Dictionary = probe.value
	players._sync_from_game_server(gs)
	_last_payload = _Raw.payload_of(gs)
	if mode == MODE_SIDECAR and not players.tracking_enabled():
		_logger.warn(
			"player tracking is disabled for this server (max players is 0). The platform cannot "
			+ "see player counts and idle servers with no trackable players may be shut down. "
			+ 'Set "Max Players per Server" in your game settings to enable tracking.'
		)
	_logger.info("connected (mode: %s)" % mode)
	return GameFlowResult.success()


## Marks the server ready to accept players and starts the automatic health
## heartbeat. Call once your server is listening.
func ready() -> GameFlowResult:
	var guard := _assert_usable()
	if guard != null:
		return guard
	if _shared.state == "ready":
		_logger.debug("ready() called more than once; ignoring")
		return GameFlowResult.success()
	var res: GameFlowResult = await _transport.ready()
	if not res.ok:
		return res
	_shared.state = "ready"
	if mode == MODE_SIDECAR:
		_health.start()
	_logger.info("server marked ready")
	return GameFlowResult.success()


## Shuts the server down cleanly. Idempotent. After this resolves the platform
## will terminate the server process, and every other SDK call fails with
## NOT_CONNECTED.
func shutdown() -> GameFlowResult:
	if _shared.state in ["shutting-down", "shutdown"]:
		return GameFlowResult.success()
	if not _started:
		_shared.state = "shutdown"
		return GameFlowResult.success()
	_shared.state = "shutting-down"
	_health.stop()
	_watcher.stop()
	var res: GameFlowResult = await _transport.shutdown()
	if not res.ok:
		_logger.warn("shutdown request failed: %s" % res.message)
	_shared.state = "shutdown"
	_watcher.on_update = Callable()
	_logger.info("server shut down")
	return GameFlowResult.success()


## The launch payload for this server: an opaque string set when the server
## was requested, in [code]res.value[/code] (null when none was provided).
## May change when the server is assigned to a new match; use
## on_payload_change() to react.
func payload() -> GameFlowResult:
	var guard := _assert_usable()
	if guard != null:
		return guard
	var res: GameFlowResult = await _transport.get_game_server()
	if not res.ok:
		return res
	_last_payload = _Raw.payload_of(res.value)
	return GameFlowResult.success(_last_payload)


## Current server details, as a [GameFlowServerInfo] in [code]res.value[/code].
func info() -> GameFlowResult:
	var guard := _assert_usable()
	if guard != null:
		return guard
	var res: GameFlowResult = await _transport.get_game_server()
	if not res.ok:
		return res
	return GameFlowResult.success(GameFlowServerInfo.from_raw(res.value))


## Subscribes to server updates. The listener receives a [GameFlowServerInfo].
## Returns an unsubscribe Callable. The stream is shared across subscribers
## and reconnects automatically.
func watch(listener: Callable) -> Callable:
	var guard := _assert_usable()
	if guard != null:
		_logger.error("watch(): %s" % guard.message)
		return func() -> void: pass
	return _watcher.subscribe(
		func(gs: Dictionary) -> void: listener.call(GameFlowServerInfo.from_raw(gs))
	)


## Fires the listener with the new payload (String or null) when the launch
## payload changes (e.g. on match assignment). Returns an unsubscribe Callable.
func on_payload_change(listener: Callable) -> Callable:
	var guard := _assert_usable()
	if guard != null:
		_logger.error("on_payload_change(): %s" % guard.message)
		return func() -> void: pass
	_payload_listeners.append(listener)
	var unsubscribe_watch: Callable = _watcher.subscribe(func(_gs: Dictionary) -> void: pass)
	var state := {"unsubscribed": false}
	return func() -> void:
		if state.unsubscribed:
			return
		state.unsubscribed = true
		_payload_listeners.erase(listener)
		unsubscribe_watch.call()


func _probe_with_retry(connect_timeout_ms: int) -> GameFlowResult:
	var deadline := Time.get_ticks_msec() + connect_timeout_ms
	var backoff_ms := 250
	var last_message := ""
	while true:
		var res: GameFlowResult = await _transport.get_game_server()
		if res.ok:
			return res
		last_message = res.message
		var wait_ms := _jitter(backoff_ms)
		if Time.get_ticks_msec() + wait_ms >= deadline:
			break
		_logger.debug("runtime not reachable yet; retrying in %dms" % wait_ms)
		await _Raw.sleep_ms(wait_ms)
		backoff_ms = mini(backoff_ms * 2, 4000)
	return GameFlowResult.failure(
		GameFlowResult.SIDECAR_UNAVAILABLE,
		(
			"could not connect to the GameFlow runtime within %dms: %s"
			% [connect_timeout_ms, last_message]
		)
	)


func _handle_update(gs: Dictionary) -> void:
	players._sync_from_game_server(gs)
	var new_payload: Variant = _Raw.payload_of(gs)
	if new_payload != _last_payload:
		_last_payload = new_payload
		for listener in _payload_listeners.duplicate():
			listener.call(new_payload)


func _assert_usable() -> GameFlowResult:
	if _shared.state == "idle":
		return GameFlowResult.failure(GameFlowResult.NOT_CONNECTED, "call start() first")
	if _shared.state in ["shutting-down", "shutdown"]:
		return GameFlowResult.failure(GameFlowResult.NOT_CONNECTED, "the SDK has been shut down")
	return null


func _resolve_mode(options: Dictionary) -> String:
	var option := str(options.get("mode", "auto"))
	if option == MODE_LOCAL or option == MODE_SIDECAR:
		return option
	var env_mode := OS.get_environment("GAMEFLOW_SDK_MODE")
	if env_mode == MODE_LOCAL or env_mode == MODE_SIDECAR:
		return env_mode
	if OS.get_environment("AGONES_SDK_HTTP_PORT") != "":
		return MODE_SIDECAR
	_logger.info(
		"no GameFlow runtime detected; running in local mode "
		+ "(lifecycle and player tracking are simulated)"
	)
	return MODE_LOCAL


static func _resolve_logger(options: Dictionary) -> GameFlowLogger:
	if options.has("logger"):
		var logger: Variant = options.get("logger")
		if logger == null:
			return GameFlowLogger.silent()
		if logger is GameFlowLogger:
			return logger
	return GameFlowLogger.new()


static func _jitter(ms: int) -> int:
	return int(round(ms * (0.8 + randf() * 0.4)))
