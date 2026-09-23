import test from 'node:test';
import assert from 'node:assert/strict';
import { readdir, readFile } from 'node:fs/promises';
import { armAddress, moveArm, homeArm, nativeCommand, compileProgram, Controller } from '../WebApp/public/control.js';

test('all twelve arms route to exactly one board and axis', () => {
  assert.deepEqual(Array.from({ length: 12 }, (_, i) => armAddress(i + 1)),
    [...'XYZABCXYZABC'].map((axis, i) => ({ axis, board: i < 6 ? 'a' : 'b' })));
  assert.deepEqual(moveArm(7, 4, 180), { a: [], b: ['G21', 'G90', 'G1 X4 F180'] });
  assert.deepEqual(homeArm(12), { a: [], b: ['G28 C'] });
  for (const arm of [0, 13, 1.5, NaN]) assert.throws(() => armAddress(arm));
});
test('manual moves reject travel, feed and non-finite values', () => {
  for (const value of [-1, 49, NaN, Infinity]) assert.throws(() => moveArm(1, value, 180));
  for (const value of [0, -1, 701, NaN]) assert.throws(() => moveArm(1, 3, value));
});
test('simulator remaps all twelve arms while reserving F for feed rate', () => {
  const [step] = compileProgram('G1 A10 B20 C30 D40 E50 H60 I70 J80 K90 L100 U0 V5 F600', 'simulator');
  assert.deepEqual(step.a, ['G21', 'G90', 'G1 X43.2 Y38.4 Z33.6 A28.8 B24 C19.2 F288']);
  assert.deepEqual(step.b, ['G21', 'G90', 'G1 X14.4 Y9.6 Z4.8 A0 B48 C45.6 F288']);
  assert.equal(compileProgram('G1 H25 F300', 'simulator')[0].a.at(-1), 'G1 C36 F144');
  assert.equal(compileProgram('G1 A10 H20 F600', 'simulator')[0].a.at(-1), 'G1 X43.2 C38.4 F288');
});
test('relative coordinates need a known origin and remain in range', () => {
  assert.throws(() => compileProgram('G91\nG1 A2', 'simulator'), /needs G28/);
  const steps = compileProgram('G28\nG91\nG1 A-5 I-5 F600\nG1 A2 I2', 'simulator');
  assert.equal(steps[2].a.at(-1), 'G1 X1.44 F288');
  assert.equal(steps[2].b.at(-1), 'G1 X1.44 F288');
  assert.throws(() => compileProgram('G28\nG91\nG1 A1', 'simulator'), /outside/);
  assert.equal(compileProgram('G1 A20\nG91\nG1 A10', 'simulator')[1].a.at(-1), 'G1 X33.6 F288');
});
test('unsupported and malformed simulator commands fail before any transmission', () => {
  for (const source of ['G1 A101', 'G1 A10 !', 'G1 A1 A2', 'G28 A', 'G4 P-2', 'G4 S121', 'G4 P1 S2', 'M24', 'G90 A2', 'G1 H1 F1 F300'])
    assert.throws(() => compileProgram(source, 'simulator'), /Line 1:/);
  assert.deepEqual(compileProgram('G4 S2', 'simulator')[0].a, ['G4 P2000']);
});
test('native commands never remap axes and reject line injection / special resets', () => {
  assert.deepEqual(nativeCommand('g1 x3 f180 ; hi', 'b'), { a: [], b: ['G1 X3 F180'] });
  for (const line of ['G1 X1\nM112', 'M112', 'M0112', 'M999', 'M110', 'G1 X1*44']) assert.throws(() => nativeCommand(line, 'a'));
  assert.equal(nativeCommand('; comment', 'a'), null);
});
test('every existing simulator study compiles without out-of-range hardware output', async () => {
  const folder = new URL('../Simulator/gcodes/', import.meta.url);
  for (const filename of await readdir(folder)) {
    if (!filename.endsWith('.gcode')) continue;
    const steps = compileProgram(await readFile(new URL(filename, folder), 'utf8'), 'simulator');
    assert.ok(steps.length > 0, filename);
    for (const step of steps) for (const board of ['a', 'b']) for (const line of step[board]) {
      assert.ok(line.length <= 160, filename);
      if (line.startsWith('G1 ')) {
        for (const [, , value] of line.matchAll(/([XYZABC])([\d.]+)/g)) assert.ok(Number(value) <= 48);
      }
    }
  }
});
test('API client stops on reboot, failure and replaced command, and never retries POST', async () => {
  for (const reason of ['reboot', 'failed', 'replaced']) {
    let posts = 0;
    const client = new Controller('', async (_url, options = {}) => {
      const sent = JSON.parse(options.body || '{}');
      if (options.method === 'POST') posts++;
      return new Response(JSON.stringify({ session: reason === 'reboot' ? 'new' : 'old', operation: {
        id: reason === 'replaced' ? 'different' : sent.id,
        state: reason === 'failed' ? 'failed' : 'done', error: 'Motor fault',
      } }));
    });
    client.status = { session: 'old' };
    await assert.rejects(client.execute({ a: ['M114'], b: [] }));
    assert.equal(posts, 1);
  }
});
