class_name GameFlowServerInfo
extends RefCounted

## Current server details, as returned by GameFlow.info() and watch listeners.

var name := ""
var state := ""
var address := ""
## Array of dictionaries: [code]{"name": String, "port": int}[/code].
var ports: Array[Dictionary] = []
var labels := {}
var annotations := {}


## Internal: builds an info object from a normalized game server dictionary.
static func from_raw(gs: Dictionary) -> GameFlowServerInfo:
	var info := GameFlowServerInfo.new()
	var meta: Variant = gs.get("object_meta")
	if meta is Dictionary:
		info.name = str(meta.get("name", ""))
		var labels_raw: Variant = meta.get("labels")
		if labels_raw is Dictionary:
			info.labels = labels_raw
		var annotations_raw: Variant = meta.get("annotations")
		if annotations_raw is Dictionary:
			info.annotations = annotations_raw
	var status: Variant = gs.get("status")
	if status is Dictionary:
		info.state = str(status.get("state", ""))
		info.address = str(status.get("address", ""))
		var ports_raw: Variant = status.get("ports")
		if ports_raw is Array:
			for p in ports_raw:
				if p is Dictionary:
					info.ports.append({"name": str(p.get("name", "")), "port": int(p.get("port", 0))})
	return info
