extends RefCounted
# Internal: automatic health heartbeat. The next ping is sent only after the
# previous one settles, so pings never overlap or pile up. Failures are
# tolerated (the platform allows several missed pings): the loop logs and
# keeps the normal cadence; after DEGRADED_THRESHOLD consecutive failures it
# reports degraded health once and recovers silently on the next success.
# The loop never fails outward.

const _Raw := preload("raw.gd")

const DEGRADED_THRESHOLD := 6

var _transport
var _interval_ms: int
var _logger: GameFlowLogger
var _on_degraded: Callable
var _running := false
var _consecutive_failures := 0
var _degraded := false


func _init(transport, interval_ms: int, logger: GameFlowLogger, on_degraded: Callable) -> void:
	_transport = transport
	_interval_ms = interval_ms
	_logger = logger
	_on_degraded = on_degraded


func start() -> void:
	if _running:
		return
	_running = true
	_run()


func stop() -> void:
	_running = false


func _run() -> void:
	while _running:
		var res: GameFlowResult = await _transport.health()
		if not _running:
			break
		if res.ok:
			if _degraded:
				_logger.info("health pings recovered")
			_consecutive_failures = 0
			_degraded = false
		else:
			_consecutive_failures += 1
			_logger.warn(
				"health ping failed (%d consecutive): %s" % [_consecutive_failures, res.message]
			)
			if _consecutive_failures >= DEGRADED_THRESHOLD and not _degraded:
				_degraded = true
				_logger.error(
					"health pings have been failing for a sustained period; "
					+ "the server may be marked unhealthy"
				)
				if _on_degraded.is_valid():
					_on_degraded.call()
		await _Raw.sleep_ms(_interval_ms)
