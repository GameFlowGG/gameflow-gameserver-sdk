export type GameFlowErrorCode =
  | 'SIDECAR_UNAVAILABLE'
  | 'PLAYER_ALREADY_CONNECTED'
  | 'SERVER_FULL'
  | 'PLAYER_TRACKING_DISABLED'
  | 'NOT_CONNECTED'
  | 'REQUEST_FAILED';

/**
 * Base class for all errors thrown by the SDK. The `code` field is stable and
 * safe for programmatic handling; messages are not.
 */
export class GameFlowError extends Error {
  readonly code: GameFlowErrorCode;

  constructor(code: GameFlowErrorCode, message: string, options?: ErrorOptions) {
    super(message, options);
    this.name = 'GameFlowError';
    this.code = code;
  }
}

/** The GameFlow runtime could not be reached (or stopped responding). */
export class SidecarUnavailableError extends GameFlowError {
  constructor(message: string, options?: ErrorOptions) {
    super('SIDECAR_UNAVAILABLE', message, options);
    this.name = 'SidecarUnavailableError';
  }
}

/** The session id is already in the connected players list. */
export class PlayerAlreadyConnectedError extends GameFlowError {
  readonly sessionId: string;

  constructor(sessionId: string, options?: ErrorOptions) {
    super('PLAYER_ALREADY_CONNECTED', `player "${sessionId}" is already connected`, options);
    this.name = 'PlayerAlreadyConnectedError';
    this.sessionId = sessionId;
  }
}

/** The players list is at capacity. */
export class ServerFullError extends GameFlowError {
  readonly capacity: number | undefined;

  constructor(capacity: number | undefined, options?: ErrorOptions) {
    super(
      'SERVER_FULL',
      `server is full${capacity !== undefined ? ` (capacity ${capacity})` : ''}`,
      options
    );
    this.name = 'ServerFullError';
    this.capacity = capacity;
  }
}

/**
 * Player tracking is not enabled for this server. This happens when the game
 * is configured with max players = 0 in GameFlow.
 */
export class PlayerTrackingDisabledError extends GameFlowError {
  constructor(options?: ErrorOptions) {
    super(
      'PLAYER_TRACKING_DISABLED',
      'player tracking is disabled for this server. Set "Max Players per Server" to a value ' +
        'greater than 0 in your game settings on GameFlow',
      options
    );
    this.name = 'PlayerTrackingDisabledError';
  }
}

/** A method was called before connect() resolved or after shutdown(). */
export class NotConnectedError extends GameFlowError {
  constructor(message = 'the SDK is not connected', options?: ErrorOptions) {
    super('NOT_CONNECTED', message, options);
    this.name = 'NotConnectedError';
  }
}

/** An unexpected non-2xx response or network failure. */
export class RequestFailedError extends GameFlowError {
  readonly status: number | undefined;

  constructor(message: string, status?: number, options?: ErrorOptions) {
    super('REQUEST_FAILED', message, options);
    this.name = 'RequestFailedError';
    this.status = status;
  }
}
