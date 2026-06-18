using GameFlow;
using Xunit;

public class ModelTest
{
    [Fact]
    public void ParsesListWithStringCapacity()
    {
        var pl = Model.ParseList(Json.Parse("{\"capacity\":\"8\",\"values\":[\"a\",\"b\"]}"));
        Assert.True(pl.TrackingEnabled);
        Assert.Equal(8, pl.Capacity);
        Assert.Equal(2, pl.SessionIds.Count);
        Assert.Equal("a", pl.SessionIds[0]);
    }

    [Fact]
    public void ParsesGameServerWireShape()
    {
        var info = Model.ParseGameServer(Json.Parse(
            "{\"object_meta\":{\"name\":\"gs-1\",\"annotations\":{\"GAMEFLOW_PAYLOAD\":\"p\",\"GAMEFLOW_REGION\":\"eu\"},\"labels\":{\"k\":\"v\"}}," +
            "\"status\":{\"state\":\"Ready\",\"address\":\"10.0.0.1\",\"ports\":[{\"name\":\"default\",\"port\":7777}]," +
            "\"lists\":{\"players\":{\"capacity\":\"2\",\"values\":[\"seeded\"]}}}}"));

        Assert.Equal("gs-1", info.Name);
        Assert.Equal("Ready", info.State);
        Assert.Equal("10.0.0.1", info.Address);
        Assert.Equal("eu", info.Region);
        Assert.Equal("v", info.Labels["k"]);
        Assert.Single(info.Ports);
        Assert.Equal("default", info.Ports[0].Name);
        Assert.Equal(7777, info.Ports[0].Port);
        Assert.Equal("p", Model.PayloadOf(info));
        Assert.True(info.Players.TrackingEnabled);
        Assert.Equal(2, info.Players.Capacity);
        Assert.Single(info.Players.SessionIds);
    }

    [Fact]
    public void GameServerWithoutListsHasTrackingDisabled()
    {
        var info = Model.ParseGameServer(Json.Parse(
            "{\"object_meta\":{\"name\":\"gs\",\"annotations\":{}},\"status\":{\"state\":\"Ready\"}}"));
        Assert.False(info.Players.TrackingEnabled);
        Assert.Null(Model.PayloadOf(info));
    }
}
