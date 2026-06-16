//! Conformance suite: drives the full SDK against the shared fake-runtime
//! fixture (`tools/conformance/serve.mjs`), the same fixture every GameFlow SDK
//! is tested against. Requires Node on PATH; skips cleanly when it is absent.

use std::io::{BufRead, BufReader};
use std::process::{Child, Command, Stdio};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use gameflow::{ConnectOptions, ErrorCode, GameFlow, ModeOption};

/// Owns the fixture process and tears it down on drop.
struct Fixture {
    child: Child,
    port: u16,
    control_port: u16,
}

impl Drop for Fixture {
    fn drop(&mut self) {
        let _ = self.child.kill();
        let _ = self.child.wait();
    }
}

fn node_available() -> bool {
    Command::new("node")
        .arg("--version")
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
}

fn start_fixture(args: &[&str]) -> Fixture {
    let serve = format!(
        "{}/../../../tools/conformance/serve.mjs",
        env!("CARGO_MANIFEST_DIR")
    );
    let mut child = Command::new("node")
        .arg(serve)
        .args(args)
        .stdout(Stdio::piped())
        .stderr(Stdio::inherit())
        .spawn()
        .expect("spawn fake sidecar");

    let stdout = child.stdout.take().expect("fixture stdout");
    let mut reader = BufReader::new(stdout);
    let mut port = None;
    let mut control_port = None;
    let mut line = String::new();
    while port.is_none() || control_port.is_none() {
        line.clear();
        let read = reader.read_line(&mut line).expect("read fixture stdout");
        assert!(read != 0, "fixture exited before announcing its ports");
        if let Some(value) = line.trim().strip_prefix("PORT=") {
            port = Some(value.parse().expect("port"));
        } else if let Some(value) = line.trim().strip_prefix("CONTROL_PORT=") {
            control_port = Some(value.parse().expect("control port"));
        }
    }
    Fixture {
        child,
        port: port.unwrap(),
        control_port: control_port.unwrap(),
    }
}

async fn control_post(control_port: u16, path: &str) {
    reqwest::Client::new()
        .post(format!("http://127.0.0.1:{control_port}{path}"))
        .send()
        .await
        .expect("control request");
}

async fn count_requests(control_port: u16, path: &str) -> usize {
    let body: Vec<serde_json::Value> = reqwest::Client::new()
        .get(format!("http://127.0.0.1:{control_port}/requests"))
        .send()
        .await
        .expect("control /requests")
        .json()
        .await
        .expect("requests json");
    body.iter()
        .filter(|r| r.get("path").and_then(|p| p.as_str()) == Some(path))
        .count()
}

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn conformance_against_fake_runtime() {
    if !node_available() {
        eprintln!("skipping conformance suite: Node not found on PATH");
        return;
    }

    // capacity 2, one seeded player, the first two requests fail (connect must retry).
    let fixture = start_fixture(&[
        "--players-capacity=2",
        "--players-seed=seeded",
        "--fail-first=2",
    ]);
    let port = fixture.port;
    let control_port = fixture.control_port;

    // Point auto-detection at the fixture. This is the only test in this binary,
    // so mutating the process environment here is race-free.
    std::env::set_var("AGONES_SDK_HTTP_PORT", port.to_string());
    std::env::remove_var("GAMEFLOW_SDK_MODE");

    let options = ConnectOptions::new()
        .mode(ModeOption::Auto)
        .health_interval(Duration::from_millis(500))
        .silent();
    let gf = GameFlow::connect_with(options)
        .await
        .expect("connect (with retries)");
    assert_eq!(gf.mode(), gameflow::Mode::Sidecar);

    // The successful probe seeds the players cache.
    assert!(gf.players().tracking_enabled());
    assert_eq!(gf.players().capacity(), 2);
    assert_eq!(gf.players().count(), 1);
    assert_eq!(gf.players().list(), vec!["seeded"]);

    gf.ready().await.expect("ready");

    // Watch + payload-change subscriptions.
    let updates = Arc::new(Mutex::new(0usize));
    let watch_sub = {
        let updates = updates.clone();
        gf.watch(move |_info| *updates.lock().unwrap() += 1)
            .expect("watch")
    };
    let payloads = Arc::new(Mutex::new(Vec::<Option<String>>::new()));
    let payload_sub = {
        let payloads = payloads.clone();
        gf.on_payload_change(move |p| payloads.lock().unwrap().push(p))
            .expect("on_payload_change")
    };

    // Player tracking against the runtime.
    gf.players().connect("p1").await.expect("connect p1");
    assert_eq!(gf.players().count(), 2);

    let full = gf.players().connect("p2").await.unwrap_err();
    assert_eq!(full.code(), ErrorCode::ServerFull);
    match full {
        gameflow::GameFlowError::ServerFull { capacity } => assert_eq!(capacity, Some(2)),
        other => panic!("expected ServerFull, got {other:?}"),
    }

    let dup = gf.players().connect("p1").await.unwrap_err();
    assert_eq!(dup.code(), ErrorCode::PlayerAlreadyConnected);

    assert!(gf
        .players()
        .disconnect("seeded")
        .await
        .expect("disconnect seeded"));
    assert!(!gf
        .players()
        .disconnect("does-not-exist")
        .await
        .expect("idempotent disconnect"));
    assert_eq!(gf.players().count(), 1);

    // Watch fires on a pushed update.
    control_post(control_port, "/push-update").await;
    tokio::time::sleep(Duration::from_millis(100)).await;
    assert!(
        *updates.lock().unwrap() >= 1,
        "watch listener should have fired"
    );

    // Payload change propagates through the watch stream.
    control_post(control_port, "/set-payload?value=match-7").await;
    tokio::time::sleep(Duration::from_millis(100)).await;
    assert_eq!(
        gf.payload().await.expect("payload").as_deref(),
        Some("match-7")
    );
    assert!(
        payloads
            .lock()
            .unwrap()
            .iter()
            .any(|p| p.as_deref() == Some("match-7")),
        "payload-change listener should have fired"
    );

    // Health heartbeat ticks at least twice.
    tokio::time::sleep(Duration::from_millis(1400)).await;

    drop(watch_sub);
    drop(payload_sub);

    gf.shutdown().await.expect("shutdown");
    assert_eq!(
        gf.players().connect("late").await.unwrap_err().code(),
        ErrorCode::NotConnected,
        "calls after shutdown fail with NOT_CONNECTED",
    );

    // Assert behavior only observable from the runtime side.
    assert_eq!(
        count_requests(control_port, "/ready").await,
        1,
        "/ready posted exactly once"
    );
    assert!(
        count_requests(control_port, "/health").await >= 2,
        "health heartbeat ticked"
    );
    assert_eq!(
        count_requests(control_port, "/shutdown").await,
        1,
        "/shutdown posted exactly once"
    );
    assert!(
        count_requests(control_port, "/gameserver").await >= 3,
        "connect retried through the seeded failures",
    );
}
