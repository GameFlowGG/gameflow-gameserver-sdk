using System.Collections.Generic;
using GameFlow;
using Xunit;

public class JsonTest
{
    [Fact]
    public void ParsesNestedObjectsAndArrays()
    {
        var root = (Dictionary<string, object>)Json.Parse("{\"a\":{\"b\":[1,\"two\",true,null]}}");
        var a = (Dictionary<string, object>)root["a"];
        var b = (List<object>)a["b"];
        Assert.Equal(4, b.Count);
        Assert.Equal("two", b[1]);
        Assert.Equal(true, b[2]);
        Assert.Null(b[3]);
    }

    [Fact]
    public void AsLongCoercesNumericStrings()
    {
        Assert.Equal(42L, Json.AsLong("42"));
        Assert.Equal(42L, Json.AsLong(42.0));
        Assert.Equal(0L, Json.AsLong(null));
    }

    [Fact]
    public void ObjSerializesStringFieldsAndEscapes()
    {
        Assert.Equal("{}", Json.Obj());
        Assert.Equal("{\"value\":\"a\\\"b\"}", Json.Obj(("value", "a\"b")));
    }

    [Fact]
    public void ParsesUnicodeAndEscapes()
    {
        var root = (Dictionary<string, object>)Json.Parse("{\"k\":\"line\\nbreak\\u0041\"}");
        Assert.Equal("line\nbreakA", root["k"]);
    }

    [Fact]
    public void ParsesEmptyContainersAndWhitespace()
    {
        var root = (Dictionary<string, object>)Json.Parse("  { \"a\" : [] , \"b\" : {} } ");
        Assert.Empty((List<object>)root["a"]);
        Assert.Empty((Dictionary<string, object>)root["b"]);
    }
}
