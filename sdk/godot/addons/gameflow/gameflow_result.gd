class_name GameFlowResult
extends RefCounted

## Result of an SDK call.
##
## [member ok] is [code]true[/code] on success. On failure [member code] holds
## one of the stable error codes shared by every GameFlow SDK; messages are
## human-readable and not part of the contract.

## The GameFlow runtime could not be reached (or stopped responding).
const SIDECAR_UNAVAILABLE := "SIDECAR_UNAVAILABLE"
## The session id is already in the connected players list.
const PLAYER_ALREADY_CONNECTED := "PLAYER_ALREADY_CONNECTED"
## The players list is at capacity.
const SERVER_FULL := "SERVER_FULL"
## Player tracking is not enabled for this server (max players is 0).
const PLAYER_TRACKING_DISABLED := "PLAYER_TRACKING_DISABLED"
## A method was called before start() resolved or after shutdown().
const NOT_CONNECTED := "NOT_CONNECTED"
## An unexpected non-2xx response or network failure.
const REQUEST_FAILED := "REQUEST_FAILED"

var ok := true
## Stable error code ("" on success).
var code := ""
var message := ""
## HTTP status for REQUEST_FAILED, 0 otherwise.
var status := 0
## Capacity for SERVER_FULL, -1 when unknown.
var capacity := -1
## Call-specific value: the payload string for GameFlow.payload(), a
## GameFlowServerInfo for GameFlow.info(), the found bool for
## GameFlowPlayers.untrack(). Null otherwise.
var value: Variant = null


static func success(p_value: Variant = null) -> GameFlowResult:
	var result := GameFlowResult.new()
	result.value = p_value
	return result


static func failure(p_code: String, p_message: String) -> GameFlowResult:
	var result := GameFlowResult.new()
	result.ok = false
	result.code = p_code
	result.message = p_message
	return result
