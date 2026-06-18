using System.Threading;
using System.Threading.Tasks;
using GameFlow;
using Xunit;

public class WatcherTest
{
    [SkippableFact]
    public async Task FiresOnPushedUpdate()
    {
        Skip.IfNot(Fixture.NodeAvailable);
        using var fx = Fixture.Start("--players-capacity=2");
        var t = new SidecarTransport($"http://127.0.0.1:{fx.Port}", 3000, new NullLogger());
        var w = new Watcher(t, new NullLogger());

        int hits = 0;
        using var sub = w.Subscribe(_ => Interlocked.Increment(ref hits));
        await Task.Delay(200);          // let the stream open + receive the initial frame
        fx.ControlPost("/push-update");
        await Task.Delay(250);
        Assert.True(hits >= 1);
    }

    [SkippableFact]
    public async Task StopsAfterLastUnsubscribe()
    {
        Skip.IfNot(Fixture.NodeAvailable);
        using var fx = Fixture.Start("--players-capacity=2");
        var t = new SidecarTransport($"http://127.0.0.1:{fx.Port}", 3000, new NullLogger());
        var w = new Watcher(t, new NullLogger());

        int hits = 0;
        var sub = w.Subscribe(_ => Interlocked.Increment(ref hits));
        await Task.Delay(150);
        sub.Dispose();
        await Task.Delay(100);
        int before = hits;
        fx.ControlPost("/push-update");
        await Task.Delay(200);
        Assert.Equal(before, hits);     // no delivery after unsubscribe
    }

    [SkippableFact]
    public async Task ResubscribeAfterUnsubscribeStillDelivers()
    {
        Skip.IfNot(Fixture.NodeAvailable);
        using var fx = Fixture.Start("--players-capacity=2");
        var t = new SidecarTransport($"http://127.0.0.1:{fx.Port}", 3000, new NullLogger());
        var w = new Watcher(t, new NullLogger());

        // Rapid unsubscribe→subscribe: the new loop must chain behind the old one, not run concurrently.
        var first = w.Subscribe(_ => { });
        first.Dispose();

        int hits = 0;
        using var second = w.Subscribe(_ => Interlocked.Increment(ref hits));
        await Task.Delay(250);
        fx.ControlPost("/push-update");
        await Task.Delay(250);
        Assert.True(hits >= 1);
    }
}
