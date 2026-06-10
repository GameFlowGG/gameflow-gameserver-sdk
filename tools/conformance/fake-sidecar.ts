import { createServer, type IncomingMessage, type Server, type ServerResponse } from 'node:http';
import type { AddressInfo } from 'node:net';

import type { RawGameServer } from '../../sdk/typescript/src/types';

export interface RecordedRequest {
  method: string;
  path: string;
  body: string;
}

interface FakeList {
  capacity: number;
  values: string[];
}

/**
 * In-memory stand-in for the local GameFlow runtime (the Agones sidecar REST
 * API). Mirrors the grpc-gateway behavior the SDK depends on: int64 fields
 * serialized as strings, gRPC error bodies, NDJSON watch streaming and custom
 * verbs with a literal ':' in the path.
 */
export class FakeSidecar {
  readonly requests: RecordedRequest[] = [];
  gameServer: RawGameServer = {
    objectMeta: { name: 'gs-test', annotations: {}, labels: {} },
    status: { state: 'Scheduled', address: '10.0.0.1', ports: [{ name: 'default', port: 7777 }] },
  };
  lists = new Map<string, FakeList>();
  /** Respond 503 to this many requests before behaving normally again. */
  failNextRequests = 0;
  /**
   * Apply list mutations but answer them with an empty 200 body, like a
   * runtime whose response framing the client could not read. SDKs must
   * recover by re-reading the list.
   */
  emptyMutationBody = false;
  /** Delay every response by this many milliseconds. */
  delayMs = 0;

  private server: Server | undefined;
  private watchClients = new Set<ServerResponse>();

  withPlayersList(capacity: number, values: string[] = []): this {
    this.lists.set('players', { capacity, values: [...values] });
    this.syncGameServerLists();
    return this;
  }

  async start(): Promise<number> {
    this.server = createServer((req, res) => void this.handle(req, res));
    await new Promise<void>((resolve) => this.server!.listen(0, '127.0.0.1', resolve));
    return (this.server!.address() as AddressInfo).port;
  }

  async stop(): Promise<void> {
    for (const client of this.watchClients) client.end();
    this.watchClients.clear();
    if (this.server) {
      this.server.closeAllConnections();
      await new Promise<void>((resolve) => this.server!.close(() => resolve()));
    }
  }

  /** Pushes the current game server state to all connected watch streams. */
  pushWatchUpdate(): void {
    this.syncGameServerLists();
    const line = JSON.stringify({ result: toWire(this.gameServer) }) + '\n';
    for (const client of this.watchClients) client.write(line);
  }

  /** Writes a raw line to watch streams (for framing/error-line tests). */
  pushWatchRaw(chunk: string): void {
    for (const client of this.watchClients) client.write(chunk);
  }

  closeWatchStreams(): void {
    for (const client of this.watchClients) client.end();
    this.watchClients.clear();
  }

  requestsTo(path: string): RecordedRequest[] {
    return this.requests.filter((r) => r.path === path);
  }

  private async handle(req: IncomingMessage, res: ServerResponse): Promise<void> {
    const path = req.url ?? '';
    const body = await readBody(req);
    this.requests.push({ method: req.method ?? '', path, body });

    if (this.delayMs > 0) await sleep(this.delayMs);
    if (this.failNextRequests > 0) {
      this.failNextRequests--;
      return sendGrpcError(res, 503, 14, 'unavailable');
    }

    if (req.method === 'GET' && path === '/gameserver') {
      return sendJson(res, 200, this.wireGameServer());
    }
    if (
      req.method === 'POST' &&
      (path === '/ready' || path === '/health' || path === '/shutdown')
    ) {
      if (path === '/ready') this.setState('Ready');
      if (path === '/shutdown') this.setState('Shutdown');
      return sendJson(res, 200, {});
    }
    if (req.method === 'GET' && path === '/watch/gameserver') {
      res.writeHead(200, { 'content-type': 'application/json' });
      res.write(JSON.stringify({ result: toWire(this.gameServer) }) + '\n');
      this.watchClients.add(res);
      res.on('close', () => this.watchClients.delete(res));
      return;
    }

    const listMatch = path.match(/^\/v1beta1\/lists\/([^:/]+)(?::(addValue|removeValue))?$/);
    if (listMatch) {
      const [, name, verb] = listMatch;
      return this.handleList(req, res, name!, verb, body);
    }

    sendGrpcError(res, 404, 5, `unknown path ${path}`);
  }

  private handleList(
    req: IncomingMessage,
    res: ServerResponse,
    name: string,
    verb: string | undefined,
    body: string
  ): void {
    const list = this.lists.get(name);
    if (!list) return sendGrpcError(res, 404, 5, `list ${name} not found`);

    if (req.method === 'GET' && verb === undefined) {
      return sendJson(res, 200, this.wireList(name, list));
    }

    const value = (JSON.parse(body || '{}') as { value?: string }).value ?? '';
    if (req.method === 'POST' && verb === 'addValue') {
      if (list.values.includes(value)) {
        return sendGrpcError(res, 409, 6, `value ${value} already in list ${name}`);
      }
      if (list.values.length >= list.capacity) {
        return sendGrpcError(res, 400, 11, `list ${name} is at capacity ${list.capacity}`);
      }
      list.values.push(value);
      this.syncGameServerLists();
      return this.sendMutationResult(res, name, list);
    }
    if (req.method === 'POST' && verb === 'removeValue') {
      const index = list.values.indexOf(value);
      if (index < 0) return sendGrpcError(res, 404, 5, `value ${value} not found in list ${name}`);
      list.values.splice(index, 1);
      this.syncGameServerLists();
      return this.sendMutationResult(res, name, list);
    }

    sendGrpcError(res, 404, 5, 'unsupported list operation');
  }

  private sendMutationResult(res: ServerResponse, name: string, list: FakeList): void {
    if (this.emptyMutationBody) {
      res.writeHead(200, { 'content-type': 'application/json' });
      res.end();
      return;
    }
    sendJson(res, 200, this.wireList(name, list));
  }

  private setState(state: string): void {
    this.gameServer = { ...this.gameServer, status: { ...this.gameServer.status, state } };
    this.pushWatchUpdate();
  }

  private syncGameServerLists(): void {
    const lists: Record<string, { capacity: string; values: string[] }> = {};
    for (const [name, list] of this.lists) lists[name] = this.wireList(name, list);
    this.gameServer = { ...this.gameServer, status: { ...this.gameServer.status, lists } };
  }

  private wireGameServer(): RawGameServer {
    this.syncGameServerLists();
    return toWire(this.gameServer);
  }

  // int64 capacity goes over the wire as a string, like grpc-gateway does
  private wireList(_name: string, list: FakeList): { capacity: string; values: string[] } {
    return { capacity: String(list.capacity), values: [...list.values] };
  }
}

// The real gateway marshals with UseProtoNames: the game server goes over
// the wire as `object_meta`, never `objectMeta`. Tests keep mutating the
// camelCase property; only the serialized responses use proto names.
function toWire(gs: RawGameServer): RawGameServer {
  const { objectMeta, ...rest } = gs;
  return { ...rest, object_meta: objectMeta };
}

function sendJson(res: ServerResponse, status: number, payload: unknown): void {
  res.writeHead(status, { 'content-type': 'application/json' });
  res.end(JSON.stringify(payload));
}

function sendGrpcError(res: ServerResponse, status: number, code: number, message: string): void {
  sendJson(res, status, { code, message, details: [] });
}

function readBody(req: IncomingMessage): Promise<string> {
  return new Promise((resolve) => {
    let data = '';
    req.on('data', (chunk: Buffer) => (data += chunk.toString()));
    req.on('end', () => resolve(data));
  });
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}
