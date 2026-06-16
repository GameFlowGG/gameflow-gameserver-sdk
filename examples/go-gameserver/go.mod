module github.com/GameFlowGG/gameflow-go-gameserver

go 1.23

// Depends on the PUBLISHED module from the Go module proxy (not a local path),
// so building this proves the released SDK works end to end. For local
// development the repo-root go.work resolves it from ../../sdk/go instead.
require github.com/GameFlowGG/gameflow-gameserver-sdk/sdk/go v0.1.0
