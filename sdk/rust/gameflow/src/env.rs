//! Platform environment: assigned ports, region and build id.
//!
//! GameFlow injects networking and placement details as `GAMEFLOW_*` env vars.
//! All lookups go through an [`Env`] source so the same logic is testable
//! without mutating the process environment.

use std::sync::Arc;

type EnvLookup = Arc<dyn Fn(&str) -> Option<String> + Send + Sync>;

/// A source of environment variables. Production reads the process
/// environment; tests can inject a fixed map.
#[derive(Clone)]
pub(crate) struct Env {
    lookup: EnvLookup,
}

impl Env {
    pub(crate) fn process() -> Self {
        Env {
            lookup: Arc::new(|key| std::env::var(key).ok()),
        }
    }

    #[cfg(test)]
    pub(crate) fn from_pairs(pairs: &[(&str, &str)]) -> Self {
        let map: std::collections::HashMap<String, String> = pairs
            .iter()
            .map(|(k, v)| ((*k).to_string(), (*v).to_string()))
            .collect();
        Env {
            lookup: Arc::new(move |key| map.get(key).cloned()),
        }
    }

    pub(crate) fn get(&self, key: &str) -> Option<String> {
        (self.lookup)(key)
    }
}

/// Platform-provided server metadata derived from the environment.
pub(crate) struct GameFlowEnv {
    env: Env,
}

impl GameFlowEnv {
    pub(crate) fn new(env: Env) -> Self {
        GameFlowEnv { env }
    }

    /// Ports assigned to this server.
    pub(crate) fn ports(&self) -> Ports {
        Ports {
            env: self.env.clone(),
        }
    }

    /// Region this server runs in, when provided by the platform.
    pub(crate) fn region(&self) -> Option<String> {
        self.env.get("GAMEFLOW_REGION")
    }

    /// Build id of the running image, when provided by the platform.
    pub(crate) fn build_id(&self) -> Option<String> {
        self.env.get("GAMEFLOW_BUILD_ID")
    }
}

/// Ports assigned to this server, read from `GAMEFLOW_*_PORT` env vars.
///
/// Every accessor returns [`None`] rather than failing when a port is absent,
/// so callers can fall back to their own default.
#[derive(Clone)]
pub struct Ports {
    env: Env,
}

impl Ports {
    /// The game's primary port (`GAMEFLOW_DEFAULT_PORT`).
    pub fn default(&self) -> Option<u16> {
        parse_port(self.env.get("GAMEFLOW_DEFAULT_PORT"))
    }

    /// An additional named port, as configured in the game's networking
    /// settings. Names are normalized (`"voice chat"` -> `VOICE_CHAT`).
    pub fn get(&self, name: &str) -> Option<u16> {
        parse_port(
            self.env
                .get(&format!("GAMEFLOW_{}_PORT", normalize_port_name(name))),
        )
    }

    /// TLS-terminated listener ports, present only when the game uses TLS.
    pub fn tls(&self) -> TlsPorts {
        TlsPorts {
            env: self.env.clone(),
        }
    }
}

/// TLS-terminated listener ports (`GAMEFLOW_TLS_*_PORT`).
#[derive(Clone)]
pub struct TlsPorts {
    env: Env,
}

impl TlsPorts {
    /// The primary TLS port (`GAMEFLOW_TLS_DEFAULT_PORT`).
    pub fn default(&self) -> Option<u16> {
        parse_port(self.env.get("GAMEFLOW_TLS_DEFAULT_PORT"))
    }

    /// An additional named TLS port.
    pub fn get(&self, name: &str) -> Option<u16> {
        parse_port(
            self.env
                .get(&format!("GAMEFLOW_TLS_{}_PORT", normalize_port_name(name))),
        )
    }
}

/// Mirrors the platform's env var naming: names are trimmed, spaces become
/// underscores and the result is uppercased (`"voice chat"` -> `VOICE_CHAT`).
pub(crate) fn normalize_port_name(name: &str) -> String {
    name.trim().replace(' ', "_").to_uppercase()
}

fn parse_port(value: Option<String>) -> Option<u16> {
    let value = value?;
    let trimmed = value.trim();
    if trimmed.is_empty() {
        return None;
    }
    trimmed.parse::<u16>().ok().filter(|port| *port > 0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn normalizes_port_names() {
        assert_eq!(normalize_port_name("voice chat"), "VOICE_CHAT");
        assert_eq!(normalize_port_name("  default "), "DEFAULT");
        assert_eq!(normalize_port_name("Game"), "GAME");
    }

    #[test]
    fn parses_only_valid_ports() {
        assert_eq!(parse_port(Some("7777".into())), Some(7777));
        assert_eq!(parse_port(Some(" 8080 ".into())), Some(8080));
        assert_eq!(parse_port(Some("0".into())), None);
        assert_eq!(parse_port(Some("-1".into())), None);
        assert_eq!(parse_port(Some("abc".into())), None);
        assert_eq!(parse_port(Some("99999".into())), None); // > u16::MAX
        assert_eq!(parse_port(Some(String::new())), None);
        assert_eq!(parse_port(None), None);
    }

    #[test]
    fn reads_named_and_tls_ports() {
        let env = Env::from_pairs(&[
            ("GAMEFLOW_DEFAULT_PORT", "7777"),
            ("GAMEFLOW_VOICE_CHAT_PORT", "7778"),
            ("GAMEFLOW_TLS_DEFAULT_PORT", "8443"),
            ("GAMEFLOW_REGION", "eu-west-1"),
            ("GAMEFLOW_BUILD_ID", "build-42"),
        ]);
        let gf_env = GameFlowEnv::new(env);
        let ports = gf_env.ports();
        assert_eq!(ports.default(), Some(7777));
        assert_eq!(ports.get("voice chat"), Some(7778));
        assert_eq!(ports.get("missing"), None);
        assert_eq!(ports.tls().default(), Some(8443));
        assert_eq!(gf_env.region().as_deref(), Some("eu-west-1"));
        assert_eq!(gf_env.build_id().as_deref(), Some("build-42"));
    }
}
