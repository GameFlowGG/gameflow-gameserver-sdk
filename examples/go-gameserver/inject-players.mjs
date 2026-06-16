#!/usr/bin/env node
// Injects fake players into the Go GameFlow server by opening TCP connections.
// Each open connection is one tracked player, so you'll see the player count
// climb in the GameFlow dashboard.
//
// Usage:
//   node inject-players.mjs <host> <port> [count=5] [hold-seconds=120]
//
// Example (host/port are what GameFlow shows for your allocated server):
//   node inject-players.mjs 203.0.113.10 31234 8 120
//
// Press Ctrl-C to disconnect everyone early (you'll see the count drop too).

import net from 'node:net';

const [host, portArg, countArg, holdArg] = process.argv.slice(2);
if (!host || !portArg) {
  console.error('usage: node inject-players.mjs <host> <port> [count=5] [hold-seconds=120]');
  process.exit(1);
}
const port = Number(portArg);
const count = Number(countArg ?? 5);
const holdMs = Number(holdArg ?? 120) * 1000;

console.log(`Injecting ${count} players into ${host}:${port}, holding ${holdMs / 1000}s...`);

const sockets = [];

function connectPlayer(i) {
  const socket = net.createConnection({ host, port }, () => {
    console.log(`[player ${i}] connected`);
    // Say hi so we're a real chat client; the server broadcasts it.
    socket.write(`hi from player ${i}\n`);
  });
  socket.setEncoding('utf8');
  socket.on('data', (chunk) => {
    for (const line of chunk.split('\n')) {
      if (line.trim()) console.log(`[player ${i}] < ${line.trim()}`);
    }
  });
  socket.on('error', (err) => console.error(`[player ${i}] error: ${err.message}`));
  socket.on('close', () => console.log(`[player ${i}] disconnected`));
  sockets.push(socket);
}

for (let i = 1; i <= count; i++) {
  // Stagger slightly so the joins are visible one by one.
  setTimeout(() => connectPlayer(i), i * 250);
}

function shutdown() {
  console.log('\nDisconnecting all players...');
  for (const socket of sockets) socket.end();
  setTimeout(() => process.exit(0), 500);
}

process.on('SIGINT', shutdown);
setTimeout(shutdown, holdMs + count * 250);
