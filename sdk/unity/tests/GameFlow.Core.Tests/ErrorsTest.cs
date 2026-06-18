using System;
using GameFlow;
using Xunit;

public class ErrorsTest
{
    [Fact]
    public void ServerFullCarriesCapacityAndCode()
    {
        var e = new ServerFullException("full", 8);
        Assert.Equal(GameFlowErrorCode.ServerFull, e.Code);
        Assert.Equal(8, e.Capacity);
        Assert.IsAssignableFrom<GameFlowException>(e);
    }

    [Fact]
    public void SidecarUnavailableKeepsInner()
    {
        var inner = new Exception("boom");
        var e = new SidecarUnavailableException("down", inner);
        Assert.Equal(GameFlowErrorCode.SidecarUnavailable, e.Code);
        Assert.Same(inner, e.InnerException);
    }

    [Fact]
    public void EachSubtypeMapsToItsCode()
    {
        Assert.Equal(GameFlowErrorCode.PlayerAlreadyConnected, new PlayerAlreadyConnectedException("x").Code);
        Assert.Equal(GameFlowErrorCode.PlayerTrackingDisabled, new PlayerTrackingDisabledException("x").Code);
        Assert.Equal(GameFlowErrorCode.NotConnected, new NotConnectedException("x").Code);
        Assert.Equal(GameFlowErrorCode.RequestFailed, new RequestFailedException("x").Code);
    }
}
