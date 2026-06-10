export interface PortGroup {
  /** The game's primary port (GAMEFLOW_DEFAULT_PORT). */
  readonly default: number | undefined;
  /** An additional named port, as configured in the game's networking settings. */
  get(name: string): number | undefined;
}

export interface Ports extends PortGroup {
  /** TLS-terminated listener ports, present only when the game uses TLS. */
  readonly tls: PortGroup;
}

export interface GameFlowEnv {
  readonly ports: Ports;
  readonly region: string | undefined;
  readonly buildId: string | undefined;
}

function parsePort(value: string | undefined): number | undefined {
  if (value === undefined || value.trim() === '') return undefined;
  const port = Number(value);
  return Number.isInteger(port) && port > 0 ? port : undefined;
}

/**
 * Mirrors the platform's env var naming: port names are trimmed, spaces become
 * underscores and the result is uppercased ("voice chat" -> VOICE_CHAT).
 */
export function normalizePortName(name: string): string {
  return name.trim().replaceAll(' ', '_').toUpperCase();
}

function portGroup(env: NodeJS.ProcessEnv, prefix: string): PortGroup {
  return {
    get default() {
      return parsePort(env[`${prefix}DEFAULT_PORT`]);
    },
    get(name: string) {
      return parsePort(env[`${prefix}${normalizePortName(name)}_PORT`]);
    },
  };
}

export function readEnv(env: NodeJS.ProcessEnv = process.env): GameFlowEnv {
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
