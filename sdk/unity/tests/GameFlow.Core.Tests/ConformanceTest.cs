using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using GameFlow;
using Xunit;

/// <summary>
/// Full-lifecycle conformance against the shared fake-runtime fixture — the C# counterpart of the
/// Go/TS suites. Drives connect-with-retry, ready+health, watch, payload-change, player mappings,
/// idempotent shutdown, and post-shutdown NotConnected, then asserts the runtime-side request counts.
/// </summary>
public class ConformanceTest
{
    [SkippableFact]
    public async Task FullLifecycleAgainstFixture()
    {
        Skip.IfNot(Fixture.NodeAvailable);

        // capacity 2, one seeded player, first two requests fail (connect must retry).
        using var fx = Fixture.Start("--players-capacity=2", "--players-seed=seeded", "--fail-first=2");

        var gf = new GameFlowClient(new GameFlowOptions
        {
            Mode = null,
            HealthIntervalMs = 500,
            Logger = new NullLogger(),
            EnvProvider = k => k == "AGONES_SDK_HTTP_PORT" ? fx.Port.ToString() : null,
        });

        await gf.Start();
        Assert.Equal(GameFlowMode.Sidecar, gf.Mode);

        // The successful probe seeds the players cache.
        Assert.True(gf.Players.TrackingEnabled);
        Assert.Equal(2, gf.Players.Capacity);
        Assert.Equal(1, gf.Players.Count);
        Assert.Equal("seeded", Assert.Single(gf.Players.List));

        await gf.Ready();

        int updates = 0;
        var payloads = new List<string>();
        var payloadsLock = new object();
        using var watchSub = gf.Watch(_ => Interlocked.Increment(ref updates));
        using var payloadSub = gf.OnPayloadChange(p =>
        {
            if (p != null) lock (payloadsLock) payloads.Add(p);
        });

        // Player tracking against the runtime.
        await gf.Players.Connect("p1");
        Assert.Equal(2, gf.Players.Count);

        var full = await Assert.ThrowsAsync<ServerFullException>(() => gf.Players.Connect("p2"));
        Assert.Equal(2, full.Capacity);

        await Assert.ThrowsAsync<PlayerAlreadyConnectedException>(() => gf.Players.Connect("p1"));

        Assert.True(await gf.Players.Disconnect("seeded"));
        Assert.False(await gf.Players.Disconnect("does-not-exist"));
        Assert.Equal(1, gf.Players.Count);

        // Watch fires on a pushed update.
        await Task.Delay(150);
        fx.ControlPost("/push-update");
        await Task.Delay(150);
        Assert.True(Volatile.Read(ref updates) >= 1);

        // Payload change propagates through the watch stream.
        fx.ControlPost("/set-payload?value=match-7");
        await Task.Delay(200);
        Assert.Equal("match-7", await gf.Payload());
        lock (payloadsLock) Assert.Contains("match-7", payloads);

        // Health heartbeat ticks at least twice (interval 500 ms).
        await Task.Delay(1400);

        await gf.Shutdown();
        await gf.Shutdown(); // idempotent

        await Assert.ThrowsAsync<NotConnectedException>(() => gf.Players.Connect("late"));

        // Behavior only observable from the runtime side.
        Assert.Equal(1, fx.CountRequests("/ready"));
        Assert.True(fx.CountRequests("/health") >= 2, $"health pings = {fx.CountRequests("/health")}");
        Assert.Equal(1, fx.CountRequests("/shutdown"));
        Assert.True(fx.CountRequests("/gameserver") >= 3, $"gameserver requests = {fx.CountRequests("/gameserver")}");
    }

    [SkippableFact]
    public async Task LocalModeRunsWithoutRuntime()
    {
        var gf = new GameFlowClient(new GameFlowOptions
        {
            Logger = new NullLogger(),
            EnvProvider = k => k switch
            {
                "GAMEFLOW_MAX_PLAYERS" => "2",
                "GAMEFLOW_PAYLOAD" => "solo",
                _ => null, // no AGONES_SDK_HTTP_PORT -> local mode
            },
        });

        await gf.Start();
        Assert.Equal(GameFlowMode.Local, gf.Mode);
        await gf.Ready();

        Assert.Equal("solo", await gf.Payload());
        await gf.Players.Connect("a");
        Assert.Equal(1, gf.Players.Count);
        await Assert.ThrowsAsync<ServerFullException>(async () =>
        {
            await gf.Players.Connect("b");
            await gf.Players.Connect("c");
        });

        await gf.Shutdown();
        await Assert.ThrowsAsync<NotConnectedException>(() => gf.Players.Connect("late"));
    }
}
