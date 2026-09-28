import test from 'node:test';
import assert from 'node:assert/strict';
import { driverRows, driverIndicators } from '../WebApp/public/driver-panel.js';
const reported = { currentMa: [800, 800, 800, 800, 800, 800, 800], microstepValues: [64, 64, 64, 64, 64, 64] };
import { boardStatus } from '../WebApp/public/system-panel.js';

test('driver checks identify the exact failed socket and the old Z/ABC mode mismatch', () => {
  const board = { octopusConnected: true, drivers: { ...reported, ageMs: 0, tested: 63, connected: 59, columns: true, settings: 7, current: 63, microsteps: 63, mode: 3 } };
  const rows = driverRows(board, true);
  assert.deepEqual(rows.map(r => r.socket), [0, 1, 2, 3, 4, 5]);
  assert.equal(rows[2].uart, 'No response');
  assert.equal(rows[0].mode, 'Quiet idle / SpreadCycle motion');
  for (const field of ['current', 'microsteps', 'mode']) assert.equal(rows[2][field], 'Unknown');
  for (let a = 3; a < 6; ++a) assert.equal(rows[a].mode, 'Mismatch');
  for (const row of driverRows(board, false)) assert.equal(row.uart, 'Unknown');
  board.octopusConnected = false;
  for (const row of driverRows(board, true)) assert.equal(row.mode, 'Unknown');
});
test('an unresponsive driver cannot show cached current or a zero-read mode as passing', () => {
  const rows = driverRows({ octopusConnected: true, drivers: { ...reported,
    ageMs: 0, tested: 63, connected: 59, columns: true, settings: 7, current: 63, microsteps: 59, mode: 63,
  } }, true);
  assert.deepEqual(rows[2], { axis: 'Z', socket: 2, uart: 'No response', current: 'Unknown', microsteps: 'Unknown', mode: 'Unknown' });
  assert.equal(rows[0].current, '800 mA');
  assert.equal(rows[0].microsteps, '64');
  assert.equal(rows[0].mode, 'Quiet idle / SpreadCycle motion');
});
test('live checks change a replaced driver from failed to passing and expire old readings', () => {
  const board = { bridgeConnected: true, octopusConnected: true, drivers: { ...reported,
    ageMs: 25, tested: 63, connected: 59, columns: true, settings: 7, current: 63, microsteps: 59, mode: 63,
  } };
  assert.equal(driverIndicators(board, true)[2].state, 'fault');
  assert.equal(driverIndicators(board, true)[0].state, 'ok');
  board.drivers = { ...board.drivers, connected: 63, microsteps: 63, ageMs: 0 };
  assert.equal(driverIndicators(board, true)[2].result, 'Checks passed');
  board.drivers.mode = 55;
  assert.equal(driverIndicators(board, true)[3].state, 'warning');
  board.drivers.ageMs = 15000;
  for (const indicator of driverIndicators(board, true)) assert.equal(indicator.state, 'unknown');
  board.drivers.ageMs = 0; board.busy = true;
  assert.match(driverIndicators(board, true)[0].detail, /paused during motion/);
  board.busy = false; board.octopusConnected = false;
  assert.match(driverIndicators(board, true)[0].detail, /Octopus offline/);
  board.octopusConnected = true; board.drivers.ageMs = null;
  assert.match(driverIndicators(board, true)[0].detail, /complete driver report/);
  delete board.drivers.ageMs;
  assert.match(driverIndicators(board, true)[0].detail, /Update the ESP bridge/);
});
test('firmware mismatch is visible even when the mismatched bridge is refused', () => {
  assert.equal(boardStatus({ a: { bridgeConnected: false, fault: 'ESP firmware mismatch: flash bridge' } }, 'a', true).label, 'Firmware mismatch');
});

test('driver table uses received values even when browser expectations are stale', () => {
  const board = { octopusConnected: true, drivers: { ...reported, ageMs: 0,
    tested: 63, connected: 63, columns: true, settings: 7, current: 63, microsteps: 63, mode: 63,
    currentMa: [600, 800, 800, 800, 800, 800], microstepValues: [32, 64, 64, 64, 64, 64] } };
  assert.equal(driverRows(board, true)[0].current, '600 mA');
  assert.equal(driverRows(board, true)[0].microsteps, '32');
  board.drivers.current = 62; board.drivers.microsteps = 62;
  assert.equal(driverRows(board, true)[0].current, '600 mA (mismatch)');
  assert.equal(driverRows(board, true)[0].microsteps, '32 (mismatch)');
  assert.equal(driverIndicators(board, true)[0].state, 'warning');
  delete board.drivers.currentMa; delete board.drivers.microstepValues;
  assert.equal(driverRows(board, true)[0].current, 'Unknown');
  assert.equal(driverRows(board, true)[0].microsteps, 'Unknown');
});
