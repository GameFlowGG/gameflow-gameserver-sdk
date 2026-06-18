using System.Threading;
using System.Threading.Tasks;
using GameFlow;
using Xunit;

public class SidecarTransportTest
{
    static SidecarTransport T(Fixture fx) => new SidecarTransport($"http://127.0.0.1:{fx.Port}", 3000, new NullLogger());

    [SkippableFact]
    public async Task ReadyAndPlayerMappingsAgainstFixture()
    {
        Skip.IfNot(Fixture.NodeAvailable);
        using var fx = Fixture.Start("--players-capacity=2", "--players-seed=seeded");
        var t = T(fx);

        await t.Ready(CancellationToken.None);

        var l = await t.AddPlayer("p1", 2, CancellationToken.None);
        Assert.Contains("p1", l.SessionIds);

        // capacity 2, now holds seeded + p1 -> full
        var full = await Assert.ThrowsAsync<ServerFullException>(() => t.AddPlayer("p2", 2, CancellationToken.None));
        Assert.Equal(2, full.Capacity);

        await Assert.ThrowsAsync<PlayerAlreadyConnectedException>(() => t.AddPlayer("seeded", 2, CancellationToken.None));

        var (found, after) = await t.RemovePlayer("seeded", CancellationToken.None);
        Assert.True(found);
        Assert.DoesNotContain("seeded", after.SessionIds);

        var (ghost, _) = await t.RemovePlayer("ghost", CancellationToken.None);
        Assert.False(ghost);

        Assert.Equal(1, fx.CountRequests("/ready"));
    }

    [SkippableFact]
    public async Task ReReadsListInsteadOfTrustingMutationEcho()
    {
        Skip.IfNot(Fixture.NodeAvailable);
        using var fx = Fixture.Start("--players-capacity=4");
        var t = T(fx);
        // Force the runtime to echo a zeroed list on mutations; the SDK must re-read the real list.
        fx.ControlPost("/set-mutation-echo?value=default-list");

        var list = await t.AddPlayer("a", 4, CancellationToken.None);
        Assert.Contains("a", list.SessionIds);
        Assert.Equal(4, list.Capacity);
    }

    [SkippableFact]
    public async Task ProbeReturnsServerInfo()
    {
        Skip.IfNot(Fixture.NodeAvailable);
        using var fx = Fixture.Start("--players-capacity=2", "--players-seed=seeded", "--payload=match-1");
        var t = T(fx);
        var info = await t.Probe(CancellationToken.None);
        Assert.True(info.Players.TrackingEnabled);
        Assert.Equal(2, info.Players.Capacity);
        Assert.Single(info.Players.SessionIds);
        Assert.Equal("match-1", Model.PayloadOf(info));
    }
}
