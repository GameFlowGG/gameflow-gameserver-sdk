import { Server } from 'colyseus';

import { gf } from './gameflow.js';
import { GameRoom } from './rooms.js';

// GAMEFLOW_DEFAULT_PORT is set by the platform; PORT comes from the build
// template; 8081 is the local-development fallback.
const port = gf.ports.default ?? Number(process.env.PORT ?? 8081);

const gameServer = new Server({ gracefullyShutdown: false });
gameServer.define('game', GameRoom);

await gameServer.listen(port);
console.log(`[server] colyseus listening on :${port} (mode: ${gf.mode})`);

const payload = await gf.payload();
if (payload !== undefined) console.log(`[server] launch payload: ${payload}`);

// Only signal readiness once the server is actually accepting connections.
await gf.ready();

// The platform sends SIGTERM and force-kills ~45s later: drain and shut down.
async function stop(signal: string): Promise<void> {
  console.log(`[server] ${signal} received, shutting down`);
  await gameServer.gracefullyShutdown(false);
  await gf.shutdown();
  process.exit(0);
}
process.on('SIGTERM', () => void stop('SIGTERM'));
process.on('SIGINT', () => void stop('SIGINT'));
