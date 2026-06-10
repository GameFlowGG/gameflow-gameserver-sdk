extends RefCounted
# Internal: REST transport to the local GameFlow runtime. Requests are
# frame-paced HTTPClient calls against 127.0.0.1; paths with custom verbs
# (lists/players:addValue) keep their literal ':'. Not part of the public API.

const _Raw := preload("raw.gd")

const GRPC_OUT_OF_RANGE := 11

var _port: int
var _request_timeout_ms: int
var _logger: GameFlowLogger


func _init(port: int, request_timeout_ms: int, logger: GameFlowLogger) -> void:
	_port = port
	_request_timeout_ms = request_timeout_ms
	_logger = logger


func ready() -> GameFlowResult:
	return await _request(HTTPClient.METHOD_POST, "/ready", "{}")


func health() -> GameFlowResult:
	return await _request(HTTPClient.METHOD_POST, "/health", "{}")


func shutdown() -> GameFlowResult:
	return await _request(HTTPClient.METHOD_POST, "/shutdown", "{}")


func get_game_server() -> GameFlowResult:
	var res: GameFlowResult = await _request(HTTPClient.METHOD_GET, "/gameserver", "")
	if not res.ok:
		return res
	return GameFlowResult.success(_Raw.normalize_game_server(res.value.body))


func get_player_list() -> GameFlowResult:
	var res: GameFlowResult = await _request(
		HTTPClient.METHOD_GET, "/v1beta1/lists/" + _Raw.PLAYERS_LIST, "", [404]
	)
	if not res.ok:
		return res
	if res.value.status == 404:
		return GameFlowResult.success(_Raw.empty_snapshot())
	return GameFlowResult.success(_Raw.parse_list(res.value.body))


func add_player(session_id: String) -> GameFlowResult:
	var res: GameFlowResult = await _request(
		HTTPClient.METHOD_POST,
		"/v1beta1/lists/%s:addValue" % _Raw.PLAYERS_LIST,
		JSON.stringify({"value": session_id}),
		[400, 404, 409]
	)
	if not res.ok:
		return res
	var status: int = res.value.status
	if status < 400:
		return GameFlowResult.success(_Raw.parse_list(res.value.body))
	if status == 409:
		return GameFlowResult.failure(
			GameFlowResult.PLAYER_ALREADY_CONNECTED, 'player "%s" is already connected' % session_id
		)
	if status == 404:
		return GameFlowResult.failure(
			GameFlowResult.PLAYER_TRACKING_DISABLED, _Raw.TRACKING_DISABLED_MESSAGE
		)
	# 400: OUT_OF_RANGE means the list is at capacity
	var gateway := _gateway_error(res.value.body)
	var gateway_message := str(gateway.get("message", ""))
	if (
		int(gateway.get("code", -1)) == GRPC_OUT_OF_RANGE
		or "capacity" in gateway_message.to_lower()
		or "out of range" in gateway_message.to_lower()
	):
		return GameFlowResult.failure(GameFlowResult.SERVER_FULL, "server is full")
	var failed := GameFlowResult.failure(
		GameFlowResult.REQUEST_FAILED,
		gateway_message if gateway_message != "" else "addValue failed with HTTP %d" % status
	)
	failed.status = status
	return failed


# Resolves success(null) when the player was not in the list (idempotent
# remove), success(snapshot) otherwise.
func remove_player(session_id: String) -> GameFlowResult:
	var res: GameFlowResult = await _request(
		HTTPClient.METHOD_POST,
		"/v1beta1/lists/%s:removeValue" % _Raw.PLAYERS_LIST,
		JSON.stringify({"value": session_id}),
		[404]
	)
	if not res.ok:
		return res
	if res.value.status == 404:
		return GameFlowResult.success(null)
	return GameFlowResult.success(_Raw.parse_list(res.value.body))


# Long-lived NDJSON stream: no per-request timeout, only cancellation.
# Resolves success when the stream ends, failure on transport errors.
func watch_game_server(on_update: Callable, cancel) -> GameFlowResult:
	var client := HTTPClient.new()
	var err := client.connect_to_host("127.0.0.1", _port)
	if err != OK:
		return _unreachable("/watch/gameserver", error_string(err))
	while client.get_status() in [HTTPClient.STATUS_CONNECTING, HTTPClient.STATUS_RESOLVING]:
		if cancel.cancelled:
			client.close()
			return GameFlowResult.success()
		client.poll()
		await _Raw.frame()
	if client.get_status() != HTTPClient.STATUS_CONNECTED:
		return _unreachable("/watch/gameserver", "status %d" % client.get_status())

	err = client.request(HTTPClient.METHOD_GET, "/watch/gameserver", PackedStringArray())
	if err != OK:
		client.close()
		return _unreachable("/watch/gameserver", error_string(err))
	while client.get_status() == HTTPClient.STATUS_REQUESTING:
		if cancel.cancelled:
			client.close()
			return GameFlowResult.success()
		client.poll()
		await _Raw.frame()
	if not client.has_response():
		client.close()
		return _unreachable("/watch/gameserver", "no response")
	var status := client.get_response_code()
	if status < 200 or status >= 300:
		client.close()
		var failed := GameFlowResult.failure(
			GameFlowResult.REQUEST_FAILED, "watch failed with HTTP %d" % status
		)
		failed.status = status
		return failed

	# Lines may be split across network chunks; frame by newline (byte 0x0A).
	var buffer := PackedByteArray()
	while client.get_status() == HTTPClient.STATUS_BODY:
		if cancel.cancelled:
			client.close()
			return GameFlowResult.success()
		client.poll()
		var chunk := client.read_response_body_chunk()
		if chunk.size() == 0:
			await _Raw.frame()
			continue
		buffer.append_array(chunk)
		var newline := _find_byte(buffer, 10)
		while newline >= 0:
			var line := buffer.slice(0, newline).get_string_from_utf8().strip_edges()
			buffer = buffer.slice(newline + 1)
			if line != "":
				_handle_watch_line(line, on_update)
			newline = _find_byte(buffer, 10)
	client.close()
	return GameFlowResult.success()


func _handle_watch_line(line: String, on_update: Callable) -> void:
	var parsed: Variant = JSON.parse_string(line)
	if not (parsed is Dictionary):
		_logger.warn("watch: skipping malformed stream line")
		return
	if parsed.has("error"):
		var stream_error: Variant = parsed.get("error")
		var error_message := "unknown"
		if stream_error is Dictionary:
			error_message = str(stream_error.get("message", "unknown"))
		_logger.warn("watch: stream error: %s" % error_message)
		return
	var result: Variant = parsed.get("result")
	if result is Dictionary:
		on_update.call(_Raw.normalize_game_server(result))


# Sends a request and maps failures to error codes. Statuses listed in
# `expected` are returned to the caller (in value.status) instead of failing.
func _request(method: int, path: String, body: String, expected: Array = []) -> GameFlowResult:
	var http: Dictionary = await _http(method, path, body, _request_timeout_ms)
	if http.error != "":
		return _unreachable(path, http.error)
	var status: int = http.status
	if (status >= 200 and status < 300) or expected.has(status):
		return GameFlowResult.success({"status": status, "body": http.body})
	var gateway := _gateway_error(http.body)
	var message := str(gateway.get("message", ""))
	if message == "":
		message = "%s failed with HTTP %d" % [path, status]
	var failed := GameFlowResult.failure(GameFlowResult.REQUEST_FAILED, message)
	failed.status = status
	return failed


# One frame-paced HTTP exchange on a fresh client (the sidecar is local, so
# connection setup is cheap and concurrent requests stay independent).
# Returns {"error": String, "status": int, "body": Variant}; a non-empty
# error means the runtime was unreachable or the deadline passed.
func _http(method: int, path: String, body: String, timeout_ms: int) -> Dictionary:
	var client := HTTPClient.new()
	var deadline := Time.get_ticks_msec() + timeout_ms
	var err := client.connect_to_host("127.0.0.1", _port)
	if err != OK:
		return {"error": "connect failed (%s)" % error_string(err), "status": 0, "body": null}
	while client.get_status() in [HTTPClient.STATUS_CONNECTING, HTTPClient.STATUS_RESOLVING]:
		client.poll()
		if Time.get_ticks_msec() >= deadline:
			client.close()
			return {"error": "timed out connecting", "status": 0, "body": null}
		await _Raw.frame()
	if client.get_status() != HTTPClient.STATUS_CONNECTED:
		return {"error": "could not connect (status %d)" % client.get_status(), "status": 0, "body": null}

	var headers := PackedStringArray()
	if body != "":
		headers.append("Content-Type: application/json")
	err = client.request(method, path, headers, body)
	if err != OK:
		client.close()
		return {"error": "request failed (%s)" % error_string(err), "status": 0, "body": null}
	while client.get_status() == HTTPClient.STATUS_REQUESTING:
		client.poll()
		if Time.get_ticks_msec() >= deadline:
			client.close()
			return {"error": "request timed out after %dms" % timeout_ms, "status": 0, "body": null}
		await _Raw.frame()
	if not client.has_response():
		client.close()
		return {"error": "connection lost (status %d)" % client.get_status(), "status": 0, "body": null}

	var status := client.get_response_code()
	var bytes := PackedByteArray()
	while client.get_status() == HTTPClient.STATUS_BODY:
		client.poll()
		var chunk := client.read_response_body_chunk()
		if chunk.size() > 0:
			bytes.append_array(chunk)
		else:
			if Time.get_ticks_msec() >= deadline:
				client.close()
				return {"error": "response timed out after %dms" % timeout_ms, "status": 0, "body": null}
			await _Raw.frame()
	client.close()
	var text := bytes.get_string_from_utf8()
	var parsed: Variant = JSON.parse_string(text) if text != "" else null
	return {"error": "", "status": status, "body": parsed}


func _unreachable(path: String, detail: String) -> GameFlowResult:
	return GameFlowResult.failure(
		GameFlowResult.SIDECAR_UNAVAILABLE,
		"could not reach the GameFlow runtime at http://127.0.0.1:%d (%s): %s" % [_port, path, detail]
	)


static func _gateway_error(body: Variant) -> Dictionary:
	return body if body is Dictionary else {}


static func _find_byte(buffer: PackedByteArray, byte: int) -> int:
	for i in buffer.size():
		if buffer[i] == byte:
			return i
	return -1
