package gameflow

import (
	"context"
	"log/slog"
)

// Logger is the SDK's diagnostic sink. It is injectable and fully silenceable so
// the SDK never writes anywhere you did not choose.
//
// Levels follow the cross-language spec: lifecycle transitions at info,
// recoverable problems at warn, degraded health at error, everything else at
// debug.
type Logger interface {
	Debug(msg string)
	Info(msg string)
	Warn(msg string)
	Error(msg string)
}

// slogLogger is the default logger: it forwards to a [log/slog] handler under a
// "gameflow" group (the idiomatic Go equivalent of the "[gameflow]" line prefix
// every other SDK uses), so it integrates with whatever slog setup the host app
// already has.
type slogLogger struct {
	logger *slog.Logger
}

func newSlogLogger(base *slog.Logger) *slogLogger {
	if base == nil {
		base = slog.Default()
	}
	return &slogLogger{logger: base.With(slog.String("component", "gameflow"))}
}

func (l *slogLogger) Debug(msg string) { l.logger.LogAttrs(context.Background(), slog.LevelDebug, msg) }
func (l *slogLogger) Info(msg string)  { l.logger.LogAttrs(context.Background(), slog.LevelInfo, msg) }
func (l *slogLogger) Warn(msg string)  { l.logger.LogAttrs(context.Background(), slog.LevelWarn, msg) }
func (l *slogLogger) Error(msg string) { l.logger.LogAttrs(context.Background(), slog.LevelError, msg) }

// nopLogger drops every message. It backs [WithSilent].
type nopLogger struct{}

func (nopLogger) Debug(string) {}
func (nopLogger) Info(string)  {}
func (nopLogger) Warn(string)  {}
func (nopLogger) Error(string) {}

func defaultLogger() Logger { return newSlogLogger(slog.Default()) }
