import test from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { HardwareController, programLines } from '../WebApp/public/hardware.js';
import { createThreeEspDemo } from '../WebApp/demo-three-esp.js';
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
async function setup(t, delay = 20) {
  const server = createThreeEspDemo({ delay }).listen(0, '127.0.0.1');
  await once(server, 'listening'); t.after(() => server.close());
  const base = 'http://127.0.0.1:' + server.address().port;
  const client = new HardwareController(base, fetch, 'test-phone');
  await client.refresh(); await client.claim();
  return { server, base, client };
}
test('three-ESP API: exclusive control, X1/X2 routing, diagnostics, and rejected axes', async t => {
  const { client, base } = await setup(t);
  const observer = new HardwareController(base, fetch, 'other-phone'); await observer.refresh();
  await assert.rejects(observer.claim(), /Another phone/);
  await assert.rejects(client.execute('G90 X1 1 F180'), /Home/);
  await client.execute('G28 X1 X2');
  const moved = await client.execute('G90 X1 10 X2 35 F350');
  assert.equal(moved.a.positions[0], 10); assert.equal(moved.b.positions[0], 35);
  await client.execute('G90 X2 2 F180');
  assert.equal(client.status.a.positions[0], 10); assert.equal(client.status.b.positions[0], 2);
  await assert.rejects(client.execute('G90 Y1 2 F180'), /not installed/);
  await assert.rejects(client.execute('G90 X1 49 F180'), /travel/);
  await client.execute('M119');
  assert.ok(client.events.some(event => event.text.includes('x_min')));
  await observer.stop();
  await assert.rejects(client.execute('G90 X1 1 F180'), /ready/);
  await client.post('/api/release'); await observer.claim();
  assert.equal(observer.ownsControl(), true);
});
test('program upload is validated before playback; stored playback needs no phone polling', async t => {
  const { client } = await setup(t);
  const lines = programLines('; test\nG28 X1 X2\nG90 X1 10 X2 35 F350\nG90 X1 0 X2 0 F180');
  const begin = await client.post('/api/program/begin', { total: lines.length });
  const program = begin.program.id;
  await assert.rejects(client.post('/api/program/start', { program }), /whole program/);
  await assert.rejects(client.post('/api/program/append', { program, index: 0, gcode: 'G1 Y2 1' }), /not installed/);
  for (const [index, line] of lines.entries())
    await client.post('/api/program/append', { program, index, gcode: line.text });
  assert.equal(client.status.program.state, 'ready');
  await client.post('/api/program/start', { program });
  await wait(150);
  await client.refresh();
  assert.equal(client.status.program.state, 'done');
  assert.equal(client.status.program.completed, 3);
  assert.equal(client.status.a.positions[0], 0); assert.equal(client.status.b.positions[0], 0);
});
test('stop or bridge fault prevents program replay', async t => {
  const { client, server } = await setup(t, 100);
  const { program: { id: program } } = await client.post('/api/program/begin', { total: 2 });
  await client.post('/api/program/append', { program, index: 0, gcode: 'G28 X1 X2' });
  await client.post('/api/program/append', { program, index: 1, gcode: 'G90 X1 10 X2 35 F350' });
  await client.post('/api/program/start', { program }); server.fault('Board 2 link lost');
  await wait(250); await client.refresh();
  assert.equal(client.status.program.state, 'faulted');
  assert.equal(client.status.a.positions[0], 0);
  await assert.rejects(client.post('/api/program/start', { program }), /whole program/);
});
test('hardware client sends lease fencing and never retries a failed motion POST', async () => {
  let posts = 0;
  const client = new HardwareController('', async (_url, options) => {
    const body = JSON.parse(options.body); ++posts;
    assert.equal(body.client, 'phone'); assert.equal(body.fence, 7); assert.equal(body.seq, 1);
    assert.equal(body.gcode, 'G90 X1 1 F180'); throw Error('Connection lost');
  }, 'phone');
  client.status = { session: 'boot', control: { owner: 'phone', fence: 7 } };
  await assert.rejects(client.execute('G90 X1 1 F180'), /Connection lost/);
  assert.equal(posts, 1);
});
test('program input preserves source lines and rejects empty/oversized files', () => {
  assert.deepEqual(programLines('; hi\n\nG28 X1 X2\r\nG90 X1 1 F180').map(line => line.number), [3, 4]);
  assert.throws(() => programLines('; nothing'), /executable/);
  assert.throws(() => programLines('G1 ' + 'X'.repeat(241)), /240/);
});
