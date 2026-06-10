#!/usr/bin/env node
// Godot SDK test runner: runs the local-mode suite, then the conformance
// suite against the fake runtime fixture (tools/conformance/serve.mjs).
//
//   node sdk/godot/test/run.mjs
//
// Requires a Godot 4 binary on PATH (or set GODOT=/path/to/godot).

import { spawn } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const testDir = path.dirname(fileURLToPath(import.meta.url));
const projectDir = path.resolve(testDir, '..');
const repoRoot = path.resolve(testDir, '../../..');
const godot = process.env.GODOT ?? 'godot';

function runGodot(script, env = {}) {
  return new Promise((resolve) => {
    const child = spawn(godot, ['--headless', '--path', projectDir, '--script', script], {
      stdio: 'inherit',
      env: { ...process.env, ...env },
    });
    child.on('error', (error) => {
      console.error(`could not launch godot ("${godot}"): ${error.message}`);
      console.error('install Godot 4 or point the GODOT env var at the binary');
      resolve(127);
    });
    child.on('exit', (code) => resolve(code ?? 1));
  });
}

function startFakeSidecar(args) {
  return new Promise((resolve, reject) => {
    const child = spawn(
      process.execPath,
      [path.join(repoRoot, 'tools/conformance/serve.mjs'), ...args],
      { stdio: ['ignore', 'pipe', 'inherit'] }
    );
    let output = '';
    child.stdout.on('data', (data) => {
      output += data;
      const port = output.match(/^PORT=(\d+)$/m);
      const controlPort = output.match(/^CONTROL_PORT=(\d+)$/m);
      if (port && controlPort) {
        resolve({ child, port: Number(port[1]), controlPort: Number(controlPort[1]) });
      }
    });
    child.on('exit', (code) => reject(new Error(`fake sidecar exited early (code ${code})`)));
  });
}

function importProject() {
  // Build the project cache (.godot/, including the global class_name
  // registry) so --script runs can resolve GameFlow & friends.
  return new Promise((resolve, reject) => {
    const child = spawn(godot, ['--headless', '--path', projectDir, '--import'], {
      stdio: 'ignore',
    });
    child.on('error', (error) => reject(error));
    child.on('exit', () => resolve());
  });
}

let failures = 0;

await importProject();

console.log('--- local mode suite ---');
if ((await runGodot('res://test/local_test.gd')) !== 0) failures++;

console.log('--- conformance suite (fake runtime) ---');
const sidecar = await startFakeSidecar([
  '--players-capacity=2',
  '--players-seed=seeded',
  '--fail-first=2',
]);
try {
  const code = await runGodot('res://test/sidecar_test.gd', {
    AGONES_SDK_HTTP_PORT: String(sidecar.port),
    GF_TEST_CONTROL_PORT: String(sidecar.controlPort),
    GAMEFLOW_SDK_MODE: '',
  });
  if (code !== 0) failures++;

  // Assert behavior only observable from the runtime side.
  const requests = await (await fetch(`http://127.0.0.1:${sidecar.controlPort}/requests`)).json();
  const count = (p) => requests.filter((r) => r.path === p).length;
  const checks = [
    [count('/ready') === 1, `/ready posted exactly once (got ${count('/ready')})`],
    [count('/health') >= 2, `health heartbeat ticked (got ${count('/health')} pings)`],
    [count('/shutdown') === 1, `/shutdown posted exactly once (got ${count('/shutdown')})`],
    [count('/gameserver') >= 3, `connect retried through failures (got ${count('/gameserver')})`],
  ];
  for (const [ok, label] of checks) {
    console.log(`  ${ok ? 'ok' : 'FAIL'} - ${label}`);
    if (!ok) failures++;
  }
} finally {
  sidecar.child.kill();
}

console.log(failures === 0 ? 'godot SDK tests passed' : `godot SDK tests FAILED (${failures})`);
process.exit(failures === 0 ? 0 : 1);
