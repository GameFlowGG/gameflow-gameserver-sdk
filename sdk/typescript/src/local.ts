import {
  PlayerAlreadyConnectedError,
  PlayerTrackingDisabledError,
  ServerFullError,
} from './errors';
import type { Logger } from './log';
import type { Transport } from './transport';
import type { PlayerListSnapshot, RawGameServer } from './types';

export interface LocalTransportOptions {
  logger: Logger;
  env?: NodeJS.ProcessEnv;
}

/**
 * Simulated runtime for local development (no GameFlow involved). Lifecycle
 * calls are no-ops, player tracking runs against an in-memory list so the rest
 * of the SDK behaves exactly like in production.
 *
 * Simulation knobs via env vars:
 * - GAMEFLOW_MAX_PLAYERS: capacity of the in-memory players list. Unset means
 *   unlimited; 0 simulates a game with player tracking disabled.
 * - GAMEFLOW_PAYLOAD: simulates the launch payload.
 */
export class LocalTransport implements Transport {
  private readonly logger: Logger;
  private readonly env: NodeJS.ProcessEnv;
  private readonly capacity: number;
  private readonly players: string[] = [];
  private readonly watchers = new Set<(gs: RawGameServer) => void>();
  private state = 'Scheduled';

  constructor(options: LocalTransportOptions) {
    this.logger = options.logger;
    this.env = options.env ?? process.env;
    const raw = this.env.GAMEFLOW_MAX_PLAYERS;
    this.capacity = raw === undefined || raw === '' ? Number.POSITIVE_INFINITY : Number(raw);
  }

  private get trackingEnabled(): boolean {
    return this.capacity > 0;
  }

  async ready(): Promise<void> {
    this.state = 'Ready';
    this.logger.debug('local: ready()');
    this.emit();
  }

  async health(): Promise<void> {
    this.logger.debug('local: health ping');
  }

  async shutdown(): Promise<void> {
    this.state = 'Shutdown';
    this.logger.debug('local: shutdown()');
    this.emit();
  }

  async getGameServer(): Promise<RawGameServer> {
    return this.buildGameServer();
  }

  async getPlayerList(): Promise<PlayerListSnapshot> {
    if (!this.trackingEnabled) return { exists: false, capacity: 0, values: [] };
    return { exists: true, capacity: this.capacity, values: [...this.players] };
  }

  async addPlayer(sessionId: string): Promise<PlayerListSnapshot> {
    if (!this.trackingEnabled) throw new PlayerTrackingDisabledError();
    if (this.players.includes(sessionId)) throw new PlayerAlreadyConnectedError(sessionId);
    if (this.players.length >= this.capacity) throw new ServerFullError(this.capacity);
    this.players.push(sessionId);
    this.emit();
    return this.getPlayerList();
  }

  async removePlayer(sessionId: string): Promise<PlayerListSnapshot | null> {
    if (!this.trackingEnabled) return null;
    const index = this.players.indexOf(sessionId);
    if (index < 0) return null;
    this.players.splice(index, 1);
    this.emit();
    return this.getPlayerList();
  }

  watchGameServer(onUpdate: (gs: RawGameServer) => void, signal: AbortSignal): Promise<void> {
    onUpdate(this.buildGameServer());
    this.watchers.add(onUpdate);
    return new Promise((_, reject) => {
      signal.addEventListener(
        'abort',
        () => {
          this.watchers.delete(onUpdate);
          reject(signal.reason ?? new DOMException('watch aborted', 'AbortError'));
        },
        { once: true }
      );
    });
  }

  private emit(): void {
    const gs = this.buildGameServer();
    for (const watcher of this.watchers) watcher(gs);
  }

  private buildGameServer(): RawGameServer {
    const annotations: Record<string, string> = {};
    if (this.env.GAMEFLOW_PAYLOAD !== undefined) {
      annotations.GAMEFLOW_PAYLOAD = this.env.GAMEFLOW_PAYLOAD;
    }
    return {
      objectMeta: { name: 'local-gameserver', annotations, labels: {} },
      status: {
        state: this.state,
        address: '127.0.0.1',
        ports: [],
        ...(this.trackingEnabled
          ? {
              lists: {
                players: { capacity: String(this.capacity), values: [...this.players] },
              },
            }
          : {}),
      },
    };
  }
}
