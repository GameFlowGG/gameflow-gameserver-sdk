using System.Collections.Generic;
using GameFlow;
using Xunit;

public class ModeTest
{
    static EnvReader R(Dictionary<string, string> m) => new EnvReader(k => m.TryGetValue(k, out var v) ? v : null);

    [Fact]
    public void ExplicitBeatsEnv()
    {
        var o = new GameFlowOptions { Mode = GameFlowMode.Local };
        Assert.Equal(GameFlowMode.Local, ModeDetection.Resolve(o, R(new() { { "AGONES_SDK_HTTP_PORT", "9358" } }), new NullLogger()));
    }

    [Fact]
    public void EnvModeBeatsAuto()
    {
        var o = new GameFlowOptions();
        Assert.Equal(GameFlowMode.Local, ModeDetection.Resolve(o, R(new() { { "GAMEFLOW_SDK_MODE", "local" }, { "AGONES_SDK_HTTP_PORT", "9358" } }), new NullLogger()));
        Assert.Equal(GameFlowMode.Sidecar, ModeDetection.Resolve(o, R(new() { { "GAMEFLOW_SDK_MODE", "SIDECAR" } }), new NullLogger()));
    }

    [Fact]
    public void AutoSidecarWhenPortPresentElseLocal()
    {
        var o = new GameFlowOptions();
        Assert.Equal(GameFlowMode.Sidecar, ModeDetection.Resolve(o, R(new() { { "AGONES_SDK_HTTP_PORT", "9358" } }), new NullLogger()));
        Assert.Equal(GameFlowMode.Local, ModeDetection.Resolve(o, R(new()), new NullLogger()));
    }
}
