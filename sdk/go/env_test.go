package gameflow

import "testing"

func TestNormalizePortName(t *testing.T) {
	cases := map[string]string{
		"voice chat": "VOICE_CHAT",
		"  default ": "DEFAULT",
		"Game":       "GAME",
	}
	for in, want := range cases {
		if got := normalizePortName(in); got != want {
			t.Errorf("normalizePortName(%q) = %q, want %q", in, got, want)
		}
	}
}

func TestParsePort(t *testing.T) {
	valid := map[string]uint16{"7777": 7777, " 8080 ": 8080}
	for in, want := range valid {
		got, ok := parsePort(in)
		if !ok || got != want {
			t.Errorf("parsePort(%q) = (%d, %v), want (%d, true)", in, got, ok, want)
		}
	}
	for _, in := range []string{"0", "-1", "abc", "99999", ""} {
		if got, ok := parsePort(in); ok {
			t.Errorf("parsePort(%q) = (%d, true), want ok=false", in, got)
		}
	}
}

func TestReadsNamedAndTLSPortsAndMetadata(t *testing.T) {
	env := mapEnv(map[string]string{
		"GAMEFLOW_DEFAULT_PORT":     "7777",
		"GAMEFLOW_VOICE_CHAT_PORT":  "7778",
		"GAMEFLOW_TLS_DEFAULT_PORT": "8443",
		"GAMEFLOW_REGION":           "eu-west-1",
		"GAMEFLOW_BUILD_ID":         "build-42",
	})
	ports := Ports{env: env}

	if got, ok := ports.Default(); !ok || got != 7777 {
		t.Errorf("Default() = (%d, %v), want (7777, true)", got, ok)
	}
	if got, ok := ports.Get("voice chat"); !ok || got != 7778 {
		t.Errorf("Get(voice chat) = (%d, %v), want (7778, true)", got, ok)
	}
	if _, ok := ports.Get("missing"); ok {
		t.Error("Get(missing) should be absent")
	}
	if got, ok := ports.TLS().Default(); !ok || got != 8443 {
		t.Errorf("TLS().Default() = (%d, %v), want (8443, true)", got, ok)
	}
	if got, _ := env.get("GAMEFLOW_REGION"); got != "eu-west-1" {
		t.Errorf("region = %q, want eu-west-1", got)
	}
	if got, _ := env.get("GAMEFLOW_BUILD_ID"); got != "build-42" {
		t.Errorf("buildID = %q, want build-42", got)
	}
}
