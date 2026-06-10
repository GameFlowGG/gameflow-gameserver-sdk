import {
  PlayerAlreadyConnectedError,
  PlayerTrackingDisabledError,
  RequestFailedError,
  ServerFullError,
  SidecarUnavailableError,
} from './errors';
import type { Logger } from './log';
import { normalizeGameServer } from './types';
import type { PlayerListSnapshot, RawGameServer, RawList } from './types';

export const PLAYERS_LIST = 'players';

/**
 * The seam between the real GameFlow runtime and local mode. Everything above
 * this interface (GameFlow, Players, HealthLoop) is mode-agnostic.
 */
export interface Transport {
  ready(): Promise<void>;
  health(): Promise<void>;
  shutdown(): Promise<void>;
  getGameServer(): Promise<RawGameServer>;
  getPlayerList(): Promise<PlayerListSnapshot>;
  addPlayer(sessionId: string): Promise<PlayerListSnapshot>;
  /** Resolves null when the player was not in the list (idempotent remove). */
  removePlayer(sessionId: string): Promise<PlayerListSnapshot | null>;
  /** Resolves when the stream ends; rejects on transport failure. */
  watchGameServer(onUpdate: (gs: RawGameServer) => void, signal: AbortSignal): Promise<void>;
}

/** int64 fields arrive as JSON strings from the runtime; coerce defensively. */
export function parseList(raw: RawList | undefined): PlayerListSnapshot {
  if (!raw) return { exists: false, capacity: 0, values: [] };
  return { exists: true, capacity: Number(raw.capacity ?? 0), values: raw.values ?? [] };
}

interface GatewayError {
  code?: number;
  message?: string;
}

const GRPC_OUT_OF_RANGE = 11;

export interface SidecarTransportOptions {
  baseUrl: string;
  requestTimeoutMs: number;
  logger: Logger;
}

export class SidecarTransport implements Transport {
  private readonly baseUrl: string;
  private readonly requestTimeoutMs: number;
  private readonly logger: Logger;

  constructor(options: SidecarTransportOptions) {
    this.baseUrl = options.baseUrl;
    this.requestTimeoutMs = options.requestTimeoutMs;
    this.logger = options.logger;
  }

  async ready(): Promise<void> {
    await this.request('POST', '/ready', {});
  }

  async health(): Promise<void> {
    await this.request('POST', '/health', {});
  }

  async shutdown(): Promise<void> {
    await this.request('POST', '/shutdown', {});
  }

  async getGameServer(): Promise<RawGameServer> {
    const res = await this.request('GET', '/gameserver');
    return normalizeGameServer((await res.json()) as RawGameServer);
  }

  async getPlayerList(): Promise<PlayerListSnapshot> {
    const res = await this.request('GET', `/v1beta1/lists/${PLAYERS_LIST}`, undefined, [404]);
    if (res.status === 404) return { exists: false, capacity: 0, values: [] };
    return parseList((await res.json()) as RawList);
  }

  async addPlayer(sessionId: string): Promise<PlayerListSnapshot> {
    const res = await this.request(
      'POST',
      `/v1beta1/lists/${PLAYERS_LIST}:addValue`,
      { value: sessionId },
      [400, 404, 409]
    );
    if (res.ok) return parseList((await res.json()) as RawList);

    const body = await readGatewayError(res);
    if (res.status === 409) {
      throw new PlayerAlreadyConnectedError(sessionId);
    }
    if (res.status === 404) {
      throw new PlayerTrackingDisabledError();
    }
    // 400: OUT_OF_RANGE means the list is at capacity
    if (body.code === GRPC_OUT_OF_RANGE || /capacity|out of range/i.test(body.message ?? '')) {
      throw new ServerFullError(undefined);
    }
    throw new RequestFailedError(
      body.message ?? `addValue failed with HTTP ${res.status}`,
      res.status
    );
  }

  async removePlayer(sessionId: string): Promise<PlayerListSnapshot | null> {
    const res = await this.request(
      'POST',
      `/v1beta1/lists/${PLAYERS_LIST}:removeValue`,
      { value: sessionId },
      [404]
    );
    if (res.status === 404) return null;
    return parseList((await res.json()) as RawList);
  }

  async watchGameServer(onUpdate: (gs: RawGameServer) => void, signal: AbortSignal): Promise<void> {
    // Long-lived stream: no per-request timeout, only the caller's abort signal.
    const res = await this.fetch('GET', '/watch/gameserver', undefined, signal);
    if (!res.ok || !res.body) {
      throw new RequestFailedError(`watch failed with HTTP ${res.status}`, res.status);
    }
    const reader = res.body.getReader();
    const decoder = new TextDecoder();
    let buffer = '';
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      buffer += decoder.decode(value, { stream: true });
      let newline: number;
      while ((newline = buffer.indexOf('\n')) >= 0) {
        const line = buffer.slice(0, newline).trim();
        buffer = buffer.slice(newline + 1);
        if (line === '') continue;
        this.handleWatchLine(line, onUpdate);
      }
    }
  }

  private handleWatchLine(line: string, onUpdate: (gs: RawGameServer) => void): void {
    let parsed: { result?: RawGameServer; error?: GatewayError };
    try {
      parsed = JSON.parse(line) as { result?: RawGameServer; error?: GatewayError };
    } catch {
      this.logger.warn('watch: skipping malformed stream line');
      return;
    }
    if (parsed.error) {
      this.logger.warn(`watch: stream error: ${parsed.error.message ?? 'unknown'}`);
      return;
    }
    if (parsed.result) onUpdate(normalizeGameServer(parsed.result));
  }

  /**
   * Sends a request and maps failures to typed errors. Statuses listed in
   * `expectedStatuses` are returned to the caller instead of throwing.
   */
  private async request(
    method: string,
    path: string,
    body?: unknown,
    expectedStatuses: number[] = []
  ): Promise<Response> {
    const res = await this.fetch(method, path, body, AbortSignal.timeout(this.requestTimeoutMs));
    if (res.ok || expectedStatuses.includes(res.status)) return res;
    const gatewayError = await readGatewayError(res);
    throw new RequestFailedError(
      gatewayError.message ?? `${method} ${path} failed with HTTP ${res.status}`,
      res.status
    );
  }

  private async fetch(
    method: string,
    path: string,
    body: unknown,
    signal: AbortSignal
  ): Promise<Response> {
    try {
      // Paths contain custom verbs with a literal ':' (lists/players:addValue),
      // so the URL is built by plain concatenation.
      return await fetch(this.baseUrl + path, {
        method,
        signal,
        ...(body !== undefined
          ? { headers: { 'content-type': 'application/json' }, body: JSON.stringify(body) }
          : {}),
      });
    } catch (cause) {
      if (signal.aborted && !isTimeoutError(cause)) throw cause;
      throw new SidecarUnavailableError(
        `could not reach the GameFlow runtime at ${this.baseUrl} (${method} ${path})`,
        { cause }
      );
    }
  }
}

function isTimeoutError(cause: unknown): boolean {
  return cause instanceof DOMException && cause.name === 'TimeoutError';
}

async function readGatewayError(res: Response): Promise<GatewayError> {
  try {
    return (await res.json()) as GatewayError;
  } catch {
    return {};
  }
}
