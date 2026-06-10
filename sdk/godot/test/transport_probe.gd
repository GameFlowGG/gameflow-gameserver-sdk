extends SceneTree
# Debug probe: exercises the sidecar transport's add_player against whatever
# answers on GF_PROBE_PORT and prints the parsed snapshot. Not part of CI.

const Transport := preload("res://addons/gameflow/internal/transport_sidecar.gd")


func _initialize() -> void:
	await process_frame
	var port := OS.get_environment("GF_PROBE_PORT").to_int()
	var transport := Transport.new(port, 3000, GameFlowLogger.silent())
	var res: GameFlowResult = await transport.add_player("t1")
	if not res.ok:
		print("PROBE_RESULT error=%s message=%s" % [res.code, res.message])
	else:
		print("PROBE_RESULT snapshot=%s" % JSON.stringify(res.value))
	quit(0)
