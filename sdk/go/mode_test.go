package gameflow

import "testing"

func resolveTestMode(option Mode, pairs map[string]string) Mode {
	return resolveMode(option, mapEnv(pairs), nopLogger{})
}

func TestModeDetectionPrecedence(t *testing.T) {
	// Explicit option wins over everything.
	if got := resolveTestMode(ModeLocal, map[string]string{"AGONES_SDK_HTTP_PORT": "9358"}); got != ModeLocal {
		t.Errorf("explicit Local = %q, want local", got)
	}
	if got := resolveTestMode(ModeSidecar, nil); got != ModeSidecar {
		t.Errorf("explicit Sidecar = %q, want sidecar", got)
	}

	// GAMEFLOW_SDK_MODE wins over auto-detection.
	if got := resolveTestMode(ModeAuto, map[string]string{
		"GAMEFLOW_SDK_MODE":    "local",
		"AGONES_SDK_HTTP_PORT": "9358",
	}); got != ModeLocal {
		t.Errorf("env mode local = %q, want local", got)
	}
	if got := resolveTestMode(ModeAuto, map[string]string{"GAMEFLOW_SDK_MODE": "sidecar"}); got != ModeSidecar {
		t.Errorf("env mode sidecar = %q, want sidecar", got)
	}

	// Any non-empty AGONES port (including whitespace) selects sidecar and fails
	// hard; only empty/unset falls back to local.
	if got := resolveTestMode(ModeAuto, map[string]string{"AGONES_SDK_HTTP_PORT": "9358"}); got != ModeSidecar {
		t.Errorf("agones port set = %q, want sidecar", got)
	}
	if got := resolveTestMode(ModeAuto, map[string]string{"AGONES_SDK_HTTP_PORT": " "}); got != ModeSidecar {
		t.Errorf("agones port whitespace = %q, want sidecar", got)
	}
	if got := resolveTestMode(ModeAuto, map[string]string{"AGONES_SDK_HTTP_PORT": ""}); got != ModeLocal {
		t.Errorf("agones port empty = %q, want local", got)
	}
	if got := resolveTestMode(ModeAuto, nil); got != ModeLocal {
		t.Errorf("nothing set = %q, want local", got)
	}
}

func TestSidecarPortFallsBackForInvalidValues(t *testing.T) {
	cases := map[string]uint16{
		"9358":   9358,
		" 9358 ": 9358,
		"0":      defaultSidecarPort,
		"abc":    defaultSidecarPort,
	}
	for raw, want := range cases {
		if got := sidecarPort(mapEnv(map[string]string{"AGONES_SDK_HTTP_PORT": raw})); got != want {
			t.Errorf("sidecarPort(%q) = %d, want %d", raw, got, want)
		}
	}
	if got := sidecarPort(mapEnv(nil)); got != defaultSidecarPort {
		t.Errorf("sidecarPort(unset) = %d, want %d", got, defaultSidecarPort)
	}
}
