import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import {
  PlayerAlreadyConnectedError,
  PlayerTrackingDisabledError,
  ServerFullError,
  SidecarUnavailableError,
} from '../src/errors';
import { silentLogger } from '../src/log';
import { SidecarTransport, parseList } from '../src/transport';
import type { RawGameServer } from '../src/types';
import { FakeSidecar } from './fixtures/fake-sidecar';

let sidecar: FakeSidecar;
let baseUrl: string;
let transport: SidecarTransport;

beforeEach(async () => {
  sidecar = new FakeSidecar().withPlayersList(2);
  const port = await sidecar.start();
  baseUrl = `http://127.0.0.1:${port}`;
  transport = new SidecarTransport({ baseUrl, requestTimeoutMs: 1000, logger: silentLogger });
});

afterEach(async () => {
  await sidecar.stop();
});

describe('parseList', () => {
  it('coerces int64 string capacity to number', () => {
    expect(parseList({ capacity: '16', values: ['a'] })).toEqual({
      exists: true,
      capacity: 16,
      values: ['a'],
    });
  });

  it('returns a non-existing snapshot for undefined', () => {
    expect(parseList(undefined)).toEqual({ exists: false, capacity: 0, values: [] });
  });
});

describe('lifecycle requests', () => {
  it('posts /ready, /health and /shutdown with empty JSON bodies', async () => {
    await transport.ready();
    await transport.health();
    await transport.shutdown();
    for (const path of ['/ready', '/health', '/shutdown']) {
      const requests = sidecar.requestsTo(path);
      expect(requests).toHaveLength(1);
      expect(requests[0]!.method).toBe('POST');
      expect(requests[0]!.body).toBe('{}');
    }
  });

  it('fetches the game server object', async () => {
    const gs = await transport.getGameServer();
    expect(gs.objectMeta?.name).toBe('gs-test');
    expect(gs.status?.lists?.players?.capacity).toBe('2');
  });

  // Regression: the real sidecar marshals proto names (object_meta). The SDK
  // read objectMeta only, so payload annotations were invisible in production.
  it('normalizes proto-named object_meta to objectMeta', async () => {
    sidecar.gameServer.objectMeta!.annotations = { GAMEFLOW_PAYLOAD: 'match-1' };
    const gs = await transport.getGameServer();
    expect(gs.objectMeta?.annotations?.GAMEFLOW_PAYLOAD).toBe('match-1');
  });

  it('wraps connection refusal in SidecarUnavailableError', async () => {
    await sidecar.stop();
    await expect(transport.ready()).rejects.toBeInstanceOf(SidecarUnavailableError);
  });

  it('wraps request timeouts in SidecarUnavailableError', async () => {
    sidecar.delayMs = 300;
    const slow = new SidecarTransport({ baseUrl, requestTimeoutMs: 50, logger: silentLogger });
    await expect(slow.ready()).rejects.toBeInstanceOf(SidecarUnavailableError);
  });
});

describe('player list operations', () => {
  it('adds players and returns the updated list', async () => {
    const list = await transport.addPlayer('p1');
    expect(list).toEqual({ exists: true, capacity: 2, values: ['p1'] });
  });

  it('uses the custom-verb path with a literal colon', async () => {
    await transport.addPlayer('p1');
    expect(sidecar.requests.at(-1)!.path).toBe('/v1beta1/lists/players:addValue');
  });

  it('throws PlayerAlreadyConnectedError on duplicates', async () => {
    await transport.addPlayer('p1');
    const error = await transport.addPlayer('p1').catch((e: unknown) => e);
    expect(error).toBeInstanceOf(PlayerAlreadyConnectedError);
    expect((error as PlayerAlreadyConnectedError).sessionId).toBe('p1');
    expect((error as PlayerAlreadyConnectedError).code).toBe('PLAYER_ALREADY_CONNECTED');
  });

  it('throws ServerFullError at capacity', async () => {
    await transport.addPlayer('p1');
    await transport.addPlayer('p2');
    await expect(transport.addPlayer('p3')).rejects.toBeInstanceOf(ServerFullError);
  });

  it('throws PlayerTrackingDisabledError when the list does not exist', async () => {
    sidecar.lists.clear();
    await expect(transport.addPlayer('p1')).rejects.toBeInstanceOf(PlayerTrackingDisabledError);
  });

  it('returns null when removing a player that is not in the list', async () => {
    expect(await transport.removePlayer('ghost')).toBeNull();
  });

  it('removes players and returns the updated list', async () => {
    await transport.addPlayer('p1');
    await transport.addPlayer('p2');
    const list = await transport.removePlayer('p1');
    expect(list).toEqual({ exists: true, capacity: 2, values: ['p2'] });
  });

  it('reports a non-existing list on getPlayerList', async () => {
    sidecar.lists.clear();
    expect(await transport.getPlayerList()).toEqual({ exists: false, capacity: 0, values: [] });
  });
});

describe('watchGameServer', () => {
  it('delivers updates and handles lines split across chunks', async () => {
    const seen: RawGameServer[] = [];
    const abort = new AbortController();
    const done = transport.watchGameServer((gs) => seen.push(gs), abort.signal);

    await waitFor(() => seen.length >= 1); // initial state line
    const update = JSON.stringify({ result: { objectMeta: { name: 'gs-split' } } }) + '\n';
    sidecar.pushWatchRaw(update.slice(0, 10));
    await delay(20);
    sidecar.pushWatchRaw(update.slice(10));
    await waitFor(() => seen.length >= 2);
    expect(seen[1]!.objectMeta?.name).toBe('gs-split');

    sidecar.closeWatchStreams();
    await done;
  });

  it('normalizes proto-named object_meta on watch updates', async () => {
    const seen: RawGameServer[] = [];
    const abort = new AbortController();
    const done = transport.watchGameServer((gs) => seen.push(gs), abort.signal);

    await waitFor(() => seen.length >= 1);
    sidecar.pushWatchRaw(
      JSON.stringify({
        result: { object_meta: { name: 'gs-late', annotations: { GAMEFLOW_PAYLOAD: 'm2' } } },
      }) + '\n'
    );
    await waitFor(() => seen.length >= 2);
    expect(seen[1]!.objectMeta?.annotations?.GAMEFLOW_PAYLOAD).toBe('m2');

    sidecar.closeWatchStreams();
    await done;
  });

  it('skips error lines without dying', async () => {
    const seen: RawGameServer[] = [];
    const abort = new AbortController();
    const done = transport.watchGameServer((gs) => seen.push(gs), abort.signal);

    await waitFor(() => seen.length >= 1);
    sidecar.pushWatchRaw(JSON.stringify({ error: { code: 13, message: 'boom' } }) + '\n');
    sidecar.pushWatchUpdate();
    await waitFor(() => seen.length >= 2);

    sidecar.closeWatchStreams();
    await done;
  });

  it('stops when aborted', async () => {
    const abort = new AbortController();
    const done = transport.watchGameServer(() => {}, abort.signal);
    await delay(20);
    abort.abort();
    await expect(done).rejects.toMatchObject({ name: 'AbortError' });
  });
});

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function waitFor(condition: () => boolean, timeoutMs = 2000): Promise<void> {
  const start = Date.now();
  while (!condition()) {
    if (Date.now() - start > timeoutMs) throw new Error('waitFor timed out');
    await delay(10);
  }
}
