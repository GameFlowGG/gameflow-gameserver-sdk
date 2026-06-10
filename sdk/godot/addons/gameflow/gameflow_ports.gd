class_name GameFlowPorts
extends RefCounted

## Ports assigned to this server, from the GAMEFLOW_*_PORT environment
## variables the platform sets. Values are [int], or [code]null[/code] when
## the port is not provided (helpers never fail).

const _Env := preload("internal/env.gd")

var _prefix := "GAMEFLOW_"

## TLS-terminated listener ports, present only when the game uses TLS.
## [code]null[/code] on the tls group itself.
var tls: GameFlowPorts

## The game's primary port (GAMEFLOW_DEFAULT_PORT), or [code]null[/code].
var default: Variant:
	get:
		return _Env.env_port(_prefix + "DEFAULT_PORT")


## Internal: builds the root port group (with its tls subgroup).
static func create() -> GameFlowPorts:
	var ports := GameFlowPorts.new()
	ports.tls = GameFlowPorts.new()
	ports.tls._prefix = "GAMEFLOW_TLS_"
	return ports


## An additional named port, as configured in the game's networking settings.
## Names are normalized like the platform does ("voice chat" ->
## GAMEFLOW_VOICE_CHAT_PORT). Returns [int] or [code]null[/code].
func by_name(port_name: String) -> Variant:
	return _Env.env_port(_prefix + _Env.normalize_port_name(port_name) + "_PORT")
