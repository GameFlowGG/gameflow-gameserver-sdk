import { PlayerTrackingDisabledError, ServerFullError } from './errors';
import { PLAYERS_LIST, parseList, type Transport } from './transport';
import type { PlayerListSnapshot, RawGameServer } from './types';

/**
 * Player tracking over the platform's players list. Mutations always go to
 * the runtime; the returned list is kept as an authoritative cache so
 * count()/list()/capacity() are synchronous.
 */
export class Players {
  private readonly transport: Transport;
  private snapshot: PlayerListSnapshot = { exists: false, capacity: 0, values: [] };

  constructor(transport: Transport) {
    this.transport = transport;
  }

  /** Registers a player session. Call when a player joins the server. */
  async connect(sessionId: string): Promise<void> {
    try {
      this.snapshot = await this.transport.addPlayer(sessionId);
    } catch (error) {
      if (error instanceof PlayerTrackingDisabledError) {
        this.snapshot = { exists: false, capacity: 0, values: [] };
      }
      // Enrich ServerFullError with the cached capacity when the wire had none
      if (error instanceof ServerFullError && error.capacity === undefined) {
        throw new ServerFullError(this.snapshot.capacity, { cause: error });
      }
      throw error;
    }
  }

  /**
   * Unregisters a player session. Resolves false when the player was not in
   * the list (safe to call on every disconnect, including duplicates).
   */
  async disconnect(sessionId: string): Promise<boolean> {
    if (!this.snapshot.exists) throw new PlayerTrackingDisabledError();
    const updated = await this.transport.removePlayer(sessionId);
    if (updated === null) return false;
    this.snapshot = updated;
    return true;
  }

  /** Current number of connected players (from the local cache). */
  count(): number {
    return this.snapshot.values.length;
  }

  /** Session ids of connected players (from the local cache). */
  list(): string[] {
    return [...this.snapshot.values];
  }

  /** Maximum players configured for this game. */
  capacity(): number {
    return this.snapshot.capacity;
  }

  /** False when the game was created with max players = 0. */
  get trackingEnabled(): boolean {
    return this.snapshot.exists;
  }

  /** Re-reads the list from the runtime. */
  async refresh(): Promise<void> {
    this.snapshot = await this.transport.getPlayerList();
  }

  /** Internal: updates the cache from a game server object (seed and watch). */
  syncFromGameServer(gs: RawGameServer): void {
    this.snapshot = parseList(gs.status?.lists?.[PLAYERS_LIST]);
  }
}
