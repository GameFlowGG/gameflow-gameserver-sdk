// Simulates a player joining the server and stays connected until told to
// leave (scripts/leave.mjs) or Ctrl+C.
//
//   node scripts/join.mjs [ws://127.0.0.1:8081]
import { appendFileSync } from 'node:fs';

import { Client } from 'colyseus.js';

const PID_FILE = '/tmp/gameflow-test-players.pids';
const endpoint = process.argv[2] ?? 'ws://127.0.0.1:8081';

console.log(`connecting to ${endpoint} ...`);
const client = new Client(endpoint);
const room = await client.joinOrCreate('game');
console.log(`joined as ${room.sessionId} (pid ${process.pid}); run scripts/leave.mjs to disconnect`);
appendFileSync(PID_FILE, `${process.pid}\n`);

room.onMessage('chat', (message) => console.log('chat:', JSON.stringify(message)));
room.onLeave(() => {
  console.log(`${room.sessionId} left the server`);
  process.exit(0);
});

const leave = () => {
  console.log(`disconnecting ${room.sessionId} ...`);
  room.leave();
  setTimeout(() => process.exit(0), 2000).unref();
};
process.on('SIGINT', leave);
process.on('SIGTERM', leave);

setInterval(() => {}, 60_000); // keep the process alive while connected
