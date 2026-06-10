import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import { PlayerTrackingDisabledError, ServerFullError } from '../src/errors';
import { silentLogger } from '../src/log';
import { Players } from '../src/players';
import { SidecarTransport } from '../src/transport';
import { FakeSidecar } from '../../../tools/conformance/fake-sidecar';

let sidecar: FakeSidecar;
let players: Players;

async function setup(sidecarInstance: FakeSidecar): Promise<Players> {
  sidecar = sidecarInstance;
  const port = await sidecar.start();
  const transport = new SidecarTransport({
    baseUrl: `http://127.0.0.1:${port}`,
    requestTimeoutMs: 1000,
    logger: silentLogger,
  });
  const instance = new Players(transport);
  instance.syncFromGameServer(await transport.getGameServer());
  return instance;
}

beforeEach(async () => {
  players = await setup(new FakeSidecar().withPlayersList(2, ['existing']));
});

afterEach(async () => {
  await sidecar.stop();
});

describe('Players', () => {
  it('seeds the cache from the game server object', () => {
    expect(players.trackingEnabled).toBe(true);
    expect(players.count()).toBe(1);
    expect(players.list()).toEqual(['existing']);
    expect(players.capacity()).toBe(2);
  });

  it('updates the cache on connect and disconnect', async () => {
    await players.connect('p1');
    expect(players.count()).toBe(2);
    expect(players.list()).toEqual(['existing', 'p1']);

    expect(await players.disconnect('existing')).toBe(true);
    expect(players.count()).toBe(1);
    expect(players.list()).toEqual(['p1']);
  });

  it('disconnect of an unknown player resolves false and keeps the cache', async () => {
    expect(await players.disconnect('ghost')).toBe(false);
    expect(players.count()).toBe(1);
  });

  it('enriches ServerFullError with the cached capacity', async () => {
    await players.connect('p1');
    const error = await players.connect('p2').catch((e: unknown) => e);
    expect(error).toBeInstanceOf(ServerFullError);
    expect((error as ServerFullError).capacity).toBe(2);
  });

  it('throws PlayerTrackingDisabledError when the game has no players list', async () => {
    await sidecar.stop();
    const disabled = await setup(new FakeSidecar()); // no players list
    expect(disabled.trackingEnabled).toBe(false);
    await expect(disabled.connect('p1')).rejects.toBeInstanceOf(PlayerTrackingDisabledError);
    await expect(disabled.disconnect('p1')).rejects.toBeInstanceOf(PlayerTrackingDisabledError);
  });

  it('refresh() re-reads the list from the runtime', async () => {
    sidecar.lists.get('players')!.values.push('outsider');
    await players.refresh();
    expect(players.list()).toEqual(['existing', 'outsider']);
  });
});
