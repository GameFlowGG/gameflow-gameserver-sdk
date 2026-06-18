using System;
using System.Threading;
using System.Threading.Tasks;
using GameFlow;
using Xunit;

public class HealthLoopTest
{
    [Fact]
    public async Task DegradesOnceAfterSixFailuresThenRecovers()
    {
        int pings = 0, degraded = 0;
        Func<CancellationToken, Task> ping = _ =>
        {
            pings++;
            return pings <= 7 ? Task.FromException(new Exception("x")) : Task.CompletedTask;
        };
        var loop = new HealthLoop(ping, (_, __) => Task.CompletedTask, 500, () => degraded++, new NullLogger());
        loop.Start();
        // Spin until the loop has pinged past the recovery point.
        var deadline = DateTime.UtcNow.AddSeconds(5);
        while (pings < 9 && DateTime.UtcNow < deadline) await Task.Delay(5);
        await loop.StopAsync();

        Assert.True(pings >= 8, $"pings={pings}");
        Assert.Equal(1, degraded); // fires exactly once, at the 6th consecutive failure
    }

    [Fact]
    public async Task StopIsPromptEvenWithLongInterval()
    {
        int pings = 0;
        var loop = new HealthLoop(_ => { pings++; return Task.CompletedTask; },
            (ms, ct) => Task.Delay(ms, ct), 60_000, null, new NullLogger());
        loop.Start();
        await Task.Delay(50);
        var sw = System.Diagnostics.Stopwatch.StartNew();
        await loop.StopAsync();
        sw.Stop();
        Assert.True(sw.ElapsedMilliseconds < 5000, $"stop took {sw.ElapsedMilliseconds} ms");
        Assert.True(pings >= 1);
    }
}
