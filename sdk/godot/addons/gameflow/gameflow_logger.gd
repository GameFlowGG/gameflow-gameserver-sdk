class_name GameFlowLogger
extends RefCounted

## Logger used by the SDK. Pass a subclass with overridden methods to route
## logs elsewhere, or [code]GameFlowLogger.silent()[/code] (or
## [code]{"logger": null}[/code] in the start options) to silence the SDK.

const PREFIX := "[gameflow]"

var _enabled := true


## Everything else. Silent by default, like the other GameFlow SDKs.
func debug(_message: String) -> void:
	pass


## Lifecycle transitions.
func info(message: String) -> void:
	if _enabled:
		print("%s %s" % [PREFIX, message])


## Recoverable problems.
func warn(message: String) -> void:
	if _enabled:
		print("%s WARNING: %s" % [PREFIX, message])


## Degraded health.
func error(message: String) -> void:
	if _enabled:
		printerr("%s ERROR: %s" % [PREFIX, message])


static func silent() -> GameFlowLogger:
	var logger := GameFlowLogger.new()
	logger._enabled = false
	return logger
