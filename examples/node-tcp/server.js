// Minimal GameFlow game server: a TCP line-echo chat built only on node:net
// and the GameFlow SDK. Connect with `nc <host> <port>` and type lines.
// Commands: /who lists connected players, /quit disconnects.
import { createServer } from 'node:net';
import { randomUUID } from 'node:crypto';

import { GameFlow, ServerFullError } from '@gameflow.gg/gameserver-sdk';

const gf = await GameFlow.connect();
console.log(`[server] mode=${gf.mode} region=${gf.region ?? '-'} build=${gf.buildId ?? '-'}`);

const payload = await gf.payload();
if (payload !== undefined) console.log(`[server] launch payload: ${payload}`);
gf.onPayloadChange((next) => console.log(`[server] payload changed: ${next ?? '(cleared)'}`));

const sockets = new Set();

const server = createServer(async (socket) => {
  const sessionId = randomUUID();
  try {
    await gf.players.connect(sessionId);
  } catch (error) {
    socket.end(error instanceof ServerFullError ? 'server full\n' : 'cannot join\n');
    return;
  }

  sockets.add(socket);
  socket.write(`welcome ${sessionId} (${gf.players.count()}/${gf.players.capacity()} players)\n`);

  socket.on('data', (data) => {
    const line = data.toString().trim();
    if (line === '/quit') return socket.end('bye\n');
    if (line === '/who') return socket.write(`players: ${gf.players.list().join(', ')}\n`);
    for (const other of sockets) if (other !== socket) other.write(`${sessionId}: ${line}\n`);
  });

  socket.on('close', () => {
    sockets.delete(socket);
    gf.players.disconnect(sessionId).catch((error) => console.error('[server]', error));
  });
  socket.on('error', () => socket.destroy());
});

const port = gf.ports.default ?? 7777;
server.listen(port, async () => {
  console.log(`[server] listening on :${port}`);
  // Only call ready() once the server can actually accept connections.
  await gf.ready();
});

// The platform sends SIGTERM and force-kills the container ~45s later:
// close the listener, drain, and call shutdown() well within that budget.
async function stop(signal) {
  console.log(`[server] ${signal} received, shutting down`);
  server.close();
  for (const socket of sockets) socket.end('server shutting down\n');
  await gf.shutdown();
  process.exit(0);
}
process.on('SIGTERM', () => void stop('SIGTERM'));
process.on('SIGINT', () => void stop('SIGINT'));
