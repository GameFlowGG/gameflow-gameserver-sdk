import type { Logger } from './log';

export type Mode = 'sidecar' | 'local';

export interface ConnectOptions {
  /**
   * Transport selection. 'auto' (default) uses the GameFlow runtime when the
   * server runs on GameFlow and falls back to local mode otherwise. The
   * GAMEFLOW_SDK_MODE env var ('local' | 'sidecar') overrides 'auto'.
   */
  mode?: 'auto' | Mode;
  /** Interval between automatic health pings. Default 5000, minimum 500. */
  healthIntervalMs?: number;
  /** Total budget for connect() including retries. Default 30000. */
  connectTimeoutMs?: number;
  /** Per-request timeout. Default 3000. */
  requestTimeoutMs?: number;
  /** Custom logger. `null` silences the SDK. */
  logger?: Logger | null;
  /** Called once when health pings have been failing for a sustained period. */
  onHealthDegraded?: () => void;
}

export interface ServerPort {
  name: string;
  port: number;
}

export interface GameServerInfo {
  name: string;
  state: string;
  address: string;
  ports: ServerPort[];
  labels: Record<string, string>;
  annotations: Record<string, string>;
}

/** Wire shapes returned by the local runtime (grpc-gateway JSON). */
export interface RawList {
  // int64 fields arrive as JSON strings
  capacity?: string | number;
  values?: string[];
}

export interface RawGameServer {
  objectMeta?: {
    name?: string;
    annotations?: Record<string, string>;
    labels?: Record<string, string>;
  };
  status?: {
    state?: string;
    address?: string;
    ports?: { name?: string; port?: number }[];
    lists?: Record<string, RawList>;
  };
}

/** Parsed players list state. `exists` is false when tracking is disabled. */
export interface PlayerListSnapshot {
  exists: boolean;
  capacity: number;
  values: string[];
}
