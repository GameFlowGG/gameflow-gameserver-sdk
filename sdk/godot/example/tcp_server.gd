extends SceneTree
# Minimal TCP server using the GameFlow SDK (the Godot twin of
# examples/node-tcp). Run from sdk/godot:
#   godot --headless --script res://example/tcp_server.gd
# Then: nc 127.0.0.1 7777

var gf := GameFlow.new()
var server := TCPServer.new()
var peers := {}


func _initialize() -> void:
	_main()


func _main() -> void:
	var res: GameFlowResult = await gf.start()
	if not res.ok:
		printerr("gameflow start failed: %s (%s)" % [res.message, res.code])
		quit(1)
		return
	print("mode=%s region=%s build=%s" % [gf.mode, gf.region, gf.build_id])

	var payload_res: GameFlowResult = await gf.payload()
	if payload_res.ok and payload_res.value != null:
		print("launch payload: %s" % payload_res.value)
	gf.on_payload_change(
		func(next: Variant) -> void:
			print("payload changed: %s" % (next if next != null else "(cleared)"))
	)

	var port: int = gf.ports.default if gf.ports.default != null else 7777
	var err := server.listen(port)
	if err != OK:
		printerr("listen failed: %s" % error_string(err))
		quit(1)
		return
	print("listening on :%d" % port)
	await gf.ready()  # only after accepting connections
	_serve_loop()


func _serve_loop() -> void:
	while true:
		await process_frame
		while server.is_connection_available():
			_handle_client(server.take_connection())
		for session_id in peers.keys():
			var peer: StreamPeerTCP = peers[session_id]
			peer.poll()
			if peer.get_status() != StreamPeerTCP.STATUS_CONNECTED:
				peers.erase(session_id)
				gf.players.untrack(session_id)


func _handle_client(peer: StreamPeerTCP) -> void:
	var session_id := "session-%d" % randi()
	var res: GameFlowResult = await gf.players.track(session_id)
	if not res.ok:
		var reason := "server full\n" if res.code == GameFlowResult.SERVER_FULL else "cannot join\n"
		peer.put_data(reason.to_utf8_buffer())
		peer.disconnect_from_host()
		return
	peers[session_id] = peer
	var welcome := "welcome %s (%d/%d)\n" % [session_id, gf.players.count(), gf.players.capacity()]
	peer.put_data(welcome.to_utf8_buffer())
