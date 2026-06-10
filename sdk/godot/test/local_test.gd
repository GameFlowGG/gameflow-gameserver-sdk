extends "res://test/test_base.gd"
# Local-mode behavioral tests (no GameFlow runtime required):
#   godot --headless --path sdk/godot --script res://test/local_test.gd


func run_tests() -> void:
	OS.set_environment("GAMEFLOW_DEFAULT_PORT", "7777")
	OS.set_environment("GAMEFLOW_VOICE_CHAT_PORT", "7878")
	OS.set_environment("GAMEFLOW_TLS_DEFAULT_PORT", "7443")
	OS.set_environment("GAMEFLOW_BAD_PORT", "not-a-port")
	OS.set_environment("GAMEFLOW_REGION", "eu-west-1")
	OS.set_environment("GAMEFLOW_BUILD_ID", "build-42")
	OS.set_environment("GAMEFLOW_MAX_PLAYERS", "2")
	OS.set_environment("GAMEFLOW_PAYLOAD", "hello")

	var gf := GameFlow.new()
	var res: GameFlowResult = await gf.start({"mode": "local", "logger": null})
	check(res.ok, "start() succeeds in local mode")
	check_eq(gf.mode, "local", "mode is local")

	# Environment
	check_eq(gf.ports.default, 7777, "default port parsed")
	check_eq(gf.ports.by_name("voice chat"), 7878, "named port normalized (voice chat)")
	check_eq(gf.ports.tls.default, 7443, "tls default port parsed")
	check_eq(gf.ports.by_name("bad"), null, "non-integer port resolves null")
	check_eq(gf.ports.by_name("missing"), null, "missing port resolves null")
	check_eq(gf.region, "eu-west-1", "region from env")
	check_eq(gf.build_id, "build-42", "build id from env")

	# Payload
	var payload_res: GameFlowResult = await gf.payload()
	check(payload_res.ok, "payload() ok")
	check_eq(payload_res.value, "hello", "payload from GAMEFLOW_PAYLOAD")

	# Players
	check_eq(gf.players.tracking_enabled(), true, "tracking enabled")
	check_eq(gf.players.capacity(), 2, "capacity from GAMEFLOW_MAX_PLAYERS")
	check((await gf.players.track("p1")).ok, "track p1")
	check((await gf.players.track("p2")).ok, "track p2")
	check_eq(gf.players.count(), 2, "count is 2")
	check(gf.players.list().has("p1"), "list contains p1")
	var dup: GameFlowResult = await gf.players.track("p1")
	check_eq(dup.code, GameFlowResult.PLAYER_ALREADY_CONNECTED, "duplicate track fails")
	var full: GameFlowResult = await gf.players.track("p3")
	check_eq(full.code, GameFlowResult.SERVER_FULL, "track over capacity fails")
	check_eq(full.capacity, 2, "SERVER_FULL carries capacity")

	# Watch + payload change (synthetic updates on local mutations)
	var states := []
	var unsubscribe := gf.watch(func(info: GameFlowServerInfo) -> void: states.append(info.state))
	var payloads := []
	var unsub_payload := gf.on_payload_change(func(p: Variant) -> void: payloads.append(p))
	OS.set_environment("GAMEFLOW_PAYLOAD", "v2")
	check((await gf.ready()).ok, "ready() ok")
	check((await gf.ready()).ok, "ready() is idempotent")
	check(await await_until(func() -> bool: return states.has("Ready")), "watch delivers Ready")
	check(
		await await_until(func() -> bool: return payloads.has("v2")),
		"payload change event fires with the new value"
	)
	var info_res: GameFlowResult = await gf.info()
	check(info_res.ok and info_res.value.state == "Ready", "info() reflects Ready state")

	var untracked: GameFlowResult = await gf.players.untrack("p2")
	check(untracked.ok and untracked.value == true, "untrack existing -> found")
	var ghost: GameFlowResult = await gf.players.untrack("ghost")
	check(ghost.ok and ghost.value == false, "untrack missing -> found=false (idempotent)")
	check_eq(gf.players.count(), 1, "count back to 1")
	unsubscribe.call()
	unsub_payload.call()

	# Shutdown
	check((await gf.shutdown()).ok, "shutdown ok")
	check((await gf.shutdown()).ok, "shutdown idempotent")
	var late: GameFlowResult = await gf.players.track("late")
	check_eq(late.code, GameFlowResult.NOT_CONNECTED, "track after shutdown -> NOT_CONNECTED")
	var late_info: GameFlowResult = await gf.info()
	check_eq(late_info.code, GameFlowResult.NOT_CONNECTED, "info after shutdown -> NOT_CONNECTED")

	# Tracking disabled (GAMEFLOW_MAX_PLAYERS=0)
	OS.set_environment("GAMEFLOW_MAX_PLAYERS", "0")
	var gf2 := GameFlow.new()
	check((await gf2.start({"mode": "local", "logger": null})).ok, "start with tracking disabled")
	check_eq(gf2.players.tracking_enabled(), false, "tracking disabled")
	var disabled_track: GameFlowResult = await gf2.players.track("x")
	check_eq(disabled_track.code, GameFlowResult.PLAYER_TRACKING_DISABLED, "track fails")
	var disabled_untrack: GameFlowResult = await gf2.players.untrack("x")
	check_eq(disabled_untrack.code, GameFlowResult.PLAYER_TRACKING_DISABLED, "untrack fails")
	await gf2.shutdown()

	# Mode detection via env var
	OS.set_environment("GAMEFLOW_SDK_MODE", "local")
	var gf3 := GameFlow.new()
	check((await gf3.start({"logger": null})).ok, "GAMEFLOW_SDK_MODE=local forces local mode")
	check_eq(gf3.mode, "local", "mode resolved from env")
	await gf3.shutdown()
	OS.set_environment("GAMEFLOW_SDK_MODE", "")

	# Calling before start
	var gf4 := GameFlow.new()
	var early: GameFlowResult = await gf4.players.track("x")
	check_eq(early.code, GameFlowResult.NOT_CONNECTED, "players before start -> NOT_CONNECTED")
