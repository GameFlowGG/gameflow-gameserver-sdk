extends SceneTree
# Minimal test harness for headless runs:
#   godot --headless --path sdk/godot --script res://test/<file>.gd
# Subclasses override run_tests(). Exit code 0 when every check passed.

var _failures := 0
var _checks := 0


func _initialize() -> void:
	_boot()


func _boot() -> void:
	await run_tests()
	if _failures > 0:
		printerr("FAILED: %d/%d checks failed" % [_failures, _checks])
	else:
		print("PASSED: %d checks" % _checks)
	quit(1 if _failures > 0 else 0)


func run_tests() -> void:
	pass


func check(condition: bool, label: String) -> void:
	_checks += 1
	if condition:
		print("  ok - %s" % label)
	else:
		_failures += 1
		printerr("  FAIL - %s" % label)


func check_eq(actual: Variant, expected: Variant, label: String) -> void:
	check(actual == expected, "%s (got %s, want %s)" % [label, actual, expected])


func await_until(predicate: Callable, timeout_ms := 3000) -> bool:
	var deadline := Time.get_ticks_msec() + timeout_ms
	while Time.get_ticks_msec() < deadline:
		if predicate.call():
			return true
		await process_frame
	return predicate.call()


func wait_ms(ms: int) -> void:
	await create_timer(ms / 1000.0).timeout
