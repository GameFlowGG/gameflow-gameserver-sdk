using System;
using GameFlow;
using Xunit;

public class BackoffTest
{
    [Fact]
    public void DoublesWithJitterAndCaps()
    {
        var b = new Backoff(new Random(1));
        int d0 = b.NextDelayMs(), d1 = b.NextDelayMs();
        Assert.InRange(d0, 200, 300);   // 250 ±20%
        Assert.InRange(d1, 400, 600);   // 500 ±20%
        for (int k = 0; k < 10; k++) b.NextDelayMs();
        Assert.InRange(b.NextDelayMs(), 3200, 4800); // capped at 4000 ±20%
    }

    [Fact]
    public void ResetGoesBackToBase()
    {
        var b = new Backoff(new Random(1));
        b.NextDelayMs();
        b.NextDelayMs();
        b.Reset();
        Assert.InRange(b.NextDelayMs(), 200, 300);
    }
}
