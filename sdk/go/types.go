package gameflow

import (
	"encoding/json"
	"strconv"
	"strings"
)

// ServerPort is a network port exposed by the server.
type ServerPort struct {
	// Name is the port's configured name (e.g. "default").
	Name string
	// Port is the assigned port number.
	Port int32
}

// ServerInfo holds the current server details, as reported by the platform.
type ServerInfo struct {
	// Name is the server object's name.
	Name string
	// State is the lifecycle state ("Scheduled", "Ready", "Shutdown", ...).
	State string
	// Address is the server's address.
	Address string
	// Ports lists the exposed ports.
	Ports []ServerPort
	// Labels holds platform labels (never nil).
	Labels map[string]string
	// Annotations holds platform annotations (never nil); the launch payload
	// lives here.
	Annotations map[string]string
}

// --- wire DTOs ---------------------------------------------------------------
//
// The local GameFlow runtime speaks grpc-gateway JSON. Two quirks shape these
// types: int64 fields (list capacity, timestamps) arrive as JSON *strings*, and
// the real sidecar marshals proto field names, so the game server arrives as
// `object_meta` while local mode builds `objectMeta`. Both are handled here at
// the deserialization boundary.

// flexInt64 decodes an int64 that grpc-gateway encodes as a JSON string, while
// still accepting a bare JSON number (local mode, hand-written fixtures) and an
// explicit null (coerced to 0, matching the TS reference's Number(value ?? 0)).
type flexInt64 int64

func (f *flexInt64) UnmarshalJSON(data []byte) error {
	s := strings.TrimSpace(string(data))
	if s == "null" || s == "" {
		*f = 0
		return nil
	}
	if s[0] == '"' {
		var str string
		if err := json.Unmarshal(data, &str); err != nil {
			return err
		}
		str = strings.TrimSpace(str)
		if str == "" {
			*f = 0
			return nil
		}
		n, err := strconv.ParseInt(str, 10, 64)
		if err != nil {
			return err
		}
		*f = flexInt64(n)
		return nil
	}
	var n int64
	if err := json.Unmarshal(data, &n); err != nil {
		return err
	}
	*f = flexInt64(n)
	return nil
}

type rawObjectMeta struct {
	Name        string            `json:"name"`
	Annotations map[string]string `json:"annotations"`
	Labels      map[string]string `json:"labels"`
}

type rawPort struct {
	Name string `json:"name"`
	Port int32  `json:"port"`
}

type rawList struct {
	// Capacity is an int64 that arrives as a JSON string from grpc-gateway.
	Capacity flexInt64 `json:"capacity"`
	Values   []string  `json:"values"`
}

type rawStatus struct {
	State   string             `json:"state"`
	Address string             `json:"address"`
	Ports   []rawPort          `json:"ports"`
	Lists   map[string]rawList `json:"lists"`
}

// rawGameServer is a game server object as it arrives over the wire. The real
// sidecar emits the proto spelling `object_meta`; local mode builds the
// camelCase `objectMeta`. Both decode into ObjectMeta so the rest of the SDK
// only ever reads one field.
type rawGameServer struct {
	ObjectMeta *rawObjectMeta
	Status     *rawStatus
}

func (r *rawGameServer) UnmarshalJSON(data []byte) error {
	var aux struct {
		ObjectMetaSnake *rawObjectMeta `json:"object_meta"`
		ObjectMetaCamel *rawObjectMeta `json:"objectMeta"`
		Status          *rawStatus     `json:"status"`
	}
	if err := json.Unmarshal(data, &aux); err != nil {
		return err
	}
	if aux.ObjectMetaSnake != nil {
		r.ObjectMeta = aux.ObjectMetaSnake
	} else {
		r.ObjectMeta = aux.ObjectMetaCamel
	}
	r.Status = aux.Status
	return nil
}

// watchLine is one line of the watch stream: either a result or a gateway error.
type watchLine struct {
	Result *rawGameServer `json:"result"`
	Error  *gatewayError  `json:"error"`
}

// gatewayError is a grpc-gateway error body
// ({"code": N, "message": "...", "details": [...]}).
type gatewayError struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
}

// playerListSnapshot is the parsed players-list state. exists is false when
// tracking is disabled.
type playerListSnapshot struct {
	exists   bool
	capacity int64
	values   []string
}

func disabledSnapshot() playerListSnapshot {
	return playerListSnapshot{exists: false, capacity: 0, values: nil}
}

// parseList coerces an optional raw list into a snapshot.
func parseList(raw *rawList) playerListSnapshot {
	if raw == nil {
		return disabledSnapshot()
	}
	values := raw.Values
	if values == nil {
		values = []string{}
	}
	return playerListSnapshot{
		exists:   true,
		capacity: int64(raw.Capacity),
		values:   values,
	}
}

// toInfo builds a [ServerInfo] from the wire object, defaulting nil maps and
// slices to empty so callers never hit a nil panic.
func toInfo(gs *rawGameServer) ServerInfo {
	info := ServerInfo{
		Ports:       []ServerPort{},
		Labels:      map[string]string{},
		Annotations: map[string]string{},
	}
	if gs == nil {
		return info
	}
	if gs.ObjectMeta != nil {
		info.Name = gs.ObjectMeta.Name
		if gs.ObjectMeta.Labels != nil {
			info.Labels = gs.ObjectMeta.Labels
		}
		if gs.ObjectMeta.Annotations != nil {
			info.Annotations = gs.ObjectMeta.Annotations
		}
	}
	if gs.Status != nil {
		info.State = gs.Status.State
		info.Address = gs.Status.Address
		for _, p := range gs.Status.Ports {
			info.Ports = append(info.Ports, ServerPort{Name: p.Name, Port: p.Port})
		}
	}
	return info
}

// payloadOf returns the launch payload annotation and whether it is present.
func payloadOf(gs *rawGameServer) (string, bool) {
	if gs == nil || gs.ObjectMeta == nil || gs.ObjectMeta.Annotations == nil {
		return "", false
	}
	v, ok := gs.ObjectMeta.Annotations[payloadAnnotation]
	return v, ok
}
