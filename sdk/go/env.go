package gameflow

import (
	"strconv"
	"strings"
)

// envSource is a source of environment variables. Production reads the process
// environment; tests inject a fixed map. It returns the value and whether the
// key was present.
type envSource func(key string) (string, bool)

func (e envSource) get(key string) (string, bool) { return e(key) }

// getOr returns the value for key, or "" when it is unset.
func (e envSource) getOr(key string) string {
	v, _ := e(key)
	return v
}

// mapEnv builds an envSource backed by a fixed map (used by tests and connect
// with an explicit environment).
func mapEnv(pairs map[string]string) envSource {
	return func(key string) (string, bool) {
		v, ok := pairs[key]
		return v, ok
	}
}

// Ports exposes the network ports GameFlow assigned to this server, read from
// GAMEFLOW_*_PORT environment variables.
//
// Every accessor returns ok=false rather than failing when a port is absent or
// invalid, so callers can fall back to their own default:
//
//	port, ok := gf.Ports().Default()
//	if !ok {
//		port = 7777
//	}
type Ports struct {
	env envSource
}

// Default returns the game's primary port (GAMEFLOW_DEFAULT_PORT).
func (p Ports) Default() (uint16, bool) {
	return parsePort(p.env.getOr("GAMEFLOW_DEFAULT_PORT"))
}

// Get returns an additional named port, as configured in the game's networking
// settings. Names are normalized ("voice chat" -> VOICE_CHAT).
func (p Ports) Get(name string) (uint16, bool) {
	return parsePort(p.env.getOr("GAMEFLOW_" + normalizePortName(name) + "_PORT"))
}

// TLS returns the TLS-terminated listener ports, present only when the game uses
// TLS.
func (p Ports) TLS() TLSPorts {
	return TLSPorts{env: p.env}
}

// TLSPorts exposes TLS-terminated listener ports (GAMEFLOW_TLS_*_PORT).
type TLSPorts struct {
	env envSource
}

// Default returns the primary TLS port (GAMEFLOW_TLS_DEFAULT_PORT).
func (p TLSPorts) Default() (uint16, bool) {
	return parsePort(p.env.getOr("GAMEFLOW_TLS_DEFAULT_PORT"))
}

// Get returns an additional named TLS port.
func (p TLSPorts) Get(name string) (uint16, bool) {
	return parsePort(p.env.getOr("GAMEFLOW_TLS_" + normalizePortName(name) + "_PORT"))
}

// normalizePortName mirrors the platform's env var naming: names are trimmed,
// spaces become underscores and the result is uppercased
// ("voice chat" -> VOICE_CHAT).
func normalizePortName(name string) string {
	return strings.ToUpper(strings.ReplaceAll(strings.TrimSpace(name), " ", "_"))
}

// parsePort parses a port string, returning ok=false for empty, non-numeric,
// zero or out-of-range values.
func parsePort(value string) (uint16, bool) {
	trimmed := strings.TrimSpace(value)
	if trimmed == "" {
		return 0, false
	}
	n, err := strconv.ParseUint(trimmed, 10, 16)
	if err != nil || n == 0 {
		return 0, false
	}
	return uint16(n), true
}
