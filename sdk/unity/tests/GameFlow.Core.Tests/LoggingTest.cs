using System;
using System.IO;
using GameFlow;
using Xunit;

public class LoggingTest
{
    [Fact]
    public void ConsoleLoggerPrefixesGameflow()
    {
        var sw = new StringWriter();
        var prev = Console.Out;
        Console.SetOut(sw);
        try { new ConsoleLogger().Info("hello"); }
        finally { Console.SetOut(prev); }
        Assert.Contains("[gameflow] hello", sw.ToString());
    }

    [Fact]
    public void DebugSilentUnlessEnabled()
    {
        var sw = new StringWriter();
        var prev = Console.Out;
        Console.SetOut(sw);
        try { new ConsoleLogger(debug: false).Debug("x"); }
        finally { Console.SetOut(prev); }
        Assert.Equal("", sw.ToString());
    }

    [Fact]
    public void NullLoggerDiscards()
    {
        var sw = new StringWriter();
        var swErr = new StringWriter();
        var prevOut = Console.Out;
        var prevErr = Console.Error;
        Console.SetOut(sw);
        Console.SetError(swErr);
        try
        {
            var log = new NullLogger();
            log.Info("a"); log.Warn("b"); log.Error("c"); log.Debug("d");
        }
        finally { Console.SetOut(prevOut); Console.SetError(prevErr); }
        Assert.Equal("", sw.ToString());
        Assert.Equal("", swErr.ToString());
    }
}
