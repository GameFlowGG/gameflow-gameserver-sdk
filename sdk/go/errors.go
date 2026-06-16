package gameflow

import (
	"errors"
	"fmt"
)

// ErrorCode is a stable, machine-readable error code shared by every GameFlow
// SDK (TypeScript, Godot, Rust, Go, ...). These string values are part of the
// cross-language contract: handle them programmatically and keep them frozen.
// Error messages, by contrast, are idiomatic per language and are not contract.
type ErrorCode string

const (
	// CodeSidecarUnavailable means the GameFlow runtime could not be reached
	// (or stopped responding).
	CodeSidecarUnavailable ErrorCode = "SIDECAR_UNAVAILABLE"
	// CodePlayerAlreadyConnected means the session id is already in the
	// connected players list.
	CodePlayerAlreadyConnected ErrorCode = "PLAYER_ALREADY_CONNECTED"
	// CodeServerFull means the players list is at capacity.
	CodeServerFull ErrorCode = "SERVER_FULL"
	// CodePlayerTrackingDisabled means player tracking is disabled (the game was
	// created with max players = 0).
	CodePlayerTrackingDisabled ErrorCode = "PLAYER_TRACKING_DISABLED"
	// CodeNotConnected means a method was called after the SDK was shut down.
	CodeNotConnected ErrorCode = "NOT_CONNECTED"
	// CodeRequestFailed means an unexpected non-2xx response or transport failure.
	CodeRequestFailed ErrorCode = "REQUEST_FAILED"
)

// Sentinel errors for use with errors.Is. They carry only a code, so
// errors.Is(err, gameflow.ErrServerFull) matches any *Error with that code
// regardless of its message or other fields.
var (
	ErrSidecarUnavailable     = &Error{Code: CodeSidecarUnavailable}
	ErrPlayerAlreadyConnected = &Error{Code: CodePlayerAlreadyConnected}
	ErrServerFull             = &Error{Code: CodeServerFull}
	ErrPlayerTrackingDisabled = &Error{Code: CodePlayerTrackingDisabled}
	ErrNotConnected           = &Error{Code: CodeNotConnected}
	ErrRequestFailed          = &Error{Code: CodeRequestFailed}
)

// Error is the single error type returned by every fallible SDK call. Use
// [CodeOf] or errors.Is with one of the sentinel errors to branch on the stable
// [ErrorCode]; the remaining fields carry context for specific codes.
type Error struct {
	// Code is the stable, cross-language error code.
	Code ErrorCode
	// Message is a human-readable, language-idiomatic description. Not contract.
	Message string
	// SessionID is set for CodePlayerAlreadyConnected.
	SessionID string
	// Capacity is the configured capacity for CodeServerFull, when known.
	Capacity int64
	// HasCapacity reports whether Capacity is meaningful.
	HasCapacity bool
	// Status is the HTTP status for CodeRequestFailed, when the failure came from
	// a response (0 otherwise).
	Status int
	// cause is the underlying error, exposed through Unwrap.
	cause error
}

func (e *Error) Error() string {
	if e.Message != "" {
		return e.Message
	}
	return string(e.Code)
}

// Unwrap returns the underlying cause, if any, so errors.Is/As can walk the
// chain.
func (e *Error) Unwrap() error { return e.cause }

// Is reports whether target is an *Error with the same code, enabling
// errors.Is(err, gameflow.ErrServerFull) and friends.
func (e *Error) Is(target error) bool {
	var t *Error
	if !errors.As(target, &t) {
		return false
	}
	return t.Code == e.Code
}

// CodeOf returns the [ErrorCode] of err if it is (or wraps) a [*Error], or the
// empty string otherwise.
func CodeOf(err error) ErrorCode {
	var e *Error
	if errors.As(err, &e) {
		return e.Code
	}
	return ""
}

func sidecarUnavailable(message string, cause error) *Error {
	return &Error{Code: CodeSidecarUnavailable, Message: message, cause: cause}
}

func notConnected() *Error {
	return &Error{Code: CodeNotConnected, Message: "the SDK has been shut down"}
}

func playerAlreadyConnected(sessionID string) *Error {
	return &Error{
		Code:      CodePlayerAlreadyConnected,
		Message:   fmt.Sprintf("player %q is already connected", sessionID),
		SessionID: sessionID,
	}
}

func playerTrackingDisabled() *Error {
	return &Error{
		Code: CodePlayerTrackingDisabled,
		Message: "player tracking is disabled for this server. Set \"Max Players per " +
			"Server\" to a value greater than 0 in your game settings on GameFlow",
	}
}

// serverFull builds a SERVER_FULL error. Pass hasCapacity=false when the
// capacity is not yet known (it is enriched from the cache higher up).
func serverFull(capacity int64, hasCapacity bool) *Error {
	msg := "server is full"
	if hasCapacity {
		msg = fmt.Sprintf("server is full (capacity %d)", capacity)
	}
	return &Error{Code: CodeServerFull, Message: msg, Capacity: capacity, HasCapacity: hasCapacity}
}

func requestFailed(message string, status int, cause error) *Error {
	return &Error{Code: CodeRequestFailed, Message: message, Status: status, cause: cause}
}
