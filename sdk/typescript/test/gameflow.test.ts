import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import { NotConnectedError, SidecarUnavailableError } from '../src/errors';
import { GameFlow } from '../src/gameflow';
import { FakeSidecar } from '../../../tools/conformance/fake-sidecar';

const ENV_KEYS = [
  'AGONES_SDK_HTTP_PORT',
  'GAMEFLOW_SDK_MODE',
  'GAMEFLOW_DEFAULT_PORT',
  'GAMEFLOW_REGION',
  'GAMEFLOW_BUILD_ID',
  'GAMEFLOW_MAX_PLAYERS',
  'GAMEFLOW_PAYLOAD',
];

let savedEnv: Record<string, string | undefined>;
let sidecar: FakeSidecar | undefined;
let gameflow: GameFlow | undefined;

beforeEach(() => {
  savedEnv = Object.fromEntries(ENV_KEYS.map((key) => [key, process.env[key]]));
  for (const key of ENV_KEYS) delete process.env[key];
});

afterEach(async () => {
  await gameflow?.shutdown().catch(() => {});
  gameflow = undefined;
  await sidecar?.stop();
  sidecar = undefined;
  for (const [key, value] of Object.entries(savedEnv)) {
    if (value === undefined) delete process.env[key];
    else process.env[key] = value;
  }
});

async function startSidecar(configure?: (s: FakeSidecar) => void): Promise<FakeSidecar> {
  sidecar = new FakeSidecar().withPlayersList(4);
  configure?.(sidecar);
  const port = await sidecar.start();
  process.env.AGONES_SDK_HTTP_PORT = String(port);
  return sidecar;
}

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function waitFor(condition: () => boolean, timeoutMs = 3000): Promise<void> {
  const start = Date.now();
  while (!condition()) {
    if (Date.now() - start > timeoutMs) throw new Error('waitFor timed out');
    await delay(10);
  }
}

describe('GameFlow.connect', () => {
  it('uses sidecar mode when AGONES_SDK_HTTP_PORT is set', async () => {
    await startSidecar();
    gameflow = await GameFlow.connect({ logger: null });
    expect(gameflow.mode).toBe('sidecar');
    expect(gameflow.players.capacity()).toBe(4);
  });

  it('falls back to local mode when no runtime is detected', async () => {
    gameflow = await GameFlow.connect({ logger: null });
    expect(gameflow.mode).toBe('local');
    await gameflow.ready();
    await gameflow.players.connect('p1');
    expect(gameflow.players.count()).toBe(1);
  });

  it('honors GAMEFLOW_SDK_MODE over auto-detection', async () => {
    await startSidecar();
    process.env.GAMEFLOW_SDK_MODE = 'local';
    gameflow = await GameFlow.connect({ logger: null });
    expect(gameflow.mode).toBe('local');
  });

  it('retries until the runtime answers', async () => {
    const fake = await startSidecar((s) => {
      s.failNextRequests = 2;
    });
    gameflow = await GameFlow.connect({ logger: null });
    expect(fake.requestsTo('/gameserver').length).toBeGreaterThanOrEqual(3);
  });

  it('fails hard when the runtime never answers within the timeout', async () => {
    process.env.AGONES_SDK_HTTP_PORT = '1'; // nothing listens here
    await expect(
      GameFlow.connect({ logger: null, connectTimeoutMs: 300, requestTimeoutMs: 100 })
    ).rejects.toBeInstanceOf(SidecarUnavailableError);
  });
});

describe('lifecycle', () => {
  it('ready() posts /ready once and starts the heartbeat', async () => {
    const fake = await startSidecar();
    gameflow = await GameFlow.connect({ logger: null, healthIntervalMs: 500 });
    await gameflow.ready();
    await gameflow.ready(); // no-op
    expect(fake.requestsTo('/ready')).toHaveLength(1);
    await waitFor(() => fake.requestsTo('/health').length >= 2, 3000);
  });

  it('shutdown() stops the heartbeat and is idempotent', async () => {
    const fake = await startSidecar();
    gameflow = await GameFlow.connect({ logger: null, healthIntervalMs: 500 });
    await gameflow.ready();
    await gameflow.shutdown();
    await gameflow.shutdown();
    expect(fake.requestsTo('/shutdown')).toHaveLength(1);

    const healthCount = fake.requestsTo('/health').length;
    await delay(700);
    expect(fake.requestsTo('/health')).toHaveLength(healthCount);
  });

  it('rejects use after shutdown', async () => {
    await startSidecar();
    gameflow = await GameFlow.connect({ logger: null });
    await gameflow.shutdown();
    await expect(gameflow.ready()).rejects.toBeInstanceOf(NotConnectedError);
    await expect(gameflow.payload()).rejects.toBeInstanceOf(NotConnectedError);
    await expect(gameflow.players.connect('p1')).rejects.toBeInstanceOf(NotConnectedError);
  });
});

describe('payload and info', () => {
  it('reads the payload annotation', async () => {
    await startSidecar((s) => {
      s.gameServer.objectMeta!.annotations = { GAMEFLOW_PAYLOAD: '{"match":"m1"}' };
    });
    gameflow = await GameFlow.connect({ logger: null });
    expect(await gameflow.payload()).toBe('{"match":"m1"}');
  });

  it('returns undefined when no payload was provided', async () => {
    await startSidecar();
    gameflow = await GameFlow.connect({ logger: null });
    expect(await gameflow.payload()).toBeUndefined();
  });

  it('maps the game server object to GameServerInfo', async () => {
    await startSidecar();
    gameflow = await GameFlow.connect({ logger: null });
    const info = await gameflow.info();
    expect(info.name).toBe('gs-test');
    expect(info.address).toBe('10.0.0.1');
    expect(info.ports).toEqual([{ name: 'default', port: 7777 }]);
  });

  it('exposes env-derived ports, region and build id', async () => {
    process.env.GAMEFLOW_DEFAULT_PORT = '7777';
    process.env.GAMEFLOW_REGION = 'eu-west-1';
    process.env.GAMEFLOW_BUILD_ID = 'b-7';
    gameflow = await GameFlow.connect({ logger: null });
    expect(gameflow.ports.default).toBe(7777);
    expect(gameflow.region).toBe('eu-west-1');
    expect(gameflow.buildId).toBe('b-7');
  });
});

describe('watch', () => {
  it('delivers server updates and syncs the players cache', async () => {
    const fake = await startSidecar();
    gameflow = await GameFlow.connect({ logger: null });

    const states: string[] = [];
    const unsubscribe = gameflow.watch((info) => states.push(info.state));
    await waitFor(() => states.length >= 1);

    fake.lists.get('players')!.values.push('via-watch');
    fake.pushWatchUpdate();
    await waitFor(() => gameflow!.players.count() === 1);
    expect(gameflow.players.list()).toEqual(['via-watch']);
    unsubscribe();
  });

  it('fires onPayloadChange when the payload annotation changes', async () => {
    const fake = await startSidecar();
    gameflow = await GameFlow.connect({ logger: null });

    const payloads: (string | undefined)[] = [];
    gameflow.onPayloadChange((payload) => payloads.push(payload));
    await delay(50); // let the stream open and deliver the initial (unchanged) state

    fake.gameServer.objectMeta!.annotations = { GAMEFLOW_PAYLOAD: 'assigned' };
    fake.pushWatchUpdate();
    await waitFor(() => payloads.length >= 1);
    expect(payloads).toEqual(['assigned']);
  });
});
