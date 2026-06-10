// src/env.ts
function parsePort(value) {
  if (value === void 0 || value.trim() === '') return void 0;
  const port = Number(value);
  return Number.isInteger(port) && port > 0 ? port : void 0;
}
function normalizePortName(name) {
  return name.trim().replaceAll(' ', '_').toUpperCase();
}
function portGroup(env, prefix) {
  return {
    get default() {
      return parsePort(env[`${prefix}DEFAULT_PORT`]);
    },
    get(name) {
      return parsePort(env[`${prefix}${normalizePortName(name)}_PORT`]);
    },
  };
}
function readEnv(env = process.env) {
  const base = portGroup(env, 'GAMEFLOW_');
  return {
    ports: {
      get default() {
        return base.default;
      },
      get: (name) => base.get(name),
      tls: portGroup(env, 'GAMEFLOW_TLS_'),
    },
    get region() {
      return env.GAMEFLOW_REGION;
    },
    get buildId() {
      return env.GAMEFLOW_BUILD_ID;
    },
  };
}

// src/errors.ts
var GameFlowError = class extends Error {
  code;
  constructor(code, message, options) {
    super(message, options);
    this.name = 'GameFlowError';
    this.code = code;
  }
};
var SidecarUnavailableError = class extends GameFlowError {
  constructor(message, options) {
    super('SIDECAR_UNAVAILABLE', message, options);
    this.name = 'SidecarUnavailableError';
  }
};
var PlayerAlreadyConnectedError = class extends GameFlowError {
  sessionId;
  constructor(sessionId, options) {
    super('PLAYER_ALREADY_CONNECTED', `player "${sessionId}" is already connected`, options);
    this.name = 'PlayerAlreadyConnectedError';
    this.sessionId = sessionId;
  }
};
var ServerFullError = class extends GameFlowError {
  capacity;
  constructor(capacity, options) {
    super(
      'SERVER_FULL',
      `server is full${capacity !== void 0 ? ` (capacity ${capacity})` : ''}`,
      options
    );
    this.name = 'ServerFullError';
    this.capacity = capacity;
  }
};
var PlayerTrackingDisabledError = class extends GameFlowError {
  constructor(options) {
    super(
      'PLAYER_TRACKING_DISABLED',
      'player tracking is disabled for this server. Set "Max Players per Server" to a value greater than 0 in your game settings on GameFlow',
      options
    );
    this.name = 'PlayerTrackingDisabledError';
  }
};
var NotConnectedError = class extends GameFlowError {
  constructor(message = 'the SDK is not connected', options) {
    super('NOT_CONNECTED', message, options);
    this.name = 'NotConnectedError';
  }
};
var RequestFailedError = class extends GameFlowError {
  status;
  constructor(message, status, options) {
    super('REQUEST_FAILED', message, options);
    this.name = 'RequestFailedError';
    this.status = status;
  }
};

// src/health.ts
var HealthLoop = class {
  ping;
  intervalMs;
  logger;
  onDegraded;
  degradedThreshold;
  timer;
  running = false;
  consecutiveFailures = 0;
  degraded = false;
  constructor(options) {
    this.ping = options.ping;
    this.intervalMs = options.intervalMs;
    this.logger = options.logger;
    this.onDegraded = options.onDegraded;
    this.degradedThreshold = options.degradedThreshold ?? 6;
  }
  start() {
    if (this.running) return;
    this.running = true;
    void this.tick();
  }
  stop() {
    this.running = false;
    if (this.timer) clearTimeout(this.timer);
    this.timer = void 0;
  }
  async tick() {
    if (!this.running) return;
    try {
      await this.ping();
      if (this.degraded) this.logger.info('health pings recovered');
      this.consecutiveFailures = 0;
      this.degraded = false;
    } catch (cause) {
      this.consecutiveFailures++;
      this.logger.warn(
        `health ping failed (${this.consecutiveFailures} consecutive): ${String(cause)}`
      );
      if (this.consecutiveFailures >= this.degradedThreshold && !this.degraded) {
        this.degraded = true;
        this.logger.error(
          'health pings have been failing for a sustained period; the server may be marked unhealthy'
        );
        this.onDegraded?.();
      }
    }
    this.schedule();
  }
  schedule() {
    if (!this.running) return;
    this.timer = setTimeout(() => void this.tick(), this.intervalMs);
    this.timer.unref();
  }
};

// src/local.ts
var LocalTransport = class {
  logger;
  env;
  capacity;
  players = [];
  watchers = /* @__PURE__ */ new Set();
  state = 'Scheduled';
  constructor(options) {
    this.logger = options.logger;
    this.env = options.env ?? process.env;
    const raw = this.env.GAMEFLOW_MAX_PLAYERS;
    this.capacity = raw === void 0 || raw === '' ? Number.POSITIVE_INFINITY : Number(raw);
  }
  get trackingEnabled() {
    return this.capacity > 0;
  }
  async ready() {
    this.state = 'Ready';
    this.logger.debug('local: ready()');
    this.emit();
  }
  async health() {
    this.logger.debug('local: health ping');
  }
  async shutdown() {
    this.state = 'Shutdown';
    this.logger.debug('local: shutdown()');
    this.emit();
  }
  async getGameServer() {
    return this.buildGameServer();
  }
  async getPlayerList() {
    if (!this.trackingEnabled) return { exists: false, capacity: 0, values: [] };
    return { exists: true, capacity: this.capacity, values: [...this.players] };
  }
  async addPlayer(sessionId) {
    if (!this.trackingEnabled) throw new PlayerTrackingDisabledError();
    if (this.players.includes(sessionId)) throw new PlayerAlreadyConnectedError(sessionId);
    if (this.players.length >= this.capacity) throw new ServerFullError(this.capacity);
    this.players.push(sessionId);
    this.emit();
    return this.getPlayerList();
  }
  async removePlayer(sessionId) {
    if (!this.trackingEnabled) return null;
    const index = this.players.indexOf(sessionId);
    if (index < 0) return null;
    this.players.splice(index, 1);
    this.emit();
    return this.getPlayerList();
  }
  watchGameServer(onUpdate, signal) {
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
  emit() {
    const gs = this.buildGameServer();
    for (const watcher of this.watchers) watcher(gs);
  }
  buildGameServer() {
    const annotations = {};
    if (this.env.GAMEFLOW_PAYLOAD !== void 0) {
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
};

// src/log.ts
var PREFIX = '[gameflow]';
var defaultLogger = {
  debug: () => {},
  info: (message) => console.info(`${PREFIX} ${message}`),
  warn: (message) => console.warn(`${PREFIX} ${message}`),
  error: (message) => console.error(`${PREFIX} ${message}`),
};
var silentLogger = {
  debug: () => {},
  info: () => {},
  warn: () => {},
  error: () => {},
};
function resolveLogger(logger) {
  if (logger === null) return silentLogger;
  return logger ?? defaultLogger;
}

// src/transport.ts
var PLAYERS_LIST = 'players';
function parseList(raw) {
  if (!raw) return { exists: false, capacity: 0, values: [] };
  return { exists: true, capacity: Number(raw.capacity ?? 0), values: raw.values ?? [] };
}
var GRPC_OUT_OF_RANGE = 11;
var SidecarTransport = class {
  baseUrl;
  requestTimeoutMs;
  logger;
  constructor(options) {
    this.baseUrl = options.baseUrl;
    this.requestTimeoutMs = options.requestTimeoutMs;
    this.logger = options.logger;
  }
  async ready() {
    await this.request('POST', '/ready', {});
  }
  async health() {
    await this.request('POST', '/health', {});
  }
  async shutdown() {
    await this.request('POST', '/shutdown', {});
  }
  async getGameServer() {
    const res = await this.request('GET', '/gameserver');
    return await res.json();
  }
  async getPlayerList() {
    const res = await this.request('GET', `/v1beta1/lists/${PLAYERS_LIST}`, void 0, [404]);
    if (res.status === 404) return { exists: false, capacity: 0, values: [] };
    return parseList(await res.json());
  }
  async addPlayer(sessionId) {
    const res = await this.request(
      'POST',
      `/v1beta1/lists/${PLAYERS_LIST}:addValue`,
      { value: sessionId },
      [400, 404, 409]
    );
    if (res.ok) return parseList(await res.json());
    const body = await readGatewayError(res);
    if (res.status === 409) {
      throw new PlayerAlreadyConnectedError(sessionId);
    }
    if (res.status === 404) {
      throw new PlayerTrackingDisabledError();
    }
    if (body.code === GRPC_OUT_OF_RANGE || /capacity|out of range/i.test(body.message ?? '')) {
      throw new ServerFullError(void 0);
    }
    throw new RequestFailedError(
      body.message ?? `addValue failed with HTTP ${res.status}`,
      res.status
    );
  }
  async removePlayer(sessionId) {
    const res = await this.request(
      'POST',
      `/v1beta1/lists/${PLAYERS_LIST}:removeValue`,
      { value: sessionId },
      [404]
    );
    if (res.status === 404) return null;
    return parseList(await res.json());
  }
  async watchGameServer(onUpdate, signal) {
    const res = await this.fetch('GET', '/watch/gameserver', void 0, signal);
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
      let newline;
      while ((newline = buffer.indexOf('\n')) >= 0) {
        const line = buffer.slice(0, newline).trim();
        buffer = buffer.slice(newline + 1);
        if (line === '') continue;
        this.handleWatchLine(line, onUpdate);
      }
    }
  }
  handleWatchLine(line, onUpdate) {
    let parsed;
    try {
      parsed = JSON.parse(line);
    } catch {
      this.logger.warn('watch: skipping malformed stream line');
      return;
    }
    if (parsed.error) {
      this.logger.warn(`watch: stream error: ${parsed.error.message ?? 'unknown'}`);
      return;
    }
    if (parsed.result) onUpdate(parsed.result);
  }
  /**
   * Sends a request and maps failures to typed errors. Statuses listed in
   * `expectedStatuses` are returned to the caller instead of throwing.
   */
  async request(method, path, body, expectedStatuses = []) {
    const res = await this.fetch(method, path, body, AbortSignal.timeout(this.requestTimeoutMs));
    if (res.ok || expectedStatuses.includes(res.status)) return res;
    const gatewayError = await readGatewayError(res);
    throw new RequestFailedError(
      gatewayError.message ?? `${method} ${path} failed with HTTP ${res.status}`,
      res.status
    );
  }
  async fetch(method, path, body, signal) {
    try {
      return await fetch(this.baseUrl + path, {
        method,
        signal,
        ...(body !== void 0
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
};
function isTimeoutError(cause) {
  return cause instanceof DOMException && cause.name === 'TimeoutError';
}
async function readGatewayError(res) {
  try {
    return await res.json();
  } catch {
    return {};
  }
}

// src/players.ts
var Players = class {
  transport;
  snapshot = { exists: false, capacity: 0, values: [] };
  constructor(transport) {
    this.transport = transport;
  }
  /** Registers a player session. Call when a player joins the server. */
  async connect(sessionId) {
    try {
      this.snapshot = await this.transport.addPlayer(sessionId);
    } catch (error) {
      if (error instanceof PlayerTrackingDisabledError) {
        this.snapshot = { exists: false, capacity: 0, values: [] };
      }
      if (error instanceof ServerFullError && error.capacity === void 0) {
        throw new ServerFullError(this.snapshot.capacity, { cause: error });
      }
      throw error;
    }
  }
  /**
   * Unregisters a player session. Resolves false when the player was not in
   * the list (safe to call on every disconnect, including duplicates).
   */
  async disconnect(sessionId) {
    if (!this.snapshot.exists) throw new PlayerTrackingDisabledError();
    const updated = await this.transport.removePlayer(sessionId);
    if (updated === null) return false;
    this.snapshot = updated;
    return true;
  }
  /** Current number of connected players (from the local cache). */
  count() {
    return this.snapshot.values.length;
  }
  /** Session ids of connected players (from the local cache). */
  list() {
    return [...this.snapshot.values];
  }
  /** Maximum players configured for this game. */
  capacity() {
    return this.snapshot.capacity;
  }
  /** False when the game was created with max players = 0. */
  get trackingEnabled() {
    return this.snapshot.exists;
  }
  /** Re-reads the list from the runtime. */
  async refresh() {
    this.snapshot = await this.transport.getPlayerList();
  }
  /** Internal: updates the cache from a game server object (seed and watch). */
  syncFromGameServer(gs) {
    this.snapshot = parseList(gs.status?.lists?.[PLAYERS_LIST]);
  }
};

// src/watch.ts
var INITIAL_BACKOFF_MS = 250;
var MAX_BACKOFF_MS = 5e3;
var GameServerWatcher = class {
  transport;
  logger;
  /** Invoked before user listeners on every update (cache sync hook). */
  onUpdate;
  listeners = /* @__PURE__ */ new Set();
  controller;
  latest;
  stopped = false;
  constructor(transport, logger) {
    this.transport = transport;
    this.logger = logger;
  }
  subscribe(listener) {
    this.listeners.add(listener);
    if (this.latest) listener(this.latest);
    this.ensureStream();
    return () => {
      this.listeners.delete(listener);
      if (this.listeners.size === 0) this.closeStream();
    };
  }
  stop() {
    this.stopped = true;
    this.listeners.clear();
    this.closeStream();
  }
  ensureStream() {
    if (this.controller || this.stopped) return;
    this.controller = new AbortController();
    void this.run(this.controller);
  }
  closeStream() {
    this.controller?.abort();
    this.controller = void 0;
  }
  async run(controller) {
    let backoffMs = INITIAL_BACKOFF_MS;
    while (!controller.signal.aborted) {
      try {
        await this.transport.watchGameServer((gs) => {
          backoffMs = INITIAL_BACKOFF_MS;
          this.dispatch(gs);
        }, controller.signal);
        this.logger.debug('watch stream ended; reconnecting');
      } catch (cause) {
        if (controller.signal.aborted) return;
        this.logger.warn(`watch stream failed (retrying in ${backoffMs}ms): ${String(cause)}`);
      }
      await abortableSleep(backoffMs, controller.signal);
      backoffMs = Math.min(backoffMs * 2, MAX_BACKOFF_MS);
    }
  }
  dispatch(gs) {
    this.latest = gs;
    this.onUpdate?.(gs);
    for (const listener of this.listeners) listener(gs);
  }
};
function abortableSleep(ms, signal) {
  return new Promise((resolve) => {
    if (signal.aborted) return resolve();
    const timer = setTimeout(resolve, ms);
    timer.unref();
    signal.addEventListener(
      'abort',
      () => {
        clearTimeout(timer);
        resolve();
      },
      { once: true }
    );
  });
}

// src/gameflow.ts
var PAYLOAD_ANNOTATION = 'GAMEFLOW_PAYLOAD';
var DEFAULT_SIDECAR_PORT = 9358;
var DEFAULT_HEALTH_INTERVAL_MS = 5e3;
var MIN_HEALTH_INTERVAL_MS = 500;
var DEFAULT_CONNECT_TIMEOUT_MS = 3e4;
var DEFAULT_REQUEST_TIMEOUT_MS = 3e3;
var GameFlow = class _GameFlow {
  /** Player tracking. */
  players;
  /** Transport in use: 'sidecar' when running on GameFlow, 'local' otherwise. */
  mode;
  transport;
  logger;
  health;
  watcher;
  env;
  payloadListeners = /* @__PURE__ */ new Set();
  lastPayload;
  state = 'connected';
  /**
   * Connects to the GameFlow runtime (with retries) or falls back to local
   * mode when the server is not running on GameFlow.
   */
  static async connect(options = {}) {
    const logger = resolveLogger(options.logger);
    const mode = resolveMode(options.mode, process.env, logger);
    const requestTimeoutMs = options.requestTimeoutMs ?? DEFAULT_REQUEST_TIMEOUT_MS;
    let transport;
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
    const gameflow = new _GameFlow(transport, mode, logger, options);
    await gameflow.init(options.connectTimeoutMs ?? DEFAULT_CONNECT_TIMEOUT_MS);
    return gameflow;
  }
  constructor(transport, mode, logger, options) {
    this.mode = mode;
    this.logger = logger;
    this.env = readEnv();
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
  async init(connectTimeoutMs) {
    const gs = await this.probeWithRetry(connectTimeoutMs);
    this.players.syncFromGameServer(gs);
    this.lastPayload = gs.objectMeta?.annotations?.[PAYLOAD_ANNOTATION];
    if (this.mode === 'sidecar' && !this.players.trackingEnabled) {
      this.logger.warn(
        'player tracking is disabled for this server (max players is 0). The platform cannot see player counts and idle servers with no trackable players may be shut down. Set "Max Players per Server" in your game settings to enable tracking.'
      );
    }
    this.logger.info(`connected (mode: ${this.mode})`);
  }
  async probeWithRetry(connectTimeoutMs) {
    const deadline = Date.now() + connectTimeoutMs;
    let backoffMs = 250;
    let lastError;
    for (;;) {
      try {
        return await this.transport.getGameServer();
      } catch (error) {
        lastError = error;
        const waitMs = jitter(backoffMs);
        if (Date.now() + waitMs >= deadline) break;
        this.logger.debug(`runtime not reachable yet; retrying in ${waitMs}ms`);
        await sleep(waitMs);
        backoffMs = Math.min(backoffMs * 2, 4e3);
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
  async ready() {
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
  async shutdown() {
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
  async payload() {
    this.assertUsable();
    const gs = await this.transport.getGameServer();
    this.lastPayload = gs.objectMeta?.annotations?.[PAYLOAD_ANNOTATION];
    return this.lastPayload;
  }
  /** Current server details (name, state, address, ports). */
  async info() {
    this.assertUsable();
    return toInfo(await this.transport.getGameServer());
  }
  /**
   * Subscribes to server updates. Returns an unsubscribe function. The stream
   * is shared across subscribers and reconnects automatically.
   */
  watch(listener) {
    this.assertUsable();
    return this.watcher.subscribe((gs) => listener(toInfo(gs)));
  }
  /** Fires when the launch payload changes (e.g. on match assignment). */
  onPayloadChange(listener) {
    this.assertUsable();
    this.payloadListeners.add(listener);
    const unsubscribe = this.watcher.subscribe(() => {});
    return () => {
      this.payloadListeners.delete(listener);
      unsubscribe();
    };
  }
  /** Ports assigned to this server (from GAMEFLOW_*_PORT env vars). */
  get ports() {
    return this.env.ports;
  }
  /** Region this server runs in, when provided by the platform. */
  get region() {
    return this.env.region;
  }
  /** Build id of the running image, when provided by the platform. */
  get buildId() {
    return this.env.buildId;
  }
  handleUpdate(gs) {
    this.players.syncFromGameServer(gs);
    const payload = gs.objectMeta?.annotations?.[PAYLOAD_ANNOTATION];
    if (payload !== this.lastPayload) {
      this.lastPayload = payload;
      for (const listener of this.payloadListeners) listener(payload);
    }
  }
  assertUsable() {
    if (this.state === 'shutting-down' || this.state === 'shutdown') {
      throw new NotConnectedError('the SDK has been shut down');
    }
  }
};
function resolveMode(option, env, logger) {
  if (option === 'local' || option === 'sidecar') return option;
  const envMode = env.GAMEFLOW_SDK_MODE;
  if (envMode === 'local' || envMode === 'sidecar') return envMode;
  if (env.AGONES_SDK_HTTP_PORT) return 'sidecar';
  logger.info(
    'no GameFlow runtime detected; running in local mode (lifecycle and player tracking are simulated)'
  );
  return 'local';
}
function guardTransport(inner, isShutdown) {
  const guard = () => {
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
function toInfo(gs) {
  return {
    name: gs.objectMeta?.name ?? '',
    state: gs.status?.state ?? '',
    address: gs.status?.address ?? '',
    ports: (gs.status?.ports ?? []).map((p) => ({ name: p.name ?? '', port: p.port ?? 0 })),
    labels: gs.objectMeta?.labels ?? {},
    annotations: gs.objectMeta?.annotations ?? {},
  };
}
function jitter(ms) {
  return Math.round(ms * (0.8 + Math.random() * 0.4));
}
function sleep(ms) {
  return new Promise((resolve) => {
    const timer = setTimeout(resolve, ms);
    timer.unref();
  });
}
export {
  GameFlow,
  GameFlowError,
  NotConnectedError,
  PlayerAlreadyConnectedError,
  PlayerTrackingDisabledError,
  Players,
  RequestFailedError,
  ServerFullError,
  SidecarUnavailableError,
};
//# sourceMappingURL=index.js.map
