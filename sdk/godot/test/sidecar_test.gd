extends "res://test/test_base.gd"
# Conformance tests against the fake runtime fixture. Run via test/run.mjs,
# which starts tools/conformance/serve.mjs with:
#   --players-capacity=2 --players-seed=seeded --fail-first=2
# and points AGONES_SDK_HTTP_PORT here. The runner additionally asserts the
# requests the fixture recorded (/ready once, /health heartbeats, /shutdown).


func run_tests() -> void:
	if OS.get_environment("AGONES_SDK_HTTP_PORT") == "":
		check(false, "AGONES_SDK_HTTP_PORT is not set; run this via test/run.mjs")
		return

	var gf := GameFlow.new()
	var res: GameFlowResult = await gf.start({"logger": null, "health_interval_ms": 500})
	check(res.ok, "start() connects, retrying through the 2 injected failures")
	check_eq(gf.mode, "sidecar", "mode auto-detected as sidecar")

	# Probe response seeded the players cache
	check_eq(gf.players.tracking_enabled(), true, "players list exists")
	check_eq(gf.players.count(), 1, "seeded player visible after connect")
	check_eq(gf.players.capacity(), 2, "capacity coerced from int64 string")

	# Player mutations against the runtime
	check((await gf.players.track("p1")).ok, "track p1")
	check_eq(gf.players.count(), 2, "cache updated from mutation response")
	var dup: GameFlowResult = await gf.players.track("p1")
	check_eq(dup.code, GameFlowResult.PLAYER_ALREADY_CONNECTED, "HTTP 409 maps to code")
	var full: GameFlowResult = await gf.players.track("p2")
	check_eq(full.code, GameFlowResult.SERVER_FULL, "HTTP 400 OUT_OF_RANGE maps to code")
	check_eq(full.capacity, 2, "SERVER_FULL enriched with cached capacity")
	var ghost: GameFlowResult = await gf.players.untrack("ghost")
	check(ghost.ok and ghost.value == false, "HTTP 404 on remove -> found=false")

	# The cache must never trust the mutation echo: real runtimes answer with
	# the updated list, a zeroed default List, or a body the client can't
	# read. Either way count/capacity must stay correct.
	await _set_mutation_echo("default-list")
	var healed: GameFlowResult = await gf.players.untrack("seeded")
	check(healed.ok and healed.value == true, "untrack succeeds on default-list echo")
	check_eq(gf.players.count(), 1, "count correct after default-list untrack echo")
	check((await gf.players.track("p3")).ok, "track succeeds on default-list echo")
	check_eq(gf.players.count(), 2, "count correct after default-list track echo")
	check_eq(gf.players.capacity(), 2, "capacity correct after default-list track echo")
	await _set_mutation_echo("empty")
	check((await gf.players.untrack("p3")).ok, "untrack succeeds on empty response body")
	check_eq(gf.players.count(), 1, "count correct after empty untrack body")
	check((await gf.players.track("p3")).ok, "track succeeds on empty response body")
	check_eq(gf.players.count(), 2, "count correct after empty track body")
	await _set_mutation_echo("list")

	# Watch stream (NDJSON over the proto-named wire shape)
	var states := []
	var unsubscribe := gf.watch(func(info: GameFlowServerInfo) -> void: states.append(info.state))
	check(
		await await_until(func() -> bool: return states.size() >= 1),
		"watch delivers the initial state"
	)
	check((await gf.ready()).ok, "ready() ok")
	check(
		await await_until(func() -> bool: return states.has("Ready")),
		"watch delivers the Ready transition"
	)
	var info_res: GameFlowResult = await gf.info()
	check(info_res.ok, "info() ok")
	check_eq(info_res.value.name, "gs-test", "info reads proto-named object_meta")
	check_eq(info_res.value.address, "10.0.0.1", "info address")

	# Let the 500ms heartbeat tick a few times; the runner asserts the counts.
	await wait_ms(1300)

	var untracked: GameFlowResult = await gf.players.untrack("p1")
	check(untracked.ok and untracked.value == true, "untrack p1 -> found")
	unsubscribe.call()

	check((await gf.shutdown()).ok, "shutdown ok")
	var late: GameFlowResult = await gf.players.track("late")
	check_eq(late.code, GameFlowResult.NOT_CONNECTED, "track after shutdown -> NOT_CONNECTED")


# Pokes the fixture's control server (port in GF_TEST_CONTROL_PORT, set by
# run.mjs) reusing the sidecar transport as a plain HTTP client.
func _set_mutation_echo(mode: String) -> void:
	const Transport := preload("res://addons/gameflow/internal/transport_sidecar.gd")
	var port := OS.get_environment("GF_TEST_CONTROL_PORT").to_int()
	var control := Transport.new(port, 3000, GameFlowLogger.silent())
	var res: GameFlowResult = await control._request(
		HTTPClient.METHOD_POST, "/set-mutation-echo?value=%s" % mode, ""
	)
	check(res.ok, "control set mutation-echo=%s" % mode)
