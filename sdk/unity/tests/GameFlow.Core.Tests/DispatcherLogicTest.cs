using GameFlow;
using Xunit;

public class DispatcherLogicTest
{
    [Fact]
    public void PumpDrainsPostedActionsInOrder()
    {
        var q = new MainThreadQueue();
        int sum = 0;
        q.Post(() => sum += 1);
        q.Post(() => sum += 10);
        q.Pump();
        Assert.Equal(11, sum);
        q.Pump(); // nothing left
        Assert.Equal(11, sum);
    }

    [Fact]
    public void ThrowingActionDoesNotStallPump()
    {
        var q = new MainThreadQueue();
        int ran = 0;
        q.Post(() => throw new System.Exception("boom"));
        q.Post(() => ran++);
        q.Pump();
        Assert.Equal(1, ran);
    }
}
