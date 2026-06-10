#!/usr/bin/env node
// Serves the fake GameFlow runtime fixture standalone so non-Node SDKs can
// run conformance tests against it (requires Node >= 23.6 for type stripping).
//
//   node tools/conformance/serve.mjs [--players-capacity=N] [--players-seed=a,b]
//                                    [--fail-first=N] [--payload=str]
//
// Prints PORT=<sidecar port> and CONTROL_PORT=<control port>, then runs until
// killed. The control server lets test harnesses inspect and poke the fixture:
//   GET  /requests            -> JSON array of every recorded request
//   POST /push-update         -> push the current state to watch streams
//   POST /set-payload?value=x -> set the launch payload annotation and push

import { createServer } from 'node:http';

import { FakeSidecar } from './fake-sidecar.ts';

const args = Object.fromEntries(
  process.argv.slice(2).map((arg) => {
    const match = arg.match(/^--([^=]+)(?:=(.*))?$/);
    if (!match) throw new Error(`unrecognized argument: ${arg}`);
    return [match[1], match[2] ?? 'true'];
  })
);

const sidecar = new FakeSidecar();
if (args['players-capacity'] !== undefined) {
  const seed = args['players-seed'] ? args['players-seed'].split(',') : [];
  sidecar.withPlayersList(Number(args['players-capacity']), seed);
}
if (args['fail-first'] !== undefined) sidecar.failNextRequests = Number(args['fail-first']);
if (args['payload'] !== undefined) setPayload(args['payload']);

function setPayload(value) {
  sidecar.gameServer.objectMeta ??= {};
  sidecar.gameServer.objectMeta.annotations ??= {};
  sidecar.gameServer.objectMeta.annotations.GAMEFLOW_PAYLOAD = value;
}

const port = await sidecar.start();

const control = createServer((req, res) => {
  const url = new URL(req.url ?? '/', 'http://127.0.0.1');
  if (req.method === 'GET' && url.pathname === '/requests') {
    res.writeHead(200, { 'content-type': 'application/json' });
    res.end(JSON.stringify(sidecar.requests));
    return;
  }
  if (req.method === 'POST' && url.pathname === '/push-update') {
    sidecar.pushWatchUpdate();
    res.writeHead(204);
    res.end();
    return;
  }
  if (req.method === 'POST' && url.pathname === '/set-payload') {
    setPayload(url.searchParams.get('value') ?? '');
    sidecar.pushWatchUpdate();
    res.writeHead(204);
    res.end();
    return;
  }
  res.writeHead(404);
  res.end();
});
await new Promise((resolve) => control.listen(0, '127.0.0.1', resolve));

console.log(`PORT=${port}`);
console.log(`CONTROL_PORT=${control.address().port}`);
