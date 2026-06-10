extends RefCounted
# Internal: simulated runtime for local development (no GameFlow involved).
# Lifecycle calls are no-ops, player tracking runs against an in-memory list
# so the rest of the SDK behaves exactly like in production.
#
# Simulation knobs via env vars:
# - GAMEFLOW_MAX_PLAYERS: capacity of the in-memory players list. Unset means
#   unlimited; 0 simulates a game with player tracking disabled.
# - GAMEFLOW_PAYLOAD: simulates the launch payload.

const _Raw := preload("raw.gd")

const UNLIMITED := -1

var _logger: GameFlowLogger
var _capacity := UNLIMITED
var _players: Array[String] = []
var _watchers: Array[Callable] = []
var _state := "Scheduled"


func _init(logger: GameFlowLogger) -> void:
	_logger = logger
	var raw := OS.get_environment("GAMEFLOW_MAX_PLAYERS").strip_edges()
	if raw != "":
		_capacity = maxi(raw.to_int(), 0) if raw.is_valid_int() else 0


func ready() -> GameFlowResult:
	_state = "Ready"
	_logger.debug("local: ready()")
	_emit()
	return GameFlowResult.success()


func health() -> GameFlowResult:
	_logger.debug("local: health ping")
	return GameFlowResult.success()


func shutdown() -> GameFlowResult:
	_state = "Shutdown"
	_logger.debug("local: shutdown()")
	_emit()
	return GameFlowResult.success()


func get_game_server() -> GameFlowResult:
	return GameFlowResult.success(_build_game_server())


func get_player_list() -> GameFlowResult:
	return GameFlowResult.success(_snapshot())


func add_player(session_id: String) -> GameFlowResult:
	if not _tracking_enabled():
		return GameFlowResult.failure(
			GameFlowResult.PLAYER_TRACKING_DISABLED, _Raw.TRACKING_DISABLED_MESSAGE
		)
	if _players.has(session_id):
		return GameFlowResult.failure(
			GameFlowResult.PLAYER_ALREADY_CONNECTED, 'player "%s" is already connected' % session_id
		)
	if _capacity != UNLIMITED and _players.size() >= _capacity:
		var failed := GameFlowResult.failure(
			GameFlowResult.SERVER_FULL, "server is full (capacity %d)" % _capacity
		)
		failed.capacity = _capacity
		return failed
	_players.append(session_id)
	_emit()
	return GameFlowResult.success(_snapshot())


func remove_player(session_id: String) -> GameFlowResult:
	if not _tracking_enabled():
		return GameFlowResult.success(null)
	var index := _players.find(session_id)
	if index < 0:
		return GameFlowResult.success(null)
	_players.remove_at(index)
	_emit()
	return GameFlowResult.success(_snapshot())


# Watch emits a synthetic server object on every local mutation.
func watch_game_server(on_update: Callable, cancel) -> GameFlowResult:
	on_update.call(_build_game_server())
	_watchers.append(on_update)
	while not cancel.cancelled:
		await _Raw.frame()
	_watchers.erase(on_update)
	return GameFlowResult.success()


func _tracking_enabled() -> bool:
	return _capacity == UNLIMITED or _capacity > 0


func _snapshot() -> Dictionary:
	if not _tracking_enabled():
		return _Raw.empty_snapshot()
	return {"exists": true, "capacity": _capacity, "values": _players.duplicate()}


func _emit() -> void:
	var gs := _build_game_server()
	for watcher in _watchers.duplicate():
		watcher.call(gs)


func _build_game_server() -> Dictionary:
	var annotations := {}
	if OS.has_environment("GAMEFLOW_PAYLOAD"):
		annotations[_Raw.PAYLOAD_ANNOTATION] = OS.get_environment("GAMEFLOW_PAYLOAD")
	var status := {"state": _state, "address": "127.0.0.1", "ports": []}
	if _tracking_enabled():
		status["lists"] = {
			_Raw.PLAYERS_LIST: {"capacity": _capacity, "values": _players.duplicate()}
		}
	return {
		"object_meta": {"name": "local-gameserver", "annotations": annotations, "labels": {}},
		"status": status,
	}
