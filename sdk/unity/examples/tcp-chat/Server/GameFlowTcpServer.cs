using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using GameFlow;
using GameFlow.Unity;
using UnityEngine;

/// <summary>
/// Minimal headless TCP line-chat server wired to GameFlow: every TCP connection is one
/// tracked player session. Off GameFlow it runs in local mode automatically (set
/// GAMEFLOW_MAX_PLAYERS to cap capacity), so the same build runs on your machine with no
/// configuration. The Unity twin of the node-tcp / tokio-tcp / go tcp-server examples.
///
/// Attach to a GameObject in a server scene, then build it as a Dedicated Server.
/// </summary>
public sealed class GameFlowTcpServer : MonoBehaviour
{
    private GameFlowClient _gf;
    private TcpListener _listener;
    private CancellationTokenSource _cts;
    private readonly object _clientsLock = new object();
    private readonly Dictionary<string, StreamWriter> _clients = new Dictionary<string, StreamWriter>();

    private async void Start()
    {
        // Headless servers don't render: cap the loop so we don't peg a CPU core.
        Application.targetFrameRate = 30;

        // The runner pumps SDK callbacks onto the main thread and sends a clean
        // shutdown when the process quits (SIGTERM on GameFlow, or Stop in the editor).
        var runner = GameFlowRunner.Create();
        _gf = new GameFlowClient(new GameFlowOptions
        {
            Logger = new UnityDebugLogger(),
            Dispatcher = runner.Dispatcher,
        });
        runner.Bind(_gf);

        try
        {
            await _gf.Start(); // connect (with retries) on GameFlow, or fall back to local mode
        }
        catch (GameFlowException e)
        {
            Debug.LogError($"[server] GameFlow start failed: {e.Code} — {e.Message}");
            return;
        }

        int port = GameFlowEnv.DefaultPort ?? 7777;
        _cts = new CancellationTokenSource();

        try
        {
            _listener = new TcpListener(IPAddress.Any, port);
            _listener.Start();
        }
        catch (Exception e)
        {
            Debug.LogError($"[server] could not listen on :{port} — {e.Message}");
            return;
        }

        Debug.Log($"[server] listening on :{port}  (mode={_gf.Mode}, region={GameFlowEnv.Region ?? "-"}, capacity={CapLabel()})");

        await _gf.Ready(); // health reporting starts automatically on GameFlow

        _ = AcceptLoop(_cts.Token);
    }

    private async Task AcceptLoop(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            TcpClient socket;
            try { socket = await _listener.AcceptTcpClientAsync().ConfigureAwait(false); }
            catch (ObjectDisposedException) { break; }            // listener stopped
            catch (Exception e) { Debug.LogWarning($"[server] accept failed: {e.Message}"); continue; }

            _ = HandleClient(socket);
        }
    }

    private async Task HandleClient(TcpClient socket)
    {
        // One session per connection. A real game would plug in its own player identity here.
        string sessionId = Guid.NewGuid().ToString();
        var stream = socket.GetStream();
        var reader = new StreamReader(stream, Encoding.UTF8);
        var writer = new StreamWriter(stream, new UTF8Encoding(false)) { AutoFlush = true };

        try
        {
            await _gf.Players.Connect(sessionId).ConfigureAwait(false);
        }
        catch (ServerFullException e)
        {
            await SafeWrite(writer, $"server full (capacity {e.Capacity})").ConfigureAwait(false);
            socket.Close();
            return;
        }
        catch (PlayerTrackingDisabledException)
        {
            // Game configured with max players = 0: tracking is off, but still let them chat.
        }
        catch (Exception e)
        {
            Debug.LogWarning($"[server] connect rejected: {e.Message}");
            socket.Close();
            return;
        }

        lock (_clientsLock) _clients[sessionId] = writer;
        Debug.Log($"[server] + {Short(sessionId)} joined ({_gf.Players.Count} online)");
        await SafeWrite(writer, $"welcome {Short(sessionId)} — {_gf.Players.Count} online").ConfigureAwait(false);
        await Broadcast($"* {Short(sessionId)} joined ({_gf.Players.Count} online)", sessionId).ConfigureAwait(false);

        try
        {
            string line;
            while ((line = await reader.ReadLineAsync().ConfigureAwait(false)) != null)
            {
                if (line.Length == 0) continue;
                await Broadcast($"{Short(sessionId)}: {line}", sessionId).ConfigureAwait(false);
            }
        }
        catch (Exception) { /* client dropped mid-read */ }
        finally
        {
            lock (_clientsLock) _clients.Remove(sessionId);
            try { await _gf.Players.Disconnect(sessionId).ConfigureAwait(false); } catch { }
            socket.Close();
            Debug.Log($"[server] - {Short(sessionId)} left ({_gf.Players.Count} online)");
            await Broadcast($"* {Short(sessionId)} left ({_gf.Players.Count} online)", sessionId).ConfigureAwait(false);
        }
    }

    private async Task Broadcast(string message, string except)
    {
        List<KeyValuePair<string, StreamWriter>> targets;
        lock (_clientsLock) targets = new List<KeyValuePair<string, StreamWriter>>(_clients);
        foreach (var kv in targets)
        {
            if (kv.Key == except) continue;
            await SafeWrite(kv.Value, message).ConfigureAwait(false);
        }
    }

    private static async Task SafeWrite(StreamWriter writer, string message)
    {
        try { await writer.WriteLineAsync(message).ConfigureAwait(false); }
        catch (Exception) { /* broken pipe: drop */ }
    }

    private string CapLabel()
    {
        long c = _gf.Players.Capacity;
        return c == long.MaxValue ? "unlimited" : c.ToString();
    }

    private static string Short(string sessionId) => sessionId.Substring(0, 8);

    private void OnApplicationQuit()
    {
        // The runner sends the GameFlow shutdown; here we just stop accepting and close sockets.
        _cts?.Cancel();
        try { _listener?.Stop(); } catch { }
        lock (_clientsLock)
        {
            foreach (var w in _clients.Values) { try { w.Close(); } catch { } }
            _clients.Clear();
        }
    }
}
