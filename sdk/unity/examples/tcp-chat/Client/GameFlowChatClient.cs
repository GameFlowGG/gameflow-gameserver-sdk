using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using UnityEngine;

/// <summary>
/// Minimal TCP chat client for the GameFlow example server, with a self-contained IMGUI UI:
/// no scene wiring needed — attach to a GameObject and press Play. Point it at 127.0.0.1:7777
/// for a local server, or at a deployed GameFlow server's public address. A real game would
/// use its own netcode and UI; this exists only to drive the server end to end.
/// </summary>
public sealed class GameFlowChatClient : MonoBehaviour
{
    public string host = "127.0.0.1";
    public string port = "7777";

    private TcpClient _socket;
    private StreamWriter _writer;
    private CancellationTokenSource _cts;
    private readonly ConcurrentQueue<string> _incoming = new ConcurrentQueue<string>();
    private readonly List<string> _log = new List<string>();
    private Vector2 _scroll;
    private string _message = "";
    private volatile bool _connected;
    private string _status = "disconnected";

    private void Update()
    {
        bool appended = false;
        while (_incoming.TryDequeue(out var line))
        {
            _log.Add(line);
            if (_log.Count > 200) _log.RemoveAt(0);
            appended = true;
        }
        if (appended) _scroll.y = float.MaxValue;
    }

    private void OnGUI()
    {
        GUILayout.BeginArea(new Rect(10, 10, 540, Screen.height - 20), GUI.skin.box);

        GUILayout.Label("GameFlow chat client");
        GUILayout.BeginHorizontal();
        GUILayout.Label("host", GUILayout.Width(34));
        host = GUILayout.TextField(host, GUILayout.Width(170));
        GUILayout.Label("port", GUILayout.Width(34));
        port = GUILayout.TextField(port, GUILayout.Width(60));
        if (!_connected) { if (GUILayout.Button("Connect")) Connect(); }
        else { if (GUILayout.Button("Disconnect")) Disconnect(); }
        GUILayout.EndHorizontal();
        GUILayout.Label("status: " + _status);

        _scroll = GUILayout.BeginScrollView(_scroll, GUI.skin.box, GUILayout.Height(Screen.height - 160));
        foreach (var l in _log) GUILayout.Label(l);
        GUILayout.EndScrollView();

        GUILayout.BeginHorizontal();
        GUI.enabled = _connected;
        _message = GUILayout.TextField(_message);
        if (GUILayout.Button("Send", GUILayout.Width(70)) && _message.Length > 0)
        {
            SendLine(_message);
            _message = "";
        }
        GUI.enabled = true;
        GUILayout.EndHorizontal();

        GUILayout.EndArea();
    }

    private void Connect()
    {
        try
        {
            _socket = new TcpClient();
            _socket.Connect(host.Trim(), int.Parse(port.Trim()));
            _writer = new StreamWriter(_socket.GetStream(), new UTF8Encoding(false)) { AutoFlush = true };
            _cts = new CancellationTokenSource();
            _connected = true;
            _status = $"connected to {host}:{port}";
            _ = ReadLoop(_socket, _cts.Token);
        }
        catch (Exception e)
        {
            _status = "connect failed: " + e.Message;
            _incoming.Enqueue("! " + e.Message);
        }
    }

    private async Task ReadLoop(TcpClient socket, CancellationToken ct)
    {
        try
        {
            using var reader = new StreamReader(socket.GetStream(), Encoding.UTF8);
            string line;
            while (!ct.IsCancellationRequested && (line = await reader.ReadLineAsync().ConfigureAwait(false)) != null)
                _incoming.Enqueue(line);
        }
        catch (Exception) { /* socket closed */ }
        finally
        {
            _connected = false;
            _status = "disconnected";
            _incoming.Enqueue("* disconnected");
        }
    }

    private void SendLine(string text)
    {
        try { _writer.WriteLine(text); _incoming.Enqueue("me: " + text); }
        catch (Exception e) { _incoming.Enqueue("! send failed: " + e.Message); }
    }

    private void Disconnect()
    {
        _connected = false;
        _status = "disconnected";
        try { _cts?.Cancel(); } catch { }
        try { _socket?.Close(); } catch { }
    }

    private void OnApplicationQuit() => Disconnect();
    private void OnDestroy() => Disconnect();
}
