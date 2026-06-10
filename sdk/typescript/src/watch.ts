import type { Logger } from './log';
import type { Transport } from './transport';
import type { RawGameServer } from './types';

const INITIAL_BACKOFF_MS = 250;
const MAX_BACKOFF_MS = 5000;

/**
 * Multiplexes a single watch stream to any number of subscribers and keeps it
 * alive with reconnect + backoff. The stream is opened lazily on the first
 * subscriber and closed when the last one unsubscribes (or on stop()).
 */
export class GameServerWatcher {
  private readonly transport: Transport;
  private readonly logger: Logger;
  /** Invoked before user listeners on every update (cache sync hook). */
  onUpdate: ((gs: RawGameServer) => void) | undefined;

  private readonly listeners = new Set<(gs: RawGameServer) => void>();
  private controller: AbortController | undefined;
  private latest: RawGameServer | undefined;
  private stopped = false;

  constructor(transport: Transport, logger: Logger) {
    this.transport = transport;
    this.logger = logger;
  }

  subscribe(listener: (gs: RawGameServer) => void): () => void {
    this.listeners.add(listener);
    if (this.latest) listener(this.latest);
    this.ensureStream();
    return () => {
      this.listeners.delete(listener);
      if (this.listeners.size === 0) this.closeStream();
    };
  }

  stop(): void {
    this.stopped = true;
    this.listeners.clear();
    this.closeStream();
  }

  private ensureStream(): void {
    if (this.controller || this.stopped) return;
    this.controller = new AbortController();
    void this.run(this.controller);
  }

  private closeStream(): void {
    this.controller?.abort();
    this.controller = undefined;
  }

  private async run(controller: AbortController): Promise<void> {
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

  private dispatch(gs: RawGameServer): void {
    this.latest = gs;
    this.onUpdate?.(gs);
    for (const listener of this.listeners) listener(gs);
  }
}

function abortableSleep(ms: number, signal: AbortSignal): Promise<void> {
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
