import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import { readFileSync } from 'node:fs';
import { localDate, clockActionNeedsRtc, renderClockReadouts } from '../WebApp/public/clock-panel.js';
import { renderSystemStatus, bindWifiSettings } from '../WebApp/public/system-panel.js';
import { renderEndstops } from '../WebApp/public/endstop-panel.js';
import { renderDrivers } from '../WebApp/public/driver-panel.js';

const flush = () => new Promise(resolve => setImmediate(resolve));
async function dashboard() {
  const html = readFileSync(new URL('../WebApp/public/index.html', import.meta.url), 'utf8');
  const nodes = [], ids = new Map();
  function node() {
    const classes = new Set(), children = new Map();
    return { textContent: '', value: '', disabled: false, hidden: false, dataset: {}, attrs: {},
      setAttribute(name, value) { this.attrs[name] = value; },
      classList: { add: name => classes.add(name), remove: name => classes.delete(name), toggle(name, on) { on ? classes.add(name) : classes.delete(name); } },
      querySelector(selector) { if (!children.has(selector)) children.set(selector, node()); return children.get(selector); } };
  }
  for (const [, , attributes] of html.matchAll(/<([a-z][\w-]*)\b([^>]*)>/g)) {
    const element = node(); nodes.push(element);
    for (const [, name, value = ''] of attributes.matchAll(/([\w-]+)(?:="([^"]*)")?/g)) {
      element.attrs[name] = value;
      if (name === 'id') ids.set(value, element);
      if (name.startsWith('data-')) element.dataset[name.slice(5).replace(/-([a-z])/g, (_, c) => c.toUpperCase())] = value;
    }
  }
  const element = id => { assert.ok(ids.has(id), `Missing HTML element: ${id}`); return ids.get(id); };
  const actions = Object.fromEntries(nodes.filter(n => n.dataset.clockAction).map(n => [n.dataset.clockAction, n]));
  const board = () => ({ online: true, ready: true, manualReady: true, homed: 63, positions: [0, 0, 0, 0, 0, 0] });
  const state = { mode: 'hardware', machineReady: true, control: {}, axesPerBoard: 6, travelMm: 48, maxFeed: 700,
    a: board(), b: board(), operation: { state: 'idle' }, program: { state: 'idle', total: 0, completed: 0 },
    clock: { present: true, valid: true, localTime: '2026-09-22T13:45:12' },
    proximity: { enabled: true, armed: true, distanceCm: 73.2 } };
  const calls = []; let offline = false;
  class Controller {
    status = state; events = [];
    ownsControl() { return state.control.owner === 'phone'; }
    async refresh() { if (offline) throw new Error('offline'); return state; }
    async claim() { state.control.owner = 'phone'; return state; }
    async post(path, data) { calls.push({ path, data }); return state; }
    async action(name) { calls.push(name); return state; }
    async execute(gcode) { calls.push({ gcode }); return state; }
    async stop() { calls.push('stop'); return state; }
  }
  const context = vm.createContext({ HardwareController: Controller, localDate, clockActionNeedsRtc, renderClockReadouts, renderSystemStatus, bindWifiSettings, renderEndstops, renderDrivers, Date, setTimeout() {},
    document: {
      querySelector(selector) { const [id, child] = selector.slice(1).split(' '); return child ? element(id).querySelector(child) : element(id); },
      querySelectorAll(selector) { const attr = selector.slice(1, -1); return nodes.filter(n => attr in n.attrs); },
    } });
  const source = readFileSync(new URL('../WebApp/public/app.js', import.meta.url), 'utf8').replace(/^import .*;\r?\n/gm, '');
  vm.runInContext(source, context); await flush();
  return { context, element, actions, state, calls, disconnect: () => { offline = true; } };
}

test('manual home and movement work on either board while the other board is disconnected', async () => {
  const { context, state, element, actions, calls } = await dashboard();
  await element('claim').onclick();
  for (const [local, absent, suffix] of [['a', 'b', '1'], ['b', 'a', '2']]) {
    state.machineReady = false;
    Object.assign(state[local], { online: true, ready: true, manualReady: true, homed: 0 });
    Object.assign(state[absent], { online: false, ready: false, manualReady: false, homed: 0 });
    for (const [index, axis] of [...'XYZABC'].entries()) {
      vm.runInContext(`selected = '${axis}${suffix}'; render(api.status)`, context);
      assert.equal(element('home').disabled, false, axis + suffix);
      assert.equal(element('move').disabled, true);
      assert.match(element('manual-status').textContent, /independently/);
      assert.equal(element('home-all').disabled, true);
      assert.equal(element('release').disabled, true);
      for (const button of Object.values(actions)) assert.equal(button.disabled, true);
      await element('home').onclick();
      assert.equal(calls.at(-1).gcode, 'G28 ' + axis + suffix);
      state[local].homed |= 1 << index;
      vm.runInContext('render(api.status)', context);
      assert.equal(element('move').disabled, false);
      element('target').value = '1'; element('feed').value = '180';
      await element('move').onclick();
      assert.equal(calls.at(-1).gcode, `G90 ${axis}${suffix} 1 F180`);
    }
    vm.runInContext(`selected = 'X${suffix === '1' ? '2' : '1'}'; render(api.status)`, context);
    assert.equal(element('home').disabled, true);
    assert.match(element('manual-status').textContent, /offline/);
  }
});

test('independent manual controls still require ownership, readiness, and an idle controller', async () => {
  const { context, state, element, disconnect } = await dashboard();
  assert.equal(element('home').disabled, true);
  await element('claim').onclick();
  state.machineReady = false; state.b.online = false; state.b.manualReady = false;
  const render = () => vm.runInContext('render(api.status)', context);
  for (const value of ['uploading', 'running', 'paused']) {
    state.program.state = value; render(); assert.equal(element('home').disabled, true);
  }
  state.program.state = 'ready'; render(); assert.equal(element('home').disabled, false);
  for (const value of ['preparing', 'running']) {
    state.operation.state = value; render(); assert.equal(element('home').disabled, true);
  }
  state.operation.state = 'done'; state.stopPending = true; render();
  assert.equal(element('home').disabled, true); assert.match(element('manual-status').textContent, /Stop acknowledgement/);
  state.stopPending = false; state.a.manualReady = false; state.a.fault = 'Driver check failed'; render();
  assert.equal(element('home').disabled, true); assert.match(element('manual-status').textContent, /Driver check failed/);
  state.a.fault = ''; state.a.manualReady = true; render(); assert.equal(element('home').disabled, false);
  // A page served against older firmware must not bypass that firmware's gate.
  delete state.a.manualReady; render(); assert.equal(element('home').disabled, true);
  state.a.manualReady = true; disconnect(); await vm.runInContext('poll()', context);
  assert.equal(element('home').disabled, true); assert.equal(element('move').disabled, true);
});

test('all twelve live driver lamps update independently without a control lease or motion request', async () => {
  const { context, state, element, calls, disconnect } = await dashboard();
  state.a = { bridgeConnected: true, octopusConnected: true, drivers: {
    ageMs: 80, tested: 63, connected: 59, columns: true, settings: 7, current: 63, microsteps: 59, mode: 63,
  } };
  state.b = { bridgeConnected: false, octopusConnected: false };
  vm.runInContext('render(api.status)', context);
  assert.equal(element('driver-Z1-status').dataset.state, 'fault');
  assert.match(element('driver-summary-1').textContent, /5 passed · 1 no response/);
  for (const axis of 'XYZABC') assert.equal(element(`driver-${axis}2-status`).dataset.state, 'unknown');
  state.a.drivers.connected = 63; state.a.drivers.microsteps = 63; state.a.drivers.ageMs = 0;
  vm.runInContext('render(api.status)', context);
  assert.equal(element('driver-Z1-status').dataset.state, 'ok');
  assert.equal(element('driver-Z1-result').textContent, 'Checks passed');
  assert.match(element('driver-Z1-status').attrs['aria-label'], /Z1, MOTOR2/);
  for (const number of [1, 2]) for (const [index, axis] of [...'XYZABC'].entries()) {
    assert.equal(element(`driver-${axis}${number}-socket`).textContent, `MOTOR${index}`);
    assert.equal(element(`driver-${axis}${number}-socket-table`).textContent, `MOTOR${index}`);
  }
  assert.equal(calls.length, 0);
  disconnect(); vm.runInContext('poll()', context); await flush();
  for (const number of [1, 2]) for (const axis of 'XYZABC')
    assert.equal(element(`driver-${axis}${number}-status`).dataset.state, 'unknown');
});

test('development clock controls use the same actions and require ownership, homing, and RTC', async () => {
  const { context, element, actions, state, calls } = await dashboard();
  assert.equal(element('clock').textContent, '13:45:12');
  assert.equal(element('sensor-distance').textContent, '73.2 cm');
  assert.equal(actions.open.disabled, true);
  await element('claim').onclick();
  for (const name of ['home', 'open', 'close', 'show-time', 'struggle', 'next-five', 'next-hour']) {
    assert.equal(actions[name].disabled, false, name);
    await actions[name].onclick(); assert.equal(calls.at(-1), name);
  }
  state.a.homed = 0; vm.runInContext('render(api.status)', context);
  assert.equal(actions.home.disabled, false); assert.equal(actions.open.disabled, true);
  state.a.homed = 63; state.clock.valid = false; vm.runInContext('render(api.status)', context);
  assert.equal(actions['show-time'].disabled, true); assert.equal(actions.struggle.disabled, false);
  assert.equal(actions['next-five'].disabled, true);
  state.clock.armMinute = 0; vm.runInContext('render(api.status)', context);
  assert.equal(actions['next-five'].disabled, false);
});

test('development RTC setting posts local time and handles a missing clock', async () => {
  const { context, element, state, calls } = await dashboard();
  await element('claim').onclick();
  element('local-time').value = '2026-09-22T09:30';
  element('time-form').onsubmit({ preventDefault() {} }); await flush();
  assert.equal(calls.at(-1).path, '/api/time');
  assert.equal(calls.at(-1).data.localTime, '2026-09-22T09:30:00');
  state.clock.present = false; state.clock.valid = false; state.clock.error = 'RTC disconnected';
  vm.runInContext('render(api.status)', context);
  assert.equal(element('save-time').disabled, true);
  assert.equal(element('clock-status').textContent, 'RTC disconnected');
});

test('development sensor stays live while moving and clears stale data on disconnection', async () => {
  const { context, element, actions, state, disconnect } = await dashboard();
  await element('claim').onclick();
  state.program = { state: 'running', total: 10, completed: 2 }; state.proximity.distanceCm = 34.5;
  vm.runInContext('render(api.status)', context);
  assert.equal(element('sensor-distance').textContent, '34.5 cm');
  assert.equal(element('sensor-status').textContent, 'Near');
  assert.equal(actions.open.disabled, true); assert.equal(element('stop').disabled, false);
  state.proximity.distanceCm = null; vm.runInContext('render(api.status)', context);
  assert.equal(element('sensor-status').textContent, 'No valid echo');
  disconnect(); await vm.runInContext('poll()', context);
  assert.equal(element('clock').textContent, '--:--:--');
  assert.equal(element('sensor-distance').textContent, '—');
  assert.equal(element('sensor-status').textContent, 'Disconnected');
  assert.equal(actions.home.disabled, true); assert.equal(element('save-time').disabled, true);
});

test('development Wi-Fi setup retains control fencing', async () => {
  const { context, element, state, calls } = await dashboard();
  assert.equal(element('wifi-save').disabled, true);
  await element('claim').onclick();
  element('wifi-ssid').value = 'Home router'; element('wifi-password').value = 'test-password';
  await element('wifi-form').onsubmit({ preventDefault() {} });
  assert.equal(calls.at(-1).path, '/api/wifi');
  assert.equal(calls.at(-1).data.ssid, 'Home router');
  assert.equal(element('wifi-password').value, '');
});

test('all twelve development switch lamps show real boolean states without taking control or homing', async () => {
  const { context, element, state, calls } = await dashboard();
  const names = ['X1', 'Y1', 'Z1', 'A1', 'B1', 'C1', 'X2', 'Y2', 'Z2', 'A2', 'B2', 'C2'];
  for (const board of [state.a, state.b]) Object.assign(board, { online: true, homed: 0, endstopsAgeMs: 100, endstops: Array(6).fill(false) });
  for (const [index, name] of names.entries()) {
    const board = index < 6 ? state.a : state.b;
    board.endstops[index % 6] = true;
    vm.runInContext('render(api.status)', context);
    for (const other of names) assert.equal(element('endstop-' + other).dataset.state, other === name ? 'triggered' : 'released');
    assert.equal(element('endstop-' + name).attrs['aria-label'], name + ': Triggered');
    assert.equal(element('endstop-state-' + name).textContent, 'Triggered');
    board.endstops[index % 6] = false;
  }
  vm.runInContext('render(api.status)', context);
  assert.equal(element('endstops-a-status').textContent, 'Live');
  assert.equal(element('endstops-b-status').textContent, 'Live');
  assert.equal(state.control.owner, undefined); assert.deepEqual(calls, []);
});

test('switch lamps clear missing, stale, busy and disconnected readings independently per board', async () => {
  const { context, element, state, disconnect } = await dashboard();
  for (const board of [state.a, state.b]) Object.assign(board, { online: true, endstopsAgeMs: 100, endstops: Array(6).fill(true) });
  const render = () => vm.runInContext('render(api.status)', context);
  for (const missing of [null, undefined, 'TRIGGERED', 1]) {
    state.a.endstops[3] = missing; render();
    assert.equal(element('endstop-A1').dataset.state, 'unknown');
    assert.equal(element('endstop-C2').dataset.state, 'triggered');
  }
  for (const age of [null, undefined, 2000, -1]) {
    state.a.endstopsAgeMs = age; render();
    assert.equal(element('endstop-X1').dataset.state, 'unknown');
  }
  state.a.endstopsAgeMs = 0; state.a.busy = true; render();
  assert.equal(element('endstop-X1').dataset.state, 'unknown');
  assert.equal(element('endstops-a-status').textContent, 'Paused during motion');
  state.a.busy = false; state.a.online = false; render();
  assert.equal(element('endstops-a-status').textContent, 'Board offline');
  assert.equal(element('endstop-C2').dataset.state, 'triggered');
  disconnect(); await vm.runInContext('poll()', context);
  assert.equal(element('endstop-C2').dataset.state, 'unknown');
  assert.equal(element('endstops-b-status').textContent, 'Disconnected');
});
