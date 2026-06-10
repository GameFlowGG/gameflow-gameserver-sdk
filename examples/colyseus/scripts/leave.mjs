// Disconnects the most recently joined test player (see scripts/join.mjs).
//
//   node scripts/leave.mjs        # disconnect last joined player
//   node scripts/leave.mjs all    # disconnect every test player
import { existsSync, readFileSync, rmSync, writeFileSync } from 'node:fs';

const PID_FILE = '/tmp/gameflow-test-players.pids';

if (!existsSync(PID_FILE)) {
  console.log('no test players connected (pid file not found)');
  process.exit(0);
}

const pids = readFileSync(PID_FILE, 'utf8').split('\n').filter(Boolean).map(Number);
if (pids.length === 0) {
  console.log('no test players connected');
  rmSync(PID_FILE, { force: true });
  process.exit(0);
}

const toDisconnect = process.argv[2] === 'all' ? pids : [pids[pids.length - 1]];
const remaining = pids.filter((pid) => !toDisconnect.includes(pid));

for (const pid of toDisconnect) {
  try {
    process.kill(pid, 'SIGTERM');
    console.log(`sent disconnect to player process ${pid}`);
  } catch {
    console.log(`player process ${pid} was already gone`);
  }
}

if (remaining.length === 0) rmSync(PID_FILE, { force: true });
else writeFileSync(PID_FILE, remaining.join('\n') + '\n');
