class_name GameFlowPlayers
extends RefCounted

## Player tracking over the platform's players list. Mutations always go to
## the runtime; the returned list is kept as an authoritative cache so
## count()/list()/capacity() are synchronous.
##
## (Named track/untrack rather than connect/disconnect because those names
## collide with Godot's signal methods; semantics match the other SDKs.)

const _Raw := preload("internal/raw.gd")

var _transport
var _shared := {}
var _snapshot := {"exists": false, "capacity": 0, "values": []}


## Registers a player session. Call when a player joins the server.
## Fails with PLAYER_ALREADY_CONNECTED, SERVER_FULL (carrying the capacity)
## or PLAYER_TRACKING_DISABLED.
func track(session_id: String) -> GameFlowResult:
	var guard := _guard()
	if guard != null:
		return guard
	var res: GameFlowResult = await _transport.add_player(session_id)
	if res.ok:
		_snapshot = res.value
		return GameFlowResult.success()
	if res.code == GameFlowResult.PLAYER_TRACKING_DISABLED:
		_snapshot = _Raw.empty_snapshot()
	# Enrich SERVER_FULL with the cached capacity when the wire had none
	if res.code == GameFlowResult.SERVER_FULL and res.capacity < 0:
		res.capacity = int(_snapshot.capacity)
	return res


## Unregisters a player session. Resolves with value=false when the player was
## not in the list (safe to call on every disconnect, including duplicates).
func untrack(session_id: String) -> GameFlowResult:
	var guard := _guard()
	if guard != null:
		return guard
	if not _snapshot.exists:
		return GameFlowResult.failure(
			GameFlowResult.PLAYER_TRACKING_DISABLED, _Raw.TRACKING_DISABLED_MESSAGE
		)
	var res: GameFlowResult = await _transport.remove_player(session_id)
	if not res.ok:
		return res
	if res.value == null:
		return GameFlowResult.success(false)
	_snapshot = res.value
	return GameFlowResult.success(true)


## Current number of connected players (from the local cache).
func count() -> int:
	return (_snapshot.values as Array).size()


## Session ids of connected players (from the local cache).
func list() -> Array:
	return (_snapshot.values as Array).duplicate()


## Maximum players configured for this game. -1 means unlimited (local mode
## without GAMEFLOW_MAX_PLAYERS).
func capacity() -> int:
	return int(_snapshot.capacity)


## False when the game was created with max players = 0.
func tracking_enabled() -> bool:
	return bool(_snapshot.exists)


## Re-reads the list from the runtime.
func refresh() -> GameFlowResult:
	var guard := _guard()
	if guard != null:
		return guard
	var res: GameFlowResult = await _transport.get_player_list()
	if not res.ok:
		return res
	_snapshot = res.value
	return GameFlowResult.success()


func _guard() -> GameFlowResult:
	if _shared.get("state", "idle") in ["idle", "shutting-down", "shutdown"]:
		return GameFlowResult.failure(GameFlowResult.NOT_CONNECTED, "the SDK is not connected")
	return null


# Internal: updates the cache from a game server object (seed and watch).
func _sync_from_game_server(gs: Dictionary) -> void:
	var lists: Variant = _Raw.lists_of(gs)
	var raw: Variant = null
	if lists is Dictionary:
		raw = (lists as Dictionary).get(_Raw.PLAYERS_LIST)
	_snapshot = _Raw.parse_list(raw)
