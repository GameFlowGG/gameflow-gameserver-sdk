using System;
using System.Collections.Generic;

namespace GameFlow
{
    /// <summary>A named port the platform assigned to this server.</summary>
    public sealed class ServerPort
    {
        public string Name;
        public int Port;
    }

    /// <summary>The tracked player list. <see cref="TrackingEnabled"/> is false when the game runs with max players = 0.</summary>
    public sealed class PlayerList
    {
        public bool TrackingEnabled;
        public long Capacity;
        public IReadOnlyList<string> SessionIds = Array.Empty<string>();
    }

    /// <summary>Current server details, surfaced from the runtime.</summary>
    public sealed class ServerInfo
    {
        public string Name;
        public string State;
        public string Address;
        public string Region;
        public string BuildId;
        public IReadOnlyList<ServerPort> Ports = Array.Empty<ServerPort>();
        public IReadOnlyDictionary<string, string> Labels = new Dictionary<string, string>();
        public IReadOnlyDictionary<string, string> Annotations = new Dictionary<string, string>();
        public PlayerList Players;
    }

    /// <summary>Maps the runtime's wire JSON (grpc-gateway/proto names) into the SDK model. Tolerant of missing optional fields.</summary>
    internal static class Model
    {
        internal const string PayloadAnnotation = "GAMEFLOW_PAYLOAD";

        internal static ServerInfo ParseGameServer(object root)
        {
            var d = root as Dictionary<string, object> ?? new Dictionary<string, object>();
            var meta = Dict(d, "object_meta");
            var status = Dict(d, "status");
            var annotations = StrDict(Dict(meta, "annotations"));
            var labels = StrDict(Dict(meta, "labels"));

            var ports = new List<ServerPort>();
            if (status.TryGetValue("ports", out var po) && po is List<object> portList)
            {
                foreach (var p in portList)
                {
                    var pd = p as Dictionary<string, object> ?? new Dictionary<string, object>();
                    ports.Add(new ServerPort { Name = Str(pd, "name"), Port = (int)Json.AsLong(Get(pd, "port")) });
                }
            }

            PlayerList players;
            var lists = Dict(status, "lists");
            if (lists.TryGetValue("players", out var pv) && pv is Dictionary<string, object>)
                players = ParseList(pv);
            else
                players = new PlayerList { TrackingEnabled = false, Capacity = 0, SessionIds = Array.Empty<string>() };

            return new ServerInfo
            {
                Name = Str(meta, "name"),
                State = Str(status, "state"),
                Address = Str(status, "address"),
                Region = annotations.TryGetValue("GAMEFLOW_REGION", out var r) ? r : "",
                BuildId = annotations.TryGetValue("GAMEFLOW_BUILD_ID", out var b) ? b : "",
                Ports = ports,
                Labels = labels,
                Annotations = annotations,
                Players = players,
            };
        }

        internal static PlayerList ParseList(object root)
        {
            var d = root as Dictionary<string, object> ?? new Dictionary<string, object>();
            var values = new List<string>();
            if (d.TryGetValue("values", out var v) && v is List<object> vl)
                foreach (var x in vl) values.Add(x as string);
            long capacity = d.TryGetValue("capacity", out var c) ? Json.AsLong(c) : 0;
            return new PlayerList { TrackingEnabled = true, Capacity = capacity, SessionIds = values };
        }

        /// <summary>Extracts the opaque payload from a parsed gameserver's annotations, or null when absent.</summary>
        internal static string PayloadOf(ServerInfo info)
        {
            if (info?.Annotations != null && info.Annotations.TryGetValue(PayloadAnnotation, out var p)) return p;
            return null;
        }

        private static Dictionary<string, object> Dict(Dictionary<string, object> d, string key)
            => d.TryGetValue(key, out var v) && v is Dictionary<string, object> nested ? nested : new Dictionary<string, object>();

        private static Dictionary<string, string> StrDict(Dictionary<string, object> d)
        {
            var result = new Dictionary<string, string>();
            foreach (var kv in d) result[kv.Key] = kv.Value as string ?? kv.Value?.ToString();
            return result;
        }

        private static string Str(Dictionary<string, object> d, string key)
            => d.TryGetValue(key, out var v) ? v as string ?? "" : "";

        private static object Get(Dictionary<string, object> d, string key)
            => d.TryGetValue(key, out var v) ? v : null;
    }
}
