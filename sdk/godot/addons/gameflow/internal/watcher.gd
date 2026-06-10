extends RefCounted
# Internal: multiplexes a single watch stream to any number of subscribers and
# keeps it alive with reconnect + backoff. The stream is opened lazily on the
# first subscriber and closed when the last one unsubscribes (or on stop()).

const _Raw := preload("raw.gd")

const INITIAL_BACKOFF_MS := 250
const MAX_BACKOFF_MS := 5000


class Cancel:
	var cancelled := false


# Invoked before user listeners on every update (cache sync hook).
var on_update := Callable()

var _transport
var _logger: GameFlowLogger
var _listeners: Array[Callable] = []
var _cancel: Cancel = null
var _latest: Variant = null
var _stopped := false


func _init(transport, logger: GameFlowLogger) -> void:
	_transport = transport
	_logger = logger


func subscribe(listener: Callable) -> Callable:
	_listeners.append(listener)
	if _latest != null:
		listener.call(_latest)
	_ensure_stream()
	var state := {"unsubscribed": false}
	return func() -> void:
		if state.unsubscribed:
			return
		state.unsubscribed = true
		_listeners.erase(listener)
		if _listeners.is_empty():
			_close_stream()


func stop() -> void:
	_stopped = true
	_listeners.clear()
	_close_stream()


func _ensure_stream() -> void:
	if _cancel != null or _stopped:
		return
	_cancel = Cancel.new()
	_run(_cancel)


func _close_stream() -> void:
	if _cancel != null:
		_cancel.cancelled = true
		_cancel = null


func _run(cancel: Cancel) -> void:
	# Backoff lives in a dictionary so the stream handler (a lambda capturing
	# by value) can reset it after any received message.
	var state := {"backoff_ms": INITIAL_BACKOFF_MS}
	var handler := func(gs: Dictionary) -> void:
		state.backoff_ms = INITIAL_BACKOFF_MS
		_dispatch(gs)
	while not cancel.cancelled:
		var res: GameFlowResult = await _transport.watch_game_server(handler, cancel)
		if cancel.cancelled:
			return
		if res.ok:
			_logger.debug("watch stream ended; reconnecting")
		else:
			_logger.warn(
				"watch stream failed (retrying in %dms): %s" % [state.backoff_ms, res.message]
			)
		await _Raw.sleep_ms_cancellable(state.backoff_ms, cancel)
		state.backoff_ms = mini(state.backoff_ms * 2, MAX_BACKOFF_MS)


func _dispatch(gs: Dictionary) -> void:
	_latest = gs
	if on_update.is_valid():
		on_update.call(gs)
	for listener in _listeners.duplicate():
		listener.call(gs)
