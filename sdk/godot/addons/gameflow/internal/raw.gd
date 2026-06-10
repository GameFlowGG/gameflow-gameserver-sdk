extends RefCounted
# Internal: wire-shape parsing and frame/sleep helpers shared by the SDK.
# Not part of the public API.

const PLAYERS_LIST := "players"
const PAYLOAD_ANNOTATION := "GAMEFLOW_PAYLOAD"
const TRACKING_DISABLED_MESSAGE := (
	'player tracking is disabled for this server. Set "Max Players per Server" '
	+ "to a value greater than 0 in your game settings on GameFlow"
)


static func empty_snapshot() -> Dictionary:
	return {"exists": false, "capacity": 0, "values": []}


# int64 fields (list capacity) arrive as JSON strings from the runtime;
# coerce defensively.
static func parse_list(raw: Variant) -> Dictionary:
	if not (raw is Dictionary):
		return empty_snapshot()
	var capacity_raw: Variant = raw.get("capacity", 0)
	var capacity := 0
	if capacity_raw is String:
		capacity = (capacity_raw as String).to_int()
	elif capacity_raw is int or capacity_raw is float:
		capacity = int(capacity_raw)
	var values := []
	var values_raw: Variant = raw.get("values")
	if values_raw is Array:
		for v in values_raw:
			values.append(str(v))
	return {"exists": true, "capacity": capacity, "values": values}


# The sidecar gateway marshals proto field names (object_meta); local mode
# builds them directly. Normalize at the transport boundary so the rest of
# the SDK only ever reads object_meta.
static func normalize_game_server(gs: Variant) -> Dictionary:
	if not (gs is Dictionary):
		return {}
	var dict: Dictionary = gs
	if not dict.has("object_meta") and dict.has("objectMeta"):
		dict = dict.duplicate()
		dict["object_meta"] = dict["objectMeta"]
	return dict


# The launch payload annotation, or null when absent.
static func payload_of(gs: Dictionary) -> Variant:
	var meta: Variant = gs.get("object_meta")
	if not (meta is Dictionary):
		return null
	var annotations: Variant = (meta as Dictionary).get("annotations")
	if not (annotations is Dictionary):
		return null
	return (annotations as Dictionary).get(PAYLOAD_ANNOTATION)


static func lists_of(gs: Dictionary) -> Variant:
	var status: Variant = gs.get("status")
	if not (status is Dictionary):
		return null
	return (status as Dictionary).get("lists")


static func frame() -> void:
	await (Engine.get_main_loop() as SceneTree).process_frame


static func sleep_ms(ms: int) -> void:
	if ms <= 0:
		await frame()
		return
	await (Engine.get_main_loop() as SceneTree).create_timer(ms / 1000.0, true, false, true).timeout


static func sleep_ms_cancellable(ms: int, cancel) -> void:
	var deadline := Time.get_ticks_msec() + ms
	while Time.get_ticks_msec() < deadline:
		if cancel.cancelled:
			return
		await frame()
