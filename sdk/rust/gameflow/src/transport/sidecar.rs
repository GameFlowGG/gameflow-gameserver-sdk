//! REST transport against the local GameFlow runtime.
//!
//! In v1 the SDK talks to a local sidecar over plain HTTP. This is an
//! implementation detail: nothing here leaks into the public API, logs or
//! errors.

use std::sync::Arc;
use std::time::Duration;

use async_trait::async_trait;
use futures_util::StreamExt;
use reqwest::{Client, RequestBuilder, Response};
use serde_json::json;
use tokio_util::sync::CancellationToken;

use super::{parse_list, Transport, WatchSink, PLAYERS_LIST};
use crate::error::{BoxError, GameFlowError, Result};
use crate::logger::Logger;
use crate::types::{GatewayError, PlayerListSnapshot, RawGameServer, RawList, WatchLine};

/// gRPC `OUT_OF_RANGE`: the list mutation was rejected because it is at capacity.
const GRPC_OUT_OF_RANGE: i64 = 11;

pub(crate) struct SidecarTransport {
    client: Client,
    base_url: String,
    request_timeout: Duration,
    logger: Arc<dyn Logger>,
}

impl SidecarTransport {
    pub(crate) fn new(
        base_url: String,
        request_timeout: Duration,
        logger: Arc<dyn Logger>,
    ) -> Result<Self> {
        // No global timeout: unary calls get a per-request timeout below, while
        // the watch stream must run indefinitely.
        let client = Client::builder().build().map_err(|e| {
            GameFlowError::sidecar_unavailable_with("could not build HTTP client", Some(boxed(e)))
        })?;
        Ok(SidecarTransport {
            client,
            base_url,
            request_timeout,
            logger,
        })
    }

    fn url(&self, path: &str) -> String {
        // Paths contain custom verbs with a literal ':' (lists/players:addValue);
        // the URL is built by plain concatenation and the `url` crate leaves the
        // colon untouched in a path segment.
        format!("{}{}", self.base_url, path)
    }

    /// Sends a unary request with the per-request timeout. Statuses listed in
    /// `expected` are returned to the caller instead of mapping to an error.
    async fn send(&self, req: RequestBuilder, what: &str, expected: &[u16]) -> Result<Response> {
        let res = req
            .timeout(self.request_timeout)
            .send()
            .await
            .map_err(|e| self.unreachable(what, e))?;
        let status = res.status().as_u16();
        if res.status().is_success() || expected.contains(&status) {
            return Ok(res);
        }
        let body = read_gateway_error(res).await;
        Err(GameFlowError::RequestFailed {
            message: body
                .message
                .unwrap_or_else(|| format!("{what} failed with HTTP {status}")),
            status: Some(status),
            source: None,
        })
    }

    fn unreachable(&self, what: &str, cause: reqwest::Error) -> GameFlowError {
        GameFlowError::sidecar_unavailable_with(
            format!(
                "could not reach the GameFlow runtime at {} ({what})",
                self.base_url
            ),
            Some(boxed(cause)),
        )
    }
}

#[async_trait]
impl Transport for SidecarTransport {
    async fn ready(&self) -> Result<()> {
        self.send(
            self.client.post(self.url("/ready")).json(&json!({})),
            "POST /ready",
            &[],
        )
        .await?;
        Ok(())
    }

    async fn health(&self) -> Result<()> {
        self.send(
            self.client.post(self.url("/health")).json(&json!({})),
            "POST /health",
            &[],
        )
        .await?;
        Ok(())
    }

    async fn shutdown(&self) -> Result<()> {
        self.send(
            self.client.post(self.url("/shutdown")).json(&json!({})),
            "POST /shutdown",
            &[],
        )
        .await?;
        Ok(())
    }

    async fn get_game_server(&self) -> Result<RawGameServer> {
        let res = self
            .send(
                self.client.get(self.url("/gameserver")),
                "GET /gameserver",
                &[],
            )
            .await?;
        res.json::<RawGameServer>()
            .await
            .map_err(|e| GameFlowError::RequestFailed {
                message: "could not decode the gameserver response".to_string(),
                status: None,
                source: Some(boxed(e)),
            })
    }

    async fn get_player_list(&self) -> Result<PlayerListSnapshot> {
        let path = format!("/v1beta1/lists/{PLAYERS_LIST}");
        let res = self
            .send(self.client.get(self.url(&path)), "GET players list", &[404])
            .await?;
        if res.status().as_u16() == 404 {
            return Ok(PlayerListSnapshot::disabled());
        }
        let list = res
            .json::<RawList>()
            .await
            .map_err(|e| GameFlowError::RequestFailed {
                message: "could not decode the players list".to_string(),
                status: None,
                source: Some(boxed(e)),
            })?;
        Ok(parse_list(Some(&list)))
    }

    async fn add_player(&self, session_id: &str) -> Result<PlayerListSnapshot> {
        let path = format!("/v1beta1/lists/{PLAYERS_LIST}:addValue");
        let res = self
            .send(
                self.client
                    .post(self.url(&path))
                    .json(&json!({ "value": session_id })),
                "addValue",
                &[400, 404, 409],
            )
            .await?;

        if res.status().is_success() {
            // Some runtime versions answer mutations with an empty/default List
            // instead of the updated one; re-read the list rather than trust it.
            return self.get_player_list().await;
        }

        let status = res.status().as_u16();
        let body = read_gateway_error(res).await;
        match status {
            409 => Err(GameFlowError::PlayerAlreadyConnected {
                session_id: session_id.to_string(),
            }),
            404 => Err(GameFlowError::PlayerTrackingDisabled),
            _ => {
                // 400 OUT_OF_RANGE means the list is at capacity.
                let message = body.message.unwrap_or_default();
                if body.code == Some(GRPC_OUT_OF_RANGE) || indicates_capacity(&message) {
                    Err(GameFlowError::ServerFull { capacity: None })
                } else {
                    Err(GameFlowError::RequestFailed {
                        message: if message.is_empty() {
                            format!("addValue failed with HTTP {status}")
                        } else {
                            message
                        },
                        status: Some(status),
                        source: None,
                    })
                }
            }
        }
    }

    async fn remove_player(&self, session_id: &str) -> Result<Option<PlayerListSnapshot>> {
        let path = format!("/v1beta1/lists/{PLAYERS_LIST}:removeValue");
        let res = self
            .send(
                self.client
                    .post(self.url(&path))
                    .json(&json!({ "value": session_id })),
                "removeValue",
                &[404],
            )
            .await?;
        if res.status().as_u16() == 404 {
            return Ok(None);
        }
        Ok(Some(self.get_player_list().await?))
    }

    async fn watch_game_server(&self, sink: WatchSink, cancel: CancellationToken) -> Result<()> {
        // Long-lived stream: no per-request timeout, only cancellation.
        let res = self
            .client
            .get(self.url("/watch/gameserver"))
            .send()
            .await
            .map_err(|e| self.unreachable("GET /watch/gameserver", e))?;
        if !res.status().is_success() {
            return Err(GameFlowError::RequestFailed {
                message: format!("watch failed with HTTP {}", res.status().as_u16()),
                status: Some(res.status().as_u16()),
                source: None,
            });
        }

        let mut stream = res.bytes_stream();
        let mut buffer: Vec<u8> = Vec::new();
        loop {
            tokio::select! {
                _ = cancel.cancelled() => return Ok(()),
                chunk = stream.next() => {
                    match chunk {
                        None => return Ok(()), // stream ended; caller reconnects
                        Some(Err(e)) => return Err(self.unreachable("watch stream", e)),
                        Some(Ok(bytes)) => {
                            buffer.extend_from_slice(&bytes);
                            // Lines may be split across chunks; frame by newline.
                            while let Some(pos) = buffer.iter().position(|&b| b == b'\n') {
                                let line: Vec<u8> = buffer.drain(..=pos).collect();
                                let mut line = &line[..line.len() - 1];
                                if line.last() == Some(&b'\r') {
                                    line = &line[..line.len() - 1];
                                }
                                if line.is_empty() {
                                    continue;
                                }
                                self.handle_watch_line(line, &sink);
                            }
                        }
                    }
                }
            }
        }
    }
}

impl SidecarTransport {
    fn handle_watch_line(&self, line: &[u8], sink: &WatchSink) {
        let parsed: WatchLine = match serde_json::from_slice(line) {
            Ok(parsed) => parsed,
            Err(_) => {
                self.logger.warn("watch: skipping malformed stream line");
                return;
            }
        };
        if let Some(error) = parsed.error {
            self.logger.warn(&format!(
                "watch: stream error: {}",
                error.message.unwrap_or_else(|| "unknown".to_string())
            ));
            return;
        }
        if let Some(gs) = parsed.result {
            sink(gs);
        }
    }
}

fn indicates_capacity(message: &str) -> bool {
    let lower = message.to_ascii_lowercase();
    lower.contains("capacity") || lower.contains("out of range")
}

async fn read_gateway_error(res: Response) -> GatewayError {
    res.json::<GatewayError>().await.unwrap_or_default()
}

fn boxed<E: std::error::Error + Send + Sync + 'static>(error: E) -> BoxError {
    Box::new(error)
}
