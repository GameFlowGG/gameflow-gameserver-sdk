// Package gameflow is the official GameFlow SDK for dedicated game servers.
//
// Integrate a dedicated server with GameFlow (https://gameflow.gg): server
// lifecycle, automatic health reporting and player tracking. The same binary
// runs on GameFlow and on your machine — off-platform the SDK enters local mode
// automatically, simulating the runtime with zero configuration.
//
//	gf, err := gameflow.Connect(ctx)
//	if err != nil {
//		log.Fatal(err)
//	}
//	if err := gf.Ready(ctx); err != nil { // health reporting starts automatically
//		log.Fatal(err)
//	}
//
//	gf.Players().Connect(ctx, sessionID)    // when a player joins
//	gf.Players().Disconnect(ctx, sessionID) // when a player leaves
//
//	gf.Shutdown(ctx) // when the match ends
//
// The SDK has no third-party dependencies; it is built entirely on the Go
// standard library. It is engine-agnostic and safe for concurrent use: a
// *GameFlow handle can be shared across goroutines.
package gameflow
