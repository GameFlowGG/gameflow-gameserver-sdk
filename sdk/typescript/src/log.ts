export interface Logger {
  debug(message: string): void;
  info(message: string): void;
  warn(message: string): void;
  error(message: string): void;
}

const PREFIX = '[gameflow]';

export const defaultLogger: Logger = {
  debug: () => {},
  info: (message) => console.info(`${PREFIX} ${message}`),
  warn: (message) => console.warn(`${PREFIX} ${message}`),
  error: (message) => console.error(`${PREFIX} ${message}`),
};

export const silentLogger: Logger = {
  debug: () => {},
  info: () => {},
  warn: () => {},
  error: () => {},
};

/**
 * Resolves the logger option: `undefined` means the default console logger,
 * `null` silences the SDK entirely.
 */
export function resolveLogger(logger: Logger | null | undefined): Logger {
  if (logger === null) return silentLogger;
  return logger ?? defaultLogger;
}
