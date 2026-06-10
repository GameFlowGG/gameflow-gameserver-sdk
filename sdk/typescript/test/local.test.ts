import { describe, expect, it } from 'vitest';

import {
  PlayerAlreadyConnectedError,
  PlayerTrackingDisabledError,
  ServerFullError,
} from '../src/errors';
import { LocalTransport } from '../src/local';
import { silentLogger } from '../src/log';
import type { RawGameServer } from '../src/types';

function local(env: NodeJS.ProcessEnv = {}): LocalTransport {
  return new LocalTransport({ logger: silentLogger, env });
}

describe('LocalTransport', () => {
  it('tracks players in memory', async () => {
    const transport = local();
    await transport.addPlayer('p1');
    await transport.addPlayer('p2');
    expect((await transport.getPlayerList()).values).toEqual(['p1', 'p2']);
    expect(await transport.removePlayer('p1')).toMatchObject({ values: ['p2'] });
    expect(await transport.removePlayer('ghost')).toBeNull();
  });

  it('throws on duplicate players', async () => {
    const transport = local();
    await transport.addPlayer('p1');
    await expect(transport.addPlayer('p1')).rejects.toBeInstanceOf(PlayerAlreadyConnectedError);
  });

  it('enforces capacity from GAMEFLOW_MAX_PLAYERS', async () => {
    const transport = local({ GAMEFLOW_MAX_PLAYERS: '1' });
    await transport.addPlayer('p1');
    await expect(transport.addPlayer('p2')).rejects.toBeInstanceOf(ServerFullError);
  });

  it('simulates tracking disabled when GAMEFLOW_MAX_PLAYERS=0', async () => {
    const transport = local({ GAMEFLOW_MAX_PLAYERS: '0' });
    await expect(transport.addPlayer('p1')).rejects.toBeInstanceOf(PlayerTrackingDisabledError);
    expect((await transport.getPlayerList()).exists).toBe(false);
  });

  it('reads the payload from GAMEFLOW_PAYLOAD', async () => {
    const transport = local({ GAMEFLOW_PAYLOAD: '{"match":"m1"}' });
    const gs = await transport.getGameServer();
    expect(gs.objectMeta?.annotations?.GAMEFLOW_PAYLOAD).toBe('{"match":"m1"}');
  });

  it('reflects lifecycle state transitions', async () => {
    const transport = local();
    expect((await transport.getGameServer()).status?.state).toBe('Scheduled');
    await transport.ready();
    expect((await transport.getGameServer()).status?.state).toBe('Ready');
    await transport.shutdown();
    expect((await transport.getGameServer()).status?.state).toBe('Shutdown');
  });

  it('notifies watchers on mutations and stops on abort', async () => {
    const transport = local();
    const seen: RawGameServer[] = [];
    const abort = new AbortController();
    const done = transport.watchGameServer((gs) => seen.push(gs), abort.signal);

    expect(seen).toHaveLength(1); // initial snapshot
    await transport.ready();
    await transport.addPlayer('p1');
    expect(seen).toHaveLength(3);
    expect(seen[2]!.status?.lists?.players?.values).toEqual(['p1']);

    abort.abort();
    await expect(done).rejects.toMatchObject({ name: 'AbortError' });
    await transport.addPlayer('p2');
    expect(seen).toHaveLength(3); // no updates after abort
  });
});
