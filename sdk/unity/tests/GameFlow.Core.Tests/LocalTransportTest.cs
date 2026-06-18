using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using GameFlow;
using Xunit;

public class LocalTransportTest
{
    static LocalTransport L(Dictionary<string, string> m) =>
        new LocalTransport(new EnvReader(k => m.TryGetValue(k, out var v) ? v : null), new NullLogger());

    [Fact]
    public async Task AddRemoveTracksInMemory()
    {
        var t = L(new() { { "GAMEFLOW_MAX_PLAYERS", "2" } });
        var l1 = await t.AddPlayer("a", 2, CancellationToken.None);
        Assert.Single(l1.SessionIds);
        var (found, l2) = await t.RemovePlayer("a", CancellationToken.None);
        Assert.True(found);
        Assert.Empty(l2.SessionIds);
        var (nf, _) = await t.RemovePlayer("ghost", CancellationToken.None);
        Assert.False(nf);
    }

    [Fact]
    public async Task FullThrowsServerFull()
    {
        var t = L(new() { { "GAMEFLOW_MAX_PLAYERS", "1" } });
        await t.AddPlayer("a", 1, CancellationToken.None);
        var ex = await Assert.ThrowsAsync<ServerFullException>(() => t.AddPlayer("b", 1, CancellationToken.None));
        Assert.Equal(1, ex.Capacity);
    }

    [Fact]
    public async Task AlreadyConnectedThrows()
    {
        var t = L(new() { { "GAMEFLOW_MAX_PLAYERS", "5" } });
        await t.AddPlayer("a", 5, CancellationToken.None);
        await Assert.ThrowsAsync<PlayerAlreadyConnectedException>(() => t.AddPlayer("a", 5, CancellationToken.None));
    }

    [Fact]
    public async Task TrackingDisabledWhenZero()
    {
        var t = L(new() { { "GAMEFLOW_MAX_PLAYERS", "0" } });
        await Assert.ThrowsAsync<PlayerTrackingDisabledException>(() => t.AddPlayer("a", 0, CancellationToken.None));
    }

    [Fact]
    public async Task UnlimitedWhenUnset()
    {
        var t = L(new());
        var l = await t.AddPlayer("a", 0, CancellationToken.None);
        Assert.True(l.TrackingEnabled);
        Assert.Equal(long.MaxValue, l.Capacity);
    }

    [Fact]
    public async Task PayloadExposedThroughSnapshot()
    {
        var t = L(new() { { "GAMEFLOW_PAYLOAD", "match-7" } });
        var info = await t.Probe(CancellationToken.None);
        Assert.Equal("match-7", Model.PayloadOf(info));
    }

    [Fact]
    public async Task WatchFiresOnMutation()
    {
        var t = L(new() { { "GAMEFLOW_MAX_PLAYERS", "4" } });
        using var cts = new CancellationTokenSource();
        int hits = 0;
        var pump = Task.Run(async () =>
        {
            await foreach (var _ in t.Watch(cts.Token)) Interlocked.Increment(ref hits);
        });
        await Task.Delay(50);
        await t.AddPlayer("a", 4, CancellationToken.None);
        await Task.Delay(100);
        cts.Cancel();
        await Assert.ThrowsAnyAsync<System.OperationCanceledException>(() => pump);
        Assert.True(hits >= 1);
    }
}
