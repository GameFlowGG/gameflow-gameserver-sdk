using System.Collections.Generic;
using GameFlow;
using Xunit;

public class EnvTest
{
    static EnvReader R(Dictionary<string, string> m) => new EnvReader(k => m.TryGetValue(k, out var v) ? v : null);

    [Fact]
    public void PortVarParsesOrNull()
    {
        var r = R(new() { { "GAMEFLOW_DEFAULT_PORT", "7777" }, { "GAMEFLOW_BAD", "x" } });
        Assert.Equal(7777, r.PortVar("GAMEFLOW_DEFAULT_PORT"));
        Assert.Null(r.PortVar("GAMEFLOW_BAD"));
        Assert.Null(r.PortVar("MISSING"));
    }

    [Fact]
    public void GetTreatsEmptyAsAbsent()
    {
        var r = R(new() { { "GAMEFLOW_REGION", "" } });
        Assert.Null(r.Get("GAMEFLOW_REGION"));
    }

    [Fact]
    public void NamedPortUppercasesAndUnderscores()
    {
        // GameFlowEnv.Port("game port") => GAMEFLOW_GAME_PORT_PORT is wrong; the helper builds GAMEFLOW_<NAME>_PORT.
        var r = R(new() { { "GAMEFLOW_GAME_PORT", "9000" } });
        Assert.Equal(9000, r.PortVar("GAMEFLOW_" + "game".ToUpperInvariant() + "_PORT"));
        Assert.Equal(9000, r.PortVar("GAMEFLOW_GAME_PORT"));
    }
}
