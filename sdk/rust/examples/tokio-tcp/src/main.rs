//! Minimal GameFlow game server: a TCP line-chat built on `tokio::net` and the
//! GameFlow SDK. The Rust twin of `examples/node-tcp`. Connect with
//! `nc <host> <port>` and type lines. Commands: `/who` lists players, `/quit`
//! disconnects.

use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;

use gameflow::{ErrorCode, GameFlow};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::{TcpListener, TcpStream};
use tokio::sync::broadcast;

static NEXT_SESSION: AtomicU64 = AtomicU64::new(1);

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    tracing_subscriber::fmt()
        .with_max_level(tracing::Level::INFO)
        .init();

    let gf = GameFlow::connect().await?;
    println!(
        "[server] mode={} region={} build={}",
        gf.mode(),
        gf.region().as_deref().unwrap_or("-"),
        gf.build_id().as_deref().unwrap_or("-"),
    );

    if let Some(payload) = gf.payload().await? {
        println!("[server] launch payload: {payload}");
    }
    // Keep the subscription alive for the whole process.
    let _payload_sub = gf.on_payload_change(|payload| {
        println!(
            "[server] payload changed: {}",
            payload.as_deref().unwrap_or("(cleared)")
        );
    })?;

    let port = gf.ports().default().unwrap_or(7777);
    let listener = TcpListener::bind(("127.0.0.1", port)).await?;

    // Only call ready() once the server can actually accept connections.
    gf.ready().await?;
    println!("[server] listening on :{port}");

    let (chat, _) = broadcast::channel::<String>(128);

    tokio::select! {
        result = accept_loop(&listener, gf.clone(), chat.clone()) => {
            result?;
        }
        signal = shutdown_signal() => {
            println!("[server] {signal} received, shutting down");
        }
    }

    // The platform sends SIGTERM and force-kills the container ~45s later:
    // drain and call shutdown() well within that budget.
    gf.shutdown().await?;
    Ok(())
}

/// Resolves on the first OS shutdown signal. The platform stops servers with
/// SIGTERM; locally you'll usually hit Ctrl-C (SIGINT). Both must lead to a
/// graceful `shutdown()`.
async fn shutdown_signal() -> &'static str {
    #[cfg(unix)]
    {
        use tokio::signal::unix::{signal, SignalKind};
        let mut term = signal(SignalKind::terminate()).expect("install SIGTERM handler");
        let mut int = signal(SignalKind::interrupt()).expect("install SIGINT handler");
        tokio::select! {
            _ = term.recv() => "SIGTERM",
            _ = int.recv() => "SIGINT",
        }
    }
    #[cfg(not(unix))]
    {
        tokio::signal::ctrl_c()
            .await
            .expect("install Ctrl-C handler");
        "Ctrl-C"
    }
}

async fn accept_loop(
    listener: &TcpListener,
    gf: GameFlow,
    chat: broadcast::Sender<String>,
) -> Result<(), Box<dyn std::error::Error>> {
    loop {
        let (socket, _addr) = listener.accept().await?;
        tokio::spawn(handle_connection(socket, gf.clone(), chat.clone()));
    }
}

async fn handle_connection(socket: TcpStream, gf: GameFlow, chat: broadcast::Sender<String>) {
    let session_id = Arc::new(format!(
        "session-{}",
        NEXT_SESSION.fetch_add(1, Ordering::Relaxed)
    ));

    if let Err(error) = gf.players().connect(session_id.as_str()).await {
        let mut socket = socket;
        let message = if error.code() == ErrorCode::ServerFull {
            "server full\n"
        } else {
            "cannot join\n"
        };
        let _ = socket.write_all(message.as_bytes()).await;
        return;
    }

    let (reader, mut writer) = socket.into_split();
    let mut lines = BufReader::new(reader).lines();
    let mut feed = chat.subscribe();

    let _ = writer
        .write_all(
            format!(
                "welcome {} ({}/{} players)\n",
                session_id,
                gf.players().count(),
                gf.players().capacity(),
            )
            .as_bytes(),
        )
        .await;

    loop {
        tokio::select! {
            line = lines.next_line() => {
                let Ok(Some(line)) = line else { break };
                let line = line.trim();
                if line == "/quit" {
                    let _ = writer.write_all(b"bye\n").await;
                    break;
                }
                if line == "/who" {
                    let _ = writer
                        .write_all(format!("players: {}\n", gf.players().list().join(", ")).as_bytes())
                        .await;
                    continue;
                }
                let _ = chat.send(format!("{session_id}: {line}"));
            }
            msg = feed.recv() => {
                match msg {
                    Ok(msg) => {
                        // Don't echo a player's own lines back to them.
                        if !msg.starts_with(&format!("{session_id}: ")) {
                            let _ = writer.write_all(format!("{msg}\n").as_bytes()).await;
                        }
                    }
                    Err(broadcast::error::RecvError::Lagged(_)) => continue,
                    Err(broadcast::error::RecvError::Closed) => break,
                }
            }
        }
    }

    if let Err(error) = gf.players().disconnect(session_id.as_str()).await {
        eprintln!("[server] disconnect failed: {error}");
    }
}
