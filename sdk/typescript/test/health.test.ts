import { afterEach, describe, expect, it } from 'vitest';

import { HealthLoop } from '../src/health';
import { silentLogger } from '../src/log';

let loop: HealthLoop | undefined;

afterEach(() => {
  loop?.stop();
});

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

describe('HealthLoop', () => {
  it('pings repeatedly at the configured cadence', async () => {
    let pings = 0;
    loop = new HealthLoop({
      ping: async () => {
        pings++;
      },
      intervalMs: 20,
      logger: silentLogger,
    });
    loop.start();
    await delay(110);
    expect(pings).toBeGreaterThanOrEqual(3);
  });

  it('stops pinging after stop()', async () => {
    let pings = 0;
    loop = new HealthLoop({
      ping: async () => {
        pings++;
      },
      intervalMs: 10,
      logger: silentLogger,
    });
    loop.start();
    await delay(50);
    loop.stop();
    const after = pings;
    await delay(50);
    expect(pings).toBe(after);
  });

  it('keeps going through failures and reports degraded once', async () => {
    let pings = 0;
    let degraded = 0;
    loop = new HealthLoop({
      ping: async () => {
        pings++;
        throw new Error('boom');
      },
      intervalMs: 5,
      logger: silentLogger,
      degradedThreshold: 3,
      onDegraded: () => {
        degraded++;
      },
    });
    loop.start();
    await delay(100);
    expect(pings).toBeGreaterThanOrEqual(5); // failures never stop the loop
    expect(degraded).toBe(1); // reported exactly once
  });

  it('recovers the degraded flag on success', async () => {
    let fail = true;
    let degraded = 0;
    loop = new HealthLoop({
      ping: async () => {
        if (fail) throw new Error('boom');
      },
      intervalMs: 5,
      logger: silentLogger,
      degradedThreshold: 2,
      onDegraded: () => {
        degraded++;
      },
    });
    loop.start();
    await delay(40);
    fail = false; // recover
    await delay(30);
    fail = true; // degrade again
    await delay(40);
    expect(degraded).toBe(2);
  });

  it('does not overlap pings when responses are slow', async () => {
    let inFlight = 0;
    let maxInFlight = 0;
    loop = new HealthLoop({
      ping: async () => {
        inFlight++;
        maxInFlight = Math.max(maxInFlight, inFlight);
        await delay(30);
        inFlight--;
      },
      intervalMs: 5,
      logger: silentLogger,
    });
    loop.start();
    await delay(120);
    expect(maxInFlight).toBe(1);
  });

  it('does not keep the process alive (timers are unref’d)', async () => {
    const { execFileSync } = await import('node:child_process');
    const script = `
      const { HealthLoop } = await import('${import.meta.dirname?.replace(/\\/g, '/')}/../src/health.ts');
      const loop = new HealthLoop({
        ping: async () => {},
        intervalMs: 1000,
        logger: { debug() {}, info() {}, warn() {}, error() {} },
      });
      loop.start();
      // No stop(): the process must exit on its own because timers are unref'd.
    `;
    execFileSync(
      process.execPath,
      ['--experimental-strip-types', '--input-type=module', '-e', script],
      {
        timeout: 5000,
      }
    );
  });
});
