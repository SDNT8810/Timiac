import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import { readFileSync } from 'node:fs';
import { HardwareController } from '../WebApp/public/hardware.js';
import { localDate, clockActionNeedsRtc, renderClockReadouts } from '../WebApp/public/clock-panel.js';
import { renderSystemStatus, bindWifiSettings } from '../WebApp/public/system-panel.js';

test('release actions use fenced requests and track completion without replaying', async () => {
  const calls = [];
  const client = new HardwareController('', async (path, options) => {
    calls.push(path);
    const body = options.body ? JSON.parse(options.body) : null;
    if (body) {
      assert.equal(path, '/api/action');
      assert.equal(body.action, 'show-time'); assert.equal(body.seq, 1);
      assert.equal(body.fence, 7); assert.equal(body.session, 'boot');
      assert.equal(body.gcode, undefined);
    }
    return { ok: true, json: async () => ({ session: 'boot', revision: calls.length,
      control: { owner: 'phone', fence: 7 }, operation: { id: 'phone-1', state: body ? 'running' : 'done' } }) };
  }, 'phone');
  client.status = { session: 'boot', control: { owner: 'phone', fence: 7 } };
  const result = await client.action('show-time');
  assert.equal(result.operation.state, 'done'); assert.equal(calls.length, 2);
});

function dashboard() {
  const nodes = new Map();
  function element(id) {
    if (!nodes.has(id)) nodes.set(id, { textContent: '', value: '', disabled: false, hidden: false, dataset: {},
      listeners: {}, classList: { toggle() {} }, addEventListener(event, callback) { this.listeners[event] = callback; },
      querySelector() { return { textContent: id }; } });
    return nodes.get(id);
  }
  const actions = ['home', 'close', 'show-time', 'next-five', 'next-hour', 'struggle'].map(action => {
    const button = element(action); button.dataset.action = action; return button;
  });
  const state = { session: 'boot', machineReady: true, machineState: 'ready', a: { homed: 63 }, b: { homed: 63 },
    operation: { state: 'idle' }, control: {}, travelMm: 50,
    clock: { present: true, valid: true, localTime: '2026-09-21T13:45:12', error: '' } };
  const calls = [];
  class Controller {
    status = state;
    ownsControl() { return this.status.control.owner === 'phone'; }
    async refresh() { return this.status; }
    async claim() { calls.push('claim'); this.status.control.owner = 'phone'; }
    async post(path, data) { calls.push({ path, data }); }
    async action(action) { calls.push(action); }
    async stop() { calls.push('stop'); }
  }
  const context = vm.createContext({ HardwareController: Controller, localDate, clockActionNeedsRtc, renderClockReadouts, renderSystemStatus, bindWifiSettings, Date, setTimeout() {},
    document: { getElementById: element, querySelectorAll: () => actions } });
  const source = readFileSync(new URL('../WebApp/release/app.js', import.meta.url), 'utf8').replace(/^import .*;\r?\n/gm, '');
  vm.runInContext(source, context);
  return { context, element, state, calls };
}
const flush = () => new Promise(resolve => setImmediate(resolve));
function slowOperation(timeoutMs, finishAfter = Infinity) {
  let now = 0, requests = 0;
  const source = readFileSync(new URL('../WebApp/public/hardware.js', import.meta.url), 'utf8').replaceAll('export ', '');
  const Controller = vm.runInNewContext(source + '\nHardwareController;', {
    Date: { now: () => now }, AbortSignal: { timeout: () => undefined },
    setTimeout(callback) { now += 200000; callback(); },
  });
  const client = new Controller('', async () => ({ ok: true, json: async () => ({
    session: 'boot', revision: ++requests, control: { owner: 'phone', fence: 1 },
    operation: { id: 'phone-1', timeoutMs, state: requests >= finishAfter ? 'done' : 'running' },
  }) }), 'phone');
  client.status = { session: 'boot', control: { owner: 'phone', fence: 1 } };
  return { client, requests: () => requests };
}
test('advertised long operation budgets remain tracked and still have a finite deadline', async () => {
  const homing = slowOperation(660000, 4);
  assert.equal((await homing.client.action('home')).operation.state, 'done');
  assert.equal(homing.requests(), 4);
  const stalled = slowOperation(660000);
  await assert.rejects(stalled.client.action('home'), /Completion timeout/);
  assert.equal(stalled.requests(), 5);
});
test('180-second homing budget gives the browser ten seconds of completion margin', async () => {
  const homing = slowOperation(180000);
  await assert.rejects(homing.client.action('home'), /Completion timeout/);
  assert.equal(homing.requests(), 2);
});
test('ordinary moves retain the shorter completion deadline', async () => {
  const move = slowOperation(300000, 4);
  await assert.rejects(move.client.action('close'), /Completion timeout/);
  assert.equal(move.requests(), 3);
});

test('20 percent motion can wait beyond the old browser cap', async () => {
  const move = slowOperation(1500000, 6);
  await move.client.action('close');
  assert.equal(move.requests(), 6);
});
test('dashboard shows RTC time and disables motion for missing home, RTC, connection, or competing owner', async () => {
  const { context, element, state } = dashboard(); await flush();
  assert.equal(element('clock').textContent, '13:45:12');
  assert.equal(element('show-time').disabled, false);
  state.a.homed = 0; vm.runInContext('render()', context);
  assert.equal(element('close').disabled, true); assert.equal(element('home').disabled, false);
  state.a.homed = 63; state.clock.valid = false; vm.runInContext('render()', context);
  assert.equal(element('show-time').disabled, true); assert.equal(element('close').disabled, false);
  state.control.owner = 'other'; vm.runInContext('render()', context);
  assert.equal(element('home').disabled, true); assert.equal(element('save-time').disabled, true);
  assert.equal(element('stop').disabled, false);
  vm.runInContext('connected = false; render()', context);
  assert.equal(element('clock').textContent, '--:--:--');
});
test('dashboard claims automatically and saves explicit local time to RTC endpoint', async () => {
  const { element, calls } = dashboard(); await flush();
  await element('close').listeners.click();
  assert.deepEqual(calls, ['claim', 'close']);
  element('local-time').value = '2026-09-21T09:30';
  element('time-form').listeners.submit({ preventDefault() {} }); await flush();
  assert.equal(calls.at(-1).path, '/api/time');
  assert.equal(calls.at(-1).data.localTime, '2026-09-21T09:30:00');
});

test('release dashboard reports automatic startup, homing, and waiting for valid time', async () => {
  const { context, element, state, calls } = dashboard(); await flush();
  state.clock.startupPending = true; state.machineReady = false;
  state.a.homed = state.b.homed = 0;
  vm.runInContext('render()', context);
  assert.match(element('running-status').textContent, /Starting automatically/);
  assert.equal(element('home').disabled, true);
  assert.equal(element('stop').disabled, false);
  state.clock.startupPending = false; state.clock.running = true;
  state.operation = { state: 'running', homing: true };
  vm.runInContext('render()', context);
  assert.match(element('running-status').textContent, /Homing all arms/);
  assert.equal(element('show-time').disabled, true);
  state.operation.state = 'done'; state.machineReady = true;
  state.a.homed = state.b.homed = 63; state.clock.valid = false;
  vm.runInContext('render()', context);
  assert.match(element('running-status').textContent, /waiting for a valid time/);
  state.clock.valid = true; vm.runInContext('render()', context);
  assert.match(element('running-status').textContent, /Clock running/);
  assert.deepEqual(calls, []); // Browser observes; startup belongs to the master.
});
test('Stop cancels an action waiting for control', async () => {
  const { element, calls } = dashboard(); await flush();
  const pending = element('close').listeners.click();
  await element('stop').listeners.click(); await pending;
  assert.ok(calls.includes('stop')); assert.ok(!calls.includes('close'));
});

test('manual Home promises to stay parked and speed slider saves through the control lease', async () => {
  const { context, element, state, calls } = dashboard(); await flush();
  state.clock.running = false; state.operation = { state: 'running', homing: true };
  vm.runInContext('render()', context);
  assert.match(element('running-status').textContent, /stay home/);
  assert.equal(element('motion-speed').disabled, true);
  state.operation.state = 'done'; vm.runInContext('render()', context);
  assert.equal(element('motion-speed').value, '50');
  assert.equal(element('motion-speed').disabled, false);
  element('motion-speed').value = '20'; element('motion-speed').listeners.input();
  vm.runInContext('render()', context); // Status polling must not move the thumb while dragging.
  assert.equal(element('motion-speed').value, '20');
  assert.equal(element('speed-value').textContent, '20%');
  await element('motion-speed').listeners.change();
  assert.equal(calls[0], 'claim');
  assert.equal(calls[1].path, '/api/speed'); assert.equal(calls[1].data.percent, 20);
  state.speedPercent = 100; vm.runInContext('render()', context);
  assert.equal(element('motion-speed').value, '100');
  state.control.owner = 'other'; vm.runInContext('render()', context);
  assert.equal(element('motion-speed').disabled, true);
});

test('animation waits for the whole program, even when a single step has finished', async () => {
  for (const ending of ['done', 'faulted']) {
    let requests = 0;
    const client = new HardwareController('', async () => {
      ++requests;
      return { ok: true, json: async () => ({ session: 'boot', revision: requests,
        control: { owner: 'phone', fence: 1 },
        operation: { id: 'phone-1-0', state: 'done' },
        program: { id: 'phone-1', builtin: true, state: requests === 1 ? 'running' : ending, error: ending === 'faulted' ? 'Operator stop' : '' },
      }) };
    }, 'phone');
    client.status = { session: 'boot', control: { owner: 'phone', fence: 1 } };
    if (ending === 'done') assert.equal((await client.action('struggle')).program.state, 'done');
    else await assert.rejects(client.action('struggle'), /Operator stop/);
    assert.equal(requests, 2);
  }
});

test('release page exposes clock controls without development actions', async () => {
  const html = readFileSync(new URL('../WebApp/release/index.html', import.meta.url), 'utf8');
  assert.deepEqual([...html.matchAll(/data-action="([^"]+)"/g)].map(m => m[1]), ['home', 'close', 'show-time', 'next-five', 'next-hour', 'struggle']);
  assert.doesNotMatch(html, /sensorless|terminal|data-command/);
  const { context, element, state } = dashboard(); await flush();
  state.clock.running = true; vm.runInContext('render()', context);
  assert.match(element('running-status').textContent, /Clock running/);
  state.clock.running = false; vm.runInContext('render()', context);
  assert.match(element('running-status').textContent, /Clock paused/);
  state.program = { state: 'running', name: 'Curious creature', completed: 3, total: 28 };
  state.operation.state = 'done'; vm.runInContext('render()', context);
  for (const name of ['home', 'close', 'show-time', 'next-five', 'next-hour', 'struggle']) assert.equal(element(name).disabled, true);
  assert.equal(element('stop').disabled, false);
  assert.match(element('machine-status').textContent, /3\/28/);
});

test('hand distance stays visible before homing and during animation, and clears on lost connection', async () => {
  const { context, element, state } = dashboard(); await flush();
  state.proximity = { enabled: true, armed: true, distanceCm: 73.2 };
  vm.runInContext('render()', context);
  assert.equal(element('sensor-distance').textContent, '73.2 cm');
  assert.equal(element('sensor-status').textContent, 'Clear');
  state.a.homed = 0; vm.runInContext('render()', context);
  assert.equal(element('sensor-distance').textContent, '73.2 cm');
  assert.match(element('machine-status').textContent, /Ready to home/);
  state.a.homed = 63;
  state.proximity.distanceCm = null; state.proximity.armed = false;
  vm.runInContext('render()', context);
  assert.equal(element('sensor-distance').textContent, '—');
  assert.equal(element('sensor-status').textContent, 'No valid echo');
  assert.match(element('machine-status').textContent, /Move hand away/);
  state.proximity.distanceCm = 34.5;
  state.program = { state: 'running', name: 'Curious creature', completed: 2, total: 18 };
  vm.runInContext('render()', context);
  assert.match(element('machine-status').textContent, /Curious creature.*2\/18/);
  assert.equal(element('sensor-distance').textContent, '34.5 cm');
  assert.equal(element('sensor-status').textContent, 'Near');
  assert.equal(element('home').disabled, true); assert.equal(element('stop').disabled, false);
  vm.runInContext('connected = false; render()', context);
  assert.equal(element('sensor-distance').textContent, '—');
  assert.equal(element('sensor-status').textContent, 'Disconnected');
});

test('release Wi-Fi form claims control, submits credentials and clears the password', async () => {
  const { element, calls } = dashboard(); await flush();
  element('wifi-ssid').value = 'Phone hotspot'; element('wifi-password').value = 'test-password';
  await element('wifi-form').onsubmit({ preventDefault() {} });
  assert.equal(calls[0], 'claim');
  assert.equal(calls.at(-1).path, '/api/wifi');
  assert.equal(calls.at(-1).data.ssid, 'Phone hotspot');
  assert.equal(calls.at(-1).data.password, 'test-password');
  assert.equal(element('wifi-password').value, '');
  await element('wifi-forget').onclick();
  assert.equal(calls.at(-1).data.forget, true);
});
