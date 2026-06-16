//! Headless GameFlow dedicated server built on Bevy.
//!
//! Runs `MinimalPlugins` (no rendering) with the `GameFlowPlugin` from the
//! adjacent [`gameflow_plugin`] module — a copy-paste Bevy bridge over the
//! engine-agnostic `gameflow` SDK. It connects in the background, is marked
//! ready automatically, simulates a few players joining, then shuts down
//! cleanly — demonstrating the full lifecycle through ECS resources, messages
//! and events.
//!
//! ```bash
//! cargo run -p gameflow-example-bevy-server
//! ```

mod gameflow_plugin;

use std::time::Duration;

use bevy::app::{AppExit, ScheduleRunnerPlugin};
use bevy::log::LogPlugin;
use bevy::prelude::*;

use gameflow_plugin::{
    GameFlowClient, GameFlowConnected, GameFlowHealthDegraded, GameFlowPlugin, GameFlowReady,
    GameFlowShutdown, PayloadChanged, PlayerConnectFailed, PlayerConnected, ServerInfoChanged,
};

#[derive(Resource)]
struct Demo {
    spawn: Timer,
    shutdown: Timer,
    next: u32,
    requested_shutdown: bool,
}

fn main() {
    App::new()
        .add_plugins(
            MinimalPlugins.set(ScheduleRunnerPlugin::run_loop(Duration::from_secs_f64(
                1.0 / 10.0,
            ))),
        )
        .add_plugins(LogPlugin::default())
        .add_plugins(GameFlowPlugin::default())
        .insert_resource(Demo {
            spawn: Timer::new(Duration::from_secs(2), TimerMode::Repeating),
            shutdown: Timer::new(Duration::from_secs(9), TimerMode::Once),
            next: 1,
            requested_shutdown: false,
        })
        .add_observer(on_connected)
        .add_observer(on_ready)
        .add_observer(on_health_degraded)
        .add_observer(on_shutdown)
        .add_systems(Update, (simulate_players, report_messages))
        .run();
}

fn on_connected(_: On<GameFlowConnected>, client: Option<Res<GameFlowClient>>) {
    if let Some(client) = client {
        info!(
            "GameFlow connected (mode {:?}); default port {:?}",
            client.mode(),
            client.default_port(),
        );
    }
}

fn on_ready(_: On<GameFlowReady>) {
    info!("server marked ready; health heartbeat running");
}

fn on_health_degraded(_: On<GameFlowHealthDegraded>) {
    warn!("health pings have been failing for a sustained period");
}

fn on_shutdown(_: On<GameFlowShutdown>, mut exit: MessageWriter<AppExit>) {
    info!("server shut down; exiting the app");
    exit.write(AppExit::Success);
}

fn simulate_players(time: Res<Time>, mut demo: ResMut<Demo>, client: Option<Res<GameFlowClient>>) {
    let Some(client) = client else {
        return; // not connected yet
    };

    demo.spawn.tick(time.delta());
    if demo.spawn.just_finished() && client.player_count() < 3 {
        let id = format!("bot-{}", demo.next);
        demo.next += 1;
        info!("simulating a player joining: {id}");
        client.connect_player(id);
    }

    demo.shutdown.tick(time.delta());
    if demo.shutdown.just_finished() && !demo.requested_shutdown {
        demo.requested_shutdown = true;
        info!("demo finished; requesting shutdown");
        client.shutdown();
    }
}

fn report_messages(
    mut connected: MessageReader<PlayerConnected>,
    mut failed: MessageReader<PlayerConnectFailed>,
    mut info_changed: MessageReader<ServerInfoChanged>,
    mut payload: MessageReader<PayloadChanged>,
    client: Option<Res<GameFlowClient>>,
) {
    let count = client.as_ref().map(|c| c.player_count()).unwrap_or(0);
    for ev in connected.read() {
        info!("player joined: {} (now {count} connected)", ev.session_id);
    }
    for ev in failed.read() {
        warn!(
            "player join failed: {} ({:?}: {})",
            ev.session_id, ev.code, ev.message
        );
    }
    for ev in info_changed.read() {
        info!("server info update: state={}", ev.info.state);
    }
    for ev in payload.read() {
        info!("launch payload changed: {:?}", ev.payload);
    }
}
