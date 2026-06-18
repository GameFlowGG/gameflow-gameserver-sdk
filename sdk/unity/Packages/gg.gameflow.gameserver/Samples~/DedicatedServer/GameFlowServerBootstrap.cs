using System.Threading.Tasks;
using GameFlow;
using GameFlow.Unity;
using UnityEngine;

// Minimal headless server bootstrap. Attach to a GameObject in your dedicated-server scene,
// or call the same sequence from your own startup code. Off GameFlow it runs in local mode
// automatically, so the same build works on your machine with no configuration.
public sealed class GameFlowServerBootstrap : MonoBehaviour
{
    private GameFlowClient _gf;

    private async void Start()
    {
        // Create the runner first: it pumps SDK callbacks onto the main thread and
        // sends a clean shutdown when the process quits.
        var runner = GameFlowRunner.Create();
        _gf = new GameFlowClient(new GameFlowOptions
        {
            Logger = new UnityDebugLogger(),
            Dispatcher = runner.Dispatcher,
        });
        runner.Bind(_gf);

        try
        {
            await _gf.Start();   // connect (with retries) or fall back to local mode
            // ... begin listening on your netcode transport here ...
            await _gf.Ready();   // health reporting starts automatically
            Debug.Log("[gameflow] server is ready for players");
        }
        catch (GameFlowException e)
        {
            Debug.LogError($"[gameflow] startup failed: {e.Code} — {e.Message}");
        }
    }

    // Call when a player session joins.
    public async Task OnPlayerJoined(string sessionId)
    {
        try
        {
            await _gf.Players.Connect(sessionId);
        }
        catch (ServerFullException e)
        {
            Debug.LogWarning($"[gameflow] rejecting {sessionId}: server full (capacity {e.Capacity})");
        }
        catch (PlayerAlreadyConnectedException)
        {
            // a reconnect of a live session — nothing to do
        }
    }

    // Call when a player session leaves.
    public async Task OnPlayerLeft(string sessionId)
    {
        await _gf.Players.Disconnect(sessionId);
    }
}
