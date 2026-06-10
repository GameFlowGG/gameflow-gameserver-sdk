extends RefCounted
# Internal: platform environment parsing. Not part of the public API.


# Returns int or null. Non-integer, empty, missing and non-positive values
# resolve to null rather than failing.
static func parse_port(value: String) -> Variant:
	var trimmed := value.strip_edges()
	if trimmed == "" or not trimmed.is_valid_int():
		return null
	var port := trimmed.to_int()
	return port if port > 0 else null


# Mirrors the platform's env var naming: port names are trimmed, spaces become
# underscores and the result is uppercased ("voice chat" -> VOICE_CHAT).
static func normalize_port_name(port_name: String) -> String:
	return port_name.strip_edges().replace(" ", "_").to_upper()


static func env_port(var_name: String) -> Variant:
	return parse_port(OS.get_environment(var_name))


static func region() -> String:
	return OS.get_environment("GAMEFLOW_REGION")


static func build_id() -> String:
	return OS.get_environment("GAMEFLOW_BUILD_ID")
