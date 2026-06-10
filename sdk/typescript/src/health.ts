import type { Logger } from './log';

export interface HealthLoopOptions {
  ping: () => Promise<void>;
  intervalMs: number;
  logger: Logger;
  onDegraded?: () => void;
  /** Consecutive failures before reporting degraded health. Default 6. */
  degradedThreshold?: number;
}

/**
 * Automatic health heartbeat. Uses a setTimeout chain (next ping is scheduled
 * only after the previous one settles), so slow responses can never pile up.
 * Timers are unref'd so the loop never keeps the process alive.
 *
 * Failures are tolerated: the platform allows several missed pings, so the
 * loop logs and keeps going at the normal cadence. After `degradedThreshold`
 * consecutive failures it reports degraded health once (error log +
 * onDegraded callback) and recovers silently on the next success.
 */
export class HealthLoop {
  private readonly ping: () => Promise<void>;
  private readonly intervalMs: number;
  private readonly logger: Logger;
  private readonly onDegraded: (() => void) | undefined;
  private readonly degradedThreshold: number;

  private timer: NodeJS.Timeout | undefined;
  private running = false;
  private consecutiveFailures = 0;
  private degraded = false;

  constructor(options: HealthLoopOptions) {
    this.ping = options.ping;
    this.intervalMs = options.intervalMs;
    this.logger = options.logger;
    this.onDegraded = options.onDegraded;
    this.degradedThreshold = options.degradedThreshold ?? 6;
  }

  start(): void {
    if (this.running) return;
    this.running = true;
    void this.tick();
  }

  stop(): void {
    this.running = false;
    if (this.timer) clearTimeout(this.timer);
    this.timer = undefined;
  }

  private async tick(): Promise<void> {
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

  private schedule(): void {
    if (!this.running) return;
    this.timer = setTimeout(() => void this.tick(), this.intervalMs);
    this.timer.unref();
  }
}
