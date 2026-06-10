import { readEnv, type GameFlowEnv, type Ports } from './env';
import { NotConnectedError, SidecarUnavailableError } from './errors';
import { HealthLoop } from './health';
import { LocalTransport } from './local';
import { resolveLogger, type Logger } from './log';
import { Players } from './players';
import { SidecarTransport, type Transport } from './transport';
import type { ConnectOptions, GameServerInfo, Mode, RawGameServer } from './types';
import { GameServerWatcher } from './watch';

const PAYLOAD_ANNOTATION = 'GAMEFLOW_PAYLOAD';
const DEFAULT_SIDECAR_PORT = 9358;
const DEFAULT_HEALTH_INTERVAL_MS = 5000;
const MIN_HEALTH_INTERVAL_MS = 500;
const DEFAULT_CONNECT_TIMEOUT_MS = 30000;
const DEFAULT_REQUEST_TIMEOUT_MS = 3000;

type State = 'connected' | 'ready' | 'shutting-down' | 'shutdown';

export class GameFlow {
  /** Player tracking. */
  readonly players: Players;
  /** Transport in use: 'sidecar' when running on GameFlow, 'local' otherwise. */
  readonly mode: Mode;

  private readonly transport: Transport;
  private readonly logger: Logger;
  private readonly health: HealthLoop;
  private readonly watcher: GameServerWatcher;
  private readonly env: GameFlowEnv;
  private readonly payloadListeners = new Set<(payload: string | undefined) => void>();
  private lastPayload: string | undefined;
  private state: State = 'connected';

  /**
   * Connects to the GameFlow runtime (with retries) or falls back to local
   * mode when the server is not running on GameFlow.
   */
  static async connect(options: ConnectOptions = {}): Promise<GameFlow> {
    const logger = resolveLogger(options.logger);
    const mode = resolveMode(options.mode, process.env, logger);
    const requestTimeoutMs = options.requestTimeoutMs ?? DEFAULT_REQUEST_TIMEOUT_MS;

    let transport: Transport;
    if (mode === 'local') {
      transport = new LocalTransport({ logger });
    } else {
      const port = Number(process.env.AGONES_SDK_HTTP_PORT) || DEFAULT_SIDECAR_PORT;
      transport = new SidecarTransport({
        baseUrl: `http://127.0.0.1:${port}`,
        requestTimeoutMs,
        logger,
      });
    }

    const gameflow = new GameFlow(transport, mode, logger, options);
    await gameflow.init(options.connectTimeoutMs ?? DEFAULT_CONNECT_TIMEOUT_MS);
    return gameflow;
  }

  private constructor(transport: Transport, mode: Mode, logger: Logger, options: ConnectOptions) {
    this.mode = mode;
    this.logger = logger;
    this.env = readEnv();
    // All calls go through a guard so post-shutdown use fails loudly.
    this.transport = guardTransport(transport, () => this.state === 'shutdown');
    this.players = new Players(this.transport);
    this.watcher = new GameServerWatcher(this.transport, logger);
    this.watcher.onUpdate = (gs) => this.handleUpdate(gs);
    this.health = new HealthLoop({
      ping: () => this.transport.health(),
      intervalMs: Math.max(
        MIN_HEALTH_INTERVAL_MS,
        options.healthIntervalMs ?? DEFAULT_HEALTH_INTERVAL_MS
      ),
      logger,
      onDegraded: options.onHealthDegraded,
    });
  }

  private async init(connectTimeoutMs: number): Promise<void> {
    const gs = await this.probeWithRetry(connectTimeoutMs);
    this.players.syncFromGameServer(gs);
    this.lastPayload = gs.objectMeta?.annotations?.[PAYLOAD_ANNOTATION];
    if (this.mode === 'sidecar' && !this.players.trackingEnabled) {
      this.logger.warn(
        'player tracking is disabled for this server (max players is 0). The platform cannot ' +
          'see player counts and idle servers with no trackable players may be shut down. ' +
          'Set "Max Players per Server" in your game settings to enable tracking.'
      );
    }
    this.logger.info(`connected (mode: ${this.mode})`);
  }

  private async probeWithRetry(connectTimeoutMs: number): Promise<RawGameServer> {
    const deadline = Date.now() + connectTimeoutMs;
    let backoffMs = 250;
    let lastError: unknown;
    for (;;) {
      try {
        return await this.transport.getGameServer();
      } catch (error) {
        lastError = error;
        const waitMs = jitter(backoffMs);
        if (Date.now() + waitMs >= deadline) break;
        this.logger.debug(`runtime not reachable yet; retrying in ${waitMs}ms`);
        await sleep(waitMs);
        backoffMs = Math.min(backoffMs * 2, 4000);
      }
    }
    throw new SidecarUnavailableError(
      `could not connect to the GameFlow runtime within ${connectTimeoutMs}ms`,
      { cause: lastError }
    );
  }

  /**
   * Marks the server ready to accept players and starts the automatic health
   * heartbeat. Call once your server is listening.
   */
  async ready(): Promise<void> {
    this.assertUsable();
    if (this.state === 'ready') {
      this.logger.debug('ready() called more than once; ignoring');
      return;
    }
    await this.transport.ready();
    this.state = 'ready';
    if (this.mode === 'sidecar') this.health.start();
    this.logger.info('server marked ready');
  }

  /**
   * Shuts the server down cleanly. Idempotent. After this resolves the
   * platform will terminate the server process.
   */
  async shutdown(): Promise<void> {
    if (this.state === 'shutting-down' || this.state === 'shutdown') return;
    this.state = 'shutting-down';
    this.health.stop();
    this.watcher.stop();
    try {
      await this.transport.shutdown();
    } catch (cause) {
      this.logger.warn(`shutdown request failed: ${String(cause)}`);
    }
    this.state = 'shutdown';
    this.logger.info('server shut down');
  }

  /**
   * The launch payload for this server (an opaque string set when the server
   * was requested), or undefined when none was provided. May change when the
   * server is assigned to a new match; use onPayloadChange() to react.
   */
  async payload(): Promise<string | undefined> {
    this.assertUsable();
    const gs = await this.transport.getGameServer();
    this.lastPayload = gs.objectMeta?.annotations?.[PAYLOAD_ANNOTATION];
    return this.lastPayload;
  }

  /** Current server details (name, state, address, ports). */
  async info(): Promise<GameServerInfo> {
    this.assertUsable();
    return toInfo(await this.transport.getGameServer());
  }

  /**
   * Subscribes to server updates. Returns an unsubscribe function. The stream
   * is shared across subscribers and reconnects automatically.
   */
  watch(listener: (info: GameServerInfo) => void): () => void {
    this.assertUsable();
    return this.watcher.subscribe((gs) => listener(toInfo(gs)));
  }

  /** Fires when the launch payload changes (e.g. on match assignment). */
  onPayloadChange(listener: (payload: string | undefined) => void): () => void {
    this.assertUsable();
    this.payloadListeners.add(listener);
    const unsubscribe = this.watcher.subscribe(() => {});
    return () => {
      this.payloadListeners.delete(listener);
      unsubscribe();
    };
  }

  /** Ports assigned to this server (from GAMEFLOW_*_PORT env vars). */
  get ports(): Ports {
    return this.env.ports;
  }

  /** Region this server runs in, when provided by the platform. */
  get region(): string | undefined {
    return this.env.region;
  }

  /** Build id of the running image, when provided by the platform. */
  get buildId(): string | undefined {
    return this.env.buildId;
  }

  private handleUpdate(gs: RawGameServer): void {
    this.players.syncFromGameServer(gs);
    const payload = gs.objectMeta?.annotations?.[PAYLOAD_ANNOTATION];
    if (payload !== this.lastPayload) {
      this.lastPayload = payload;
      for (const listener of this.payloadListeners) listener(payload);
    }
  }

  private assertUsable(): void {
    if (this.state === 'shutting-down' || this.state === 'shutdown') {
      throw new NotConnectedError('the SDK has been shut down');
    }
  }
}

function resolveMode(
  option: 'auto' | Mode | undefined,
  env: NodeJS.ProcessEnv,
  logger: Logger
): Mode {
  if (option === 'local' || option === 'sidecar') return option;
  const envMode = env.GAMEFLOW_SDK_MODE;
  if (envMode === 'local' || envMode === 'sidecar') return envMode;
  if (env.AGONES_SDK_HTTP_PORT) return 'sidecar';
  logger.info(
    'no GameFlow runtime detected; running in local mode (lifecycle and player tracking are simulated)'
  );
  return 'local';
}

function guardTransport(inner: Transport, isShutdown: () => boolean): Transport {
  const guard = (): void => {
    if (isShutdown()) throw new NotConnectedError('the SDK has been shut down');
  };
  return {
    ready: async () => (guard(), inner.ready()),
    health: async () => (guard(), inner.health()),
    // shutdown stays unguarded: it is what transitions the state
    shutdown: async () => inner.shutdown(),
    getGameServer: async () => (guard(), inner.getGameServer()),
    getPlayerList: async () => (guard(), inner.getPlayerList()),
    addPlayer: async (id) => (guard(), inner.addPlayer(id)),
    removePlayer: async (id) => (guard(), inner.removePlayer(id)),
    watchGameServer: async (onUpdate, signal) => (guard(), inner.watchGameServer(onUpdate, signal)),
  };
}

function toInfo(gs: RawGameServer): GameServerInfo {
  return {
    name: gs.objectMeta?.name ?? '',
    state: gs.status?.state ?? '',
    address: gs.status?.address ?? '',
    ports: (gs.status?.ports ?? []).map((p) => ({ name: p.name ?? '', port: p.port ?? 0 })),
    labels: gs.objectMeta?.labels ?? {},
    annotations: gs.objectMeta?.annotations ?? {},
  };
}

function jitter(ms: number): number {
  return Math.round(ms * (0.8 + Math.random() * 0.4));
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => {
    const timer = setTimeout(resolve, ms);
    timer.unref();
  });
}
