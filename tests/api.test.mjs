import test from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { createDemoServer } from '../WebApp/server.js';
import { Controller } from '../WebApp/public/control.js';

test('HTTP workflow: homing, isolated arm routing, deduplication, and stop', async t => {
  const server = createDemoServer({ delay: 20 }).listen(0, '127.0.0.1');
  await once(server, 'listening'); t.after(() => server.close());
  const base = `http://127.0.0.1:${server.address().port}`, client = new Controller(base);
  assert.equal((await client.refresh()).mode, 'demo');
  await client.execute({ a: ['G28'], b: ['G28'] });
  const moved = await client.execute({ a: [], b: ['G90', 'G1 C4 F180'] });
  assert.deepEqual(moved.a.positions, [0, 0, 0, 0, 0, 0]);
  assert.deepEqual(moved.b.positions, [0, 0, 0, 0, 0, 4]);
  const duplicate = await client.request('/api/command', { session: moved.session, id: moved.operation.id, a: [], b: ['G91', 'G1 C4 F180'] });
  assert.equal(duplicate.operation.state, 'done'); assert.equal(duplicate.b.positions[5], 4);
  const pending = client.execute({ a: ['G1 X10'], b: [] });
  await new Promise(resolve => setTimeout(resolve, 5)); await client.stop();
  await assert.rejects(pending, /Operator stop/);
  assert.equal((await client.refresh()).a.ready, false);
  await assert.rejects(client.execute({ a: ['G1 X1'], b: [] }), /offline or stopped/);
  assert.equal((await fetch(base + '/api/recover', { method: 'POST', headers: { 'X-Timiac': '1' }, body: '{}' })).status, 404);
});
test('HTTP validation blocks malformed requests, stale sessions, and missing header', async t => {
  const server = createDemoServer().listen(0, '127.0.0.1');
  await once(server, 'listening'); t.after(() => server.close());
  const base = `http://127.0.0.1:${server.address().port}`, client = new Controller(base);
  await client.refresh();
  assert.equal((await fetch(base + '/api/stop', { method: 'POST', body: '{}' })).status, 403);
  await assert.rejects(client.request('/api/command', { session: 'stale', id: '1', a: ['M114'], b: [] }), /restarted/);
  await assert.rejects(client.request('/api/command', { session: client.status.session, id: '1', a: ['G1 X1\nM112'], b: [] }));
  assert.equal((await fetch(base + '/../server.js')).status, 404);
  assert.match(await (await fetch(base)).text(), /TIMIAC/);
});
