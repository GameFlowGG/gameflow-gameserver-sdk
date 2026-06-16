//! Wire DTOs and the public server-info types.
//!
//! The local GameFlow runtime speaks grpc-gateway JSON. Two quirks shape these
//! structs: `int64` fields (list capacity, timestamps) arrive as JSON
//! *strings*, and the real sidecar marshals proto field names, so the game
//! server arrives as `object_meta` while local mode builds `objectMeta`. Both
//! are handled here at the deserialization boundary.

use std::collections::HashMap;
use std::fmt;

use serde::de::{self, Deserializer, Unexpected, Visitor};
use serde::Deserialize;

/// A network port exposed by the server.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ServerPort {
    /// The port's configured name (e.g. `"default"`).
    pub name: String,
    /// The assigned port number.
    pub port: i32,
}

/// Current server details, as reported by the platform.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ServerInfo {
    /// The server object's name.
    pub name: String,
    /// Lifecycle state (`"Scheduled"`, `"Ready"`, `"Shutdown"`, ...).
    pub state: String,
    /// The server's address.
    pub address: String,
    /// Exposed ports.
    pub ports: Vec<ServerPort>,
    /// Platform labels.
    pub labels: HashMap<String, String>,
    /// Platform annotations (the launch payload lives here).
    pub annotations: HashMap<String, String>,
}

#[derive(Debug, Clone, Default, Deserialize)]
pub(crate) struct RawObjectMeta {
    #[serde(default)]
    pub name: Option<String>,
    #[serde(default, deserialize_with = "de_null_default")]
    pub annotations: HashMap<String, String>,
    #[serde(default, deserialize_with = "de_null_default")]
    pub labels: HashMap<String, String>,
}

#[derive(Debug, Clone, Default, Deserialize)]
pub(crate) struct RawPort {
    #[serde(default)]
    pub name: Option<String>,
    #[serde(default, deserialize_with = "de_null_default")]
    pub port: i32,
}

#[derive(Debug, Clone, Default, Deserialize)]
pub(crate) struct RawList {
    /// int64 capacity; arrives as a JSON string from grpc-gateway.
    #[serde(default, deserialize_with = "de_i64_str_or_num")]
    pub capacity: i64,
    #[serde(default, deserialize_with = "de_null_default")]
    pub values: Vec<String>,
}

#[derive(Debug, Clone, Default, Deserialize)]
pub(crate) struct RawStatus {
    #[serde(default)]
    pub state: Option<String>,
    #[serde(default)]
    pub address: Option<String>,
    #[serde(default, deserialize_with = "de_null_default")]
    pub ports: Vec<RawPort>,
    #[serde(default, deserialize_with = "de_null_default")]
    pub lists: HashMap<String, RawList>,
}

/// A game server object as it arrives over the wire.
///
/// `object_meta` is the proto spelling the real sidecar emits; `objectMeta` is
/// the camelCase spelling local mode builds. `alias` accepts both into the same
/// field, so the rest of the SDK only ever reads `object_meta`.
#[derive(Debug, Clone, Default, Deserialize)]
pub(crate) struct RawGameServer {
    #[serde(default, rename = "object_meta", alias = "objectMeta")]
    pub object_meta: Option<RawObjectMeta>,
    #[serde(default)]
    pub status: Option<RawStatus>,
}

/// One line of the watch stream: either a result or a gateway error.
#[derive(Debug, Deserialize)]
pub(crate) struct WatchLine {
    #[serde(default)]
    pub result: Option<RawGameServer>,
    #[serde(default)]
    pub error: Option<GatewayError>,
}

/// A grpc-gateway error body (`{"code": N, "message": "...", "details": [...]}`).
#[derive(Debug, Default, Deserialize)]
pub(crate) struct GatewayError {
    #[serde(default)]
    pub code: Option<i64>,
    #[serde(default)]
    pub message: Option<String>,
}

/// Parsed players-list state. `exists` is false when tracking is disabled.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub(crate) struct PlayerListSnapshot {
    pub exists: bool,
    pub capacity: i64,
    pub values: Vec<String>,
}

impl PlayerListSnapshot {
    pub(crate) fn disabled() -> Self {
        PlayerListSnapshot {
            exists: false,
            capacity: 0,
            values: Vec::new(),
        }
    }
}

/// Builds a [`ServerInfo`] from the wire object.
pub(crate) fn to_info(gs: &RawGameServer) -> ServerInfo {
    let meta = gs.object_meta.clone().unwrap_or_default();
    let status = gs.status.clone().unwrap_or_default();
    ServerInfo {
        name: meta.name.unwrap_or_default(),
        state: status.state.unwrap_or_default(),
        address: status.address.unwrap_or_default(),
        ports: status
            .ports
            .into_iter()
            .map(|p| ServerPort {
                name: p.name.unwrap_or_default(),
                port: p.port,
            })
            .collect(),
        labels: meta.labels,
        annotations: meta.annotations,
    }
}

/// grpc-gateway encodes `int64` as a JSON string, but local mode and
/// hand-written fixtures may send a bare number. Accept both.
fn de_i64_str_or_num<'de, D>(deserializer: D) -> std::result::Result<i64, D::Error>
where
    D: Deserializer<'de>,
{
    struct I64Visitor;

    impl<'de> Visitor<'de> for I64Visitor {
        type Value = i64;

        fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            f.write_str("an i64 as a JSON number or string")
        }

        fn visit_i64<E: de::Error>(self, v: i64) -> std::result::Result<i64, E> {
            Ok(v)
        }

        // An explicit JSON null coerces to 0, matching the TS reference's
        // `Number(value ?? 0)`.
        fn visit_unit<E: de::Error>(self) -> std::result::Result<i64, E> {
            Ok(0)
        }

        fn visit_u64<E: de::Error>(self, v: u64) -> std::result::Result<i64, E> {
            i64::try_from(v).map_err(|_| E::invalid_value(Unexpected::Unsigned(v), &self))
        }

        fn visit_f64<E: de::Error>(self, v: f64) -> std::result::Result<i64, E> {
            if v.fract() == 0.0 && v >= i64::MIN as f64 && v <= i64::MAX as f64 {
                Ok(v as i64)
            } else {
                Err(E::invalid_value(Unexpected::Float(v), &self))
            }
        }

        fn visit_str<E: de::Error>(self, v: &str) -> std::result::Result<i64, E> {
            v.trim()
                .parse::<i64>()
                .map_err(|_| E::invalid_value(Unexpected::Str(v), &self))
        }

        fn visit_string<E: de::Error>(self, v: String) -> std::result::Result<i64, E> {
            self.visit_str(&v)
        }
    }

    deserializer.deserialize_any(I64Visitor)
}

/// Coerces an explicit JSON `null` to the type's default, matching the TS
/// reference's permissive `value ?? default`. A stray null on one field never
/// fails the whole snapshot parse.
fn de_null_default<'de, D, T>(deserializer: D) -> std::result::Result<T, D::Error>
where
    D: Deserializer<'de>,
    T: Deserialize<'de> + Default,
{
    Ok(Option::<T>::deserialize(deserializer)?.unwrap_or_default())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_capacity_as_string_or_number() {
        let from_str: RawList = serde_json::from_str(r#"{"capacity":"8","values":["a"]}"#).unwrap();
        assert_eq!(from_str.capacity, 8);
        assert_eq!(from_str.values, vec!["a"]);

        let from_num: RawList = serde_json::from_str(r#"{"capacity":8}"#).unwrap();
        assert_eq!(from_num.capacity, 8);

        let empty: RawList = serde_json::from_str("{}").unwrap();
        assert_eq!(empty.capacity, 0);
        assert!(empty.values.is_empty());
    }

    #[test]
    fn accepts_both_object_meta_spellings() {
        let proto: RawGameServer =
            serde_json::from_str(r#"{"object_meta":{"name":"gs"}}"#).unwrap();
        assert_eq!(proto.object_meta.unwrap().name.as_deref(), Some("gs"));

        let camel: RawGameServer = serde_json::from_str(r#"{"objectMeta":{"name":"gs"}}"#).unwrap();
        assert_eq!(camel.object_meta.unwrap().name.as_deref(), Some("gs"));
    }

    #[test]
    fn coerces_explicit_null_to_defaults() {
        // grpc-gateway shouldn't emit nulls, but a stray null on any field must
        // never fail the whole snapshot parse (matching the TS `?? default`).
        let list: RawList = serde_json::from_str(r#"{"capacity":null,"values":null}"#).unwrap();
        assert_eq!(list.capacity, 0);
        assert!(list.values.is_empty());

        let gs: RawGameServer = serde_json::from_str(
            r#"{"object_meta":{"name":"gs","annotations":null,"labels":null},
                "status":{"ports":null,"lists":null}}"#,
        )
        .unwrap();
        let meta = gs.object_meta.unwrap();
        assert!(meta.annotations.is_empty());
        assert!(meta.labels.is_empty());
        let status = gs.status.unwrap();
        assert!(status.ports.is_empty());
        assert!(status.lists.is_empty());
    }

    #[test]
    fn ignores_unknown_fields() {
        let gs: RawGameServer = serde_json::from_str(
            r#"{"object_meta":{"name":"gs","resourceVersion":"5"},"extra":true}"#,
        )
        .unwrap();
        assert_eq!(gs.object_meta.unwrap().name.as_deref(), Some("gs"));
    }
}
