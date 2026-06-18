using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using GameFlow;
using Xunit;

public class PlayersTest
{
    [Fact]
    public async Task ConnectAndDisconnectUpdateCacheSynchronously()
    {
        var t = new LocalTransport(new EnvReader(k => k == "GAMEFLOW_MAX_PLAYERS" ? "3" : null), new NullLogger());
        var p = new Players(t);
        var info = await t.Probe(CancellationToken.None);
        p.SetCache(info.Players);

        Assert.True(p.TrackingEnabled);
        Assert.Equal(3, p.Capacity);

        await p.Connect("a");
        Assert.Equal(1, p.Count);
        Assert.Contains("a", p.List);

        Assert.True(await p.Disconnect("a"));
        Assert.Equal(0, p.Count);
        Assert.False(await p.Disconnect("a"));
    }

    [Fact]
    public async Task GuardThrowsWhenNotConnected()
    {
        var t = new LocalTransport(new EnvReader(_ => null), new NullLogger());
        var p = new Players(t, () => throw new NotConnectedException("not connected"));
        await Assert.ThrowsAsync<NotConnectedException>(() => p.Connect("a"));
    }
}
