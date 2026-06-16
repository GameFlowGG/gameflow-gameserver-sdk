package gameflow

import (
	"log/slog"
	"os"
	"time"
)

// Mode is the transport the SDK uses (or, for [ModeAuto], the selection
// strategy). [GameFlow.Mode] reports the resolved mode, always [ModeSidecar] or
// [ModeLocal].
type Mode string

const (
	// ModeAuto uses the runtime on GameFlow when detected, and local mode
	// otherwise. It is the default.
	ModeAuto Mode = "auto"
	// ModeSidecar forces talking to the GameFlow runtime.
	ModeSidecar Mode = "sidecar"
	// ModeLocal forces the simulated local runtime.
	ModeLocal Mode = "local"
)

// options holds the resolved connection configuration. Callers shape it through
// [Option] values passed to [Connect].
type options struct {
	mode             Mode
	healthInterval   time.Duration
	connectTimeout   time.Duration
	requestTimeout   time.Duration
	logger           Logger
	onHealthDegraded func()
	env              envSource
}

func defaultOptions() options {
	return options{
		mode:           ModeAuto,
		healthInterval: 5 * time.Second,
		connectTimeout: 30 * time.Second,
		requestTimeout: 3 * time.Second,
		logger:         defaultLogger(),
		env:            osEnv,
	}
}

// osEnv reads the process environment.
func osEnv(key string) (string, bool) { return os.LookupEnv(key) }

// Option configures [Connect]. Pass any number of options; later options win.
type Option func(*options)

// WithMode forces a transport mode. The GAMEFLOW_SDK_MODE env var overrides
// [ModeAuto] (the default) but not an explicit [ModeSidecar]/[ModeLocal].
func WithMode(m Mode) Option {
	return func(o *options) { o.mode = m }
}

// WithHealthInterval sets the automatic health ping interval. Default 5s,
// clamped to no less than 500ms at connect time.
func WithHealthInterval(d time.Duration) Option {
	return func(o *options) { o.healthInterval = d }
}

// WithConnectTimeout sets the total budget for [Connect], including retries.
// Default 30s.
func WithConnectTimeout(d time.Duration) Option {
	return func(o *options) { o.connectTimeout = d }
}

// WithRequestTimeout sets the per-request timeout. Default 3s. The watch stream
// is exempt; it runs until cancelled.
func WithRequestTimeout(d time.Duration) Option {
	return func(o *options) { o.requestTimeout = d }
}

// WithLogger installs a custom [Logger].
func WithLogger(l Logger) Option {
	return func(o *options) {
		if l != nil {
			o.logger = l
		}
	}
}

// WithSlogLogger routes SDK diagnostics through a specific [log/slog] logger
// (under a "gameflow" component attribute). Without it the SDK uses
// slog.Default().
func WithSlogLogger(l *slog.Logger) Option {
	return func(o *options) { o.logger = newSlogLogger(l) }
}

// WithSilent silences the SDK entirely.
func WithSilent() Option {
	return func(o *options) { o.logger = nopLogger{} }
}

// WithOnHealthDegraded registers a callback invoked once when health pings have
// been failing for a sustained period (and again only after a recovery and a
// fresh sustained failure).
func WithOnHealthDegraded(cb func()) Option {
	return func(o *options) { o.onHealthDegraded = cb }
}

// withEnv injects an environment source. Unexported: used by tests to drive mode
// detection and local mode without mutating the process environment.
func withEnv(env envSource) Option {
	return func(o *options) { o.env = env }
}
