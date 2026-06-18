using System;
using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Text.Json;

// The fixture spawns one Node process and has timing-sensitive assertions; run the suite
// serially so parallel CPU contention can't perturb health/watch timing.
[assembly: Xunit.CollectionBehavior(DisableTestParallelization = true)]

/// <summary>
/// Drives the shared fake-runtime fixture (tools/conformance/serve.mjs) the same way every other
/// GameFlow SDK's conformance suite does: spawn Node, read the announced ports, poke the control server.
/// Skips cleanly when Node is absent.
/// </summary>
internal sealed class Fixture : IDisposable
{
    private readonly Process _proc;
    public int Port { get; }
    public int ControlPort { get; }

    private static readonly HttpClient Http = new HttpClient();

    public static bool NodeAvailable
    {
        get
        {
            try
            {
                using var p = Process.Start(new ProcessStartInfo("node", "--version")
                {
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    UseShellExecute = false,
                });
                p.WaitForExit(5000);
                return p.ExitCode == 0;
            }
            catch { return false; }
        }
    }

    private Fixture(Process proc, int port, int controlPort)
    {
        _proc = proc;
        Port = port;
        ControlPort = controlPort;
    }

    public static Fixture Start(params string[] args)
    {
        var script = LocateServeScript();
        var psi = new ProcessStartInfo("node")
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            WorkingDirectory = Path.GetDirectoryName(script),
        };
        psi.ArgumentList.Add(script);
        foreach (var a in args) psi.ArgumentList.Add(a);

        var proc = Process.Start(psi) ?? throw new InvalidOperationException("failed to spawn node fixture");

        int port = 0, controlPort = 0;
        var deadline = DateTime.UtcNow.AddSeconds(15);
        while ((port == 0 || controlPort == 0) && DateTime.UtcNow < deadline)
        {
            var line = proc.StandardOutput.ReadLine();
            if (line == null) throw new InvalidOperationException("fixture exited before announcing its ports");
            if (line.StartsWith("PORT=")) port = int.Parse(line.Substring("PORT=".Length));
            else if (line.StartsWith("CONTROL_PORT=")) controlPort = int.Parse(line.Substring("CONTROL_PORT=".Length));
        }
        if (port == 0 || controlPort == 0) throw new InvalidOperationException("fixture did not announce its ports in time");

        // Drain remaining stdout so the child never blocks on a full pipe.
        proc.OutputDataReceived += (_, __) => { };
        return new Fixture(proc, port, controlPort);
    }

    public void ControlPost(string path)
    {
        using var resp = Http.PostAsync($"http://127.0.0.1:{ControlPort}{path}", null).GetAwaiter().GetResult();
    }

    public int CountRequests(string path)
    {
        var json = Http.GetStringAsync($"http://127.0.0.1:{ControlPort}/requests").GetAwaiter().GetResult();
        using var doc = JsonDocument.Parse(json);
        int n = 0;
        foreach (var el in doc.RootElement.EnumerateArray())
            if (el.TryGetProperty("path", out var p) && p.GetString() == path) n++;
        return n;
    }

    private static string LocateServeScript()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir != null)
        {
            var candidate = Path.Combine(dir.FullName, "tools", "conformance", "serve.mjs");
            if (File.Exists(candidate)) return candidate;
            dir = dir.Parent;
        }
        throw new FileNotFoundException("could not locate tools/conformance/serve.mjs above the test assembly");
    }

    public void Dispose()
    {
        try { if (!_proc.HasExited) _proc.Kill(true); } catch { }
        try { _proc.Dispose(); } catch { }
    }
}
