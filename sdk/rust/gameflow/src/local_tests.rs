//! Local-mode behavior tests.
//!
//! These drive the full `GameFlow` handle against the in-memory transport with
//! an injected environment, so they exercise real SDK behavior without touching
//! the process environment or the network.

use std::sync::{Arc, Mutex};
use std::time::Duration;

use crate::env::Env;
use crate::error::ErrorCode;
use crate::gameflow::{ConnectOptions, GameFlow, Mode, ModeOption};

fn local_options() -> ConnectOptions {
    ConnectOptions::new().mode(ModeOption::Local).silent()
}

async fn connect(env: &[(&str, &str)]) -> GameFlow {
    GameFlow::connect_with_env(local_options(), Env::from_pairs(env))
        .await
        .unwrap()
}

#[tokio::test]
async fn connects_in_local_mode_with_unlimited_tracking() {
    let gf = connect(&[]).await;
    assert_eq!(gf.mode(), Mode::Local);
    assert!(gf.players().tracking_enabled());
    assert_eq!(gf.players().capacity(), -1); // unlimited
    assert_eq!(gf.players().count(), 0);

    // ready() is a no-op in local mode but must succeed.
    gf.ready().await.unwrap();
}

#[tokio::test]
async fn tracks_players_against_the_in_memory_list() {
    let gf = connect(&[]).await;

    gf.players().connect("a").await.unwrap();
    gf.players().connect("b").await.unwrap();
    gf.players().connect("c").await.unwrap();
    assert_eq!(gf.players().count(), 3);
    assert_eq!(gf.players().list(), vec!["a", "b", "c"]);

    // duplicate id
    let err = gf.players().connect("b").await.unwrap_err();
    assert_eq!(err.code(), ErrorCode::PlayerAlreadyConnected);

    // idempotent disconnect
    assert!(gf.players().disconnect("a").await.unwrap());
    assert!(!gf.players().disconnect("a").await.unwrap());
    assert_eq!(gf.players().count(), 2);
}

#[tokio::test]
async fn enforces_capacity() {
    let gf = connect(&[("GAMEFLOW_MAX_PLAYERS", "2")]).await;
    assert_eq!(gf.players().capacity(), 2);

    gf.players().connect("a").await.unwrap();
    gf.players().connect("b").await.unwrap();

    let err = gf.players().connect("c").await.unwrap_err();
    assert_eq!(err.code(), ErrorCode::ServerFull);
    match err {
        crate::GameFlowError::ServerFull { capacity } => assert_eq!(capacity, Some(2)),
        other => panic!("expected ServerFull, got {other:?}"),
    }
}

#[tokio::test]
async fn tracking_disabled_when_max_players_is_zero() {
    let gf = connect(&[("GAMEFLOW_MAX_PLAYERS", "0")]).await;
    assert!(!gf.players().tracking_enabled());
    assert_eq!(gf.players().capacity(), 0);

    let connect_err = gf.players().connect("a").await.unwrap_err();
    assert_eq!(connect_err.code(), ErrorCode::PlayerTrackingDisabled);

    let disconnect_err = gf.players().disconnect("a").await.unwrap_err();
    assert_eq!(disconnect_err.code(), ErrorCode::PlayerTrackingDisabled);
}

#[tokio::test]
async fn malformed_max_players_disables_tracking() {
    // Negative or unparseable values disable tracking, matching the TS reference.
    for raw in ["-5", "abc"] {
        let gf = connect(&[("GAMEFLOW_MAX_PLAYERS", raw)]).await;
        assert!(
            !gf.players().tracking_enabled(),
            "max_players={raw:?} should disable tracking"
        );
        assert_eq!(
            gf.players().connect("a").await.unwrap_err().code(),
            ErrorCode::PlayerTrackingDisabled,
        );
    }
}

#[tokio::test]
async fn exposes_the_launch_payload() {
    let gf = connect(&[("GAMEFLOW_PAYLOAD", "{\"match\":\"test-1\"}")]).await;
    assert_eq!(
        gf.payload().await.unwrap().as_deref(),
        Some("{\"match\":\"test-1\"}")
    );

    let gf_none = connect(&[]).await;
    assert_eq!(gf_none.payload().await.unwrap(), None);
}

#[tokio::test]
async fn exposes_server_info() {
    let gf = connect(&[]).await;
    let info = gf.info().await.unwrap();
    assert_eq!(info.name, "local-gameserver");
    assert_eq!(info.address, "127.0.0.1");
}

#[tokio::test]
async fn reads_ports_and_metadata_from_env() {
    let gf = connect(&[
        ("GAMEFLOW_DEFAULT_PORT", "7777"),
        ("GAMEFLOW_REGION", "eu-west-1"),
        ("GAMEFLOW_BUILD_ID", "build-42"),
    ])
    .await;
    assert_eq!(gf.ports().default(), Some(7777));
    assert_eq!(gf.region().as_deref(), Some("eu-west-1"));
    assert_eq!(gf.build_id().as_deref(), Some("build-42"));
}

#[tokio::test]
async fn watch_emits_synthetic_updates() {
    let gf = connect(&[]).await;
    let states = Arc::new(Mutex::new(Vec::<String>::new()));
    let sub = {
        let states = states.clone();
        gf.watch(move |info| states.lock().unwrap().push(info.state))
            .unwrap()
    };

    // Let the lazily-opened stream deliver the initial state, then mutate.
    tokio::time::sleep(Duration::from_millis(50)).await;
    gf.ready().await.unwrap();
    tokio::time::sleep(Duration::from_millis(50)).await;

    let seen = states.lock().unwrap().clone();
    assert!(
        seen.iter().any(|s| s == "Ready"),
        "expected a Ready update, saw {seen:?}"
    );
    drop(sub);
}

#[tokio::test]
async fn shutdown_is_idempotent_and_disables_further_calls() {
    let gf = connect(&[]).await;
    gf.ready().await.unwrap();

    gf.shutdown().await.unwrap();
    gf.shutdown().await.unwrap(); // idempotent

    assert_eq!(
        gf.ready().await.unwrap_err().code(),
        ErrorCode::NotConnected
    );
    assert_eq!(gf.info().await.unwrap_err().code(), ErrorCode::NotConnected);
    assert_eq!(
        gf.players().connect("a").await.unwrap_err().code(),
        ErrorCode::NotConnected
    );
}

#[tokio::test]
async fn explicit_mode_option_overrides_env() {
    // AGONES port is set, but the explicit Local option must win.
    let gf = GameFlow::connect_with_env(
        local_options(),
        Env::from_pairs(&[("AGONES_SDK_HTTP_PORT", "9999")]),
    )
    .await
    .unwrap();
    assert_eq!(gf.mode(), Mode::Local);
}
