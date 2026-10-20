import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

async function dashboard({ active = false, supported = true, uploadError = false, statusPatch = {}, online = true, queued = 0, fullUpdate = null, update = null } = {}) {
  const nodes = new Map(), requests = [], listeners = {}, network = { fail: false };
  let refresh;
  const node = id => {
    if (!nodes.has(id)) nodes.set(id, { value: '', textContent: '', hidden: false, disabled: false, files: [],
      classList: { classes: new Set(), toggle(name, active) { if (active) this.classes.add(name); else this.classes.delete(name); }, contains(name) { return this.classes.has(name); } }, click() { if (!this.disabled) return this.onclick?.(); } });
    return nodes.get(id);
  };
  const buttons = ['open', 'close', 'show-time', 'next-five', 'next-hour', 'struggle'].map(action => ({ ...node('action-' + action), dataset: { action } }));
  node('firmware-target').value = 'master:app';
  const document = { getElementById: node, activeElement: null,
    querySelectorAll: () => buttons, querySelector: () => buttons[0], addEventListener: (name, fn) => { listeners[name] = fn; } };
  const status = { online, queued, fullUpdate, update, status: { machineReady: true, a: { homed: 63 }, b: { homed: 63 },
    clock: { valid: true, localTime: '2026-10-15T18:15:42', armMinute: 1095 },
    ota: { supported, active, state: active ? 'downloading' : 'idle' }, ...statusPatch } };
  const fetch = async (path, options = {}) => {
    requests.push({ path, options });
    if (path === '/api/session') return Response.json({ username: 'Andrea' });
    if (path === '/api/config') return Response.json({ deviceId: 'clock-01' });
    if (path.endsWith('/status')) { if (network.fail) throw new Error('Connection interrupted'); return Response.json(status); }
    if (path.endsWith('/firmware')) return uploadError ? Response.json({ error: 'Wrong board image' }, { status: 400 }) : Response.json({ id: 'image', revision: '2026-10-15.5' });
    if (path.endsWith('/updates')) return Response.json({ id: 'job', state: 'queued' });
    if (path.endsWith('/component-update')) return uploadError ? Response.json({ error: 'GitHub build access unavailable' }, { status: 502 }) : Response.json({ id: 'component', state: 'building' });
    return Response.json({});
  };
  runInNewContext(readFileSync(new URL('../public/app.js', import.meta.url), 'utf8'), {
    document, fetch, setInterval: fn => { refresh = fn; return 1; }, clearInterval() {}, sessionStorage: { removeItem() {} },
  });
  await new Promise(resolve => setImmediate(resolve));
  return { node, buttons, listeners, requests, status, network, refresh: () => refresh() };
}

test('remote sensor shows voltage, arming, retained trigger time and a brief ignored hand event', async () => {
  const f = await dashboard({ statusPatch: { proximity: { enabled: true, pin: 34, detected: false, armed: true, ready: true,
    millivolts: 95, rawAdc: 80, blockedReason: '', triggers: 2, ignored: 1,
    lastTriggerTime: '2026-10-20T17:31:42', lastTriggerAgeMs: 12000, lastTriggerId: 'sensor-gesture',
    lastOutcome: 'ignored', lastReason: 'Motion or a program is busy', lastEventTime: '2026-10-20T17:31:50' },
    program: { id: 'sensor-gesture', state: 'running' } } });
  assert.equal(f.node('sensor-presence').textContent, 'Clear');
  assert.match(f.node('sensor-reading').textContent, /GPIO34.*95 mV.*ADC 80/);
  assert.match(f.node('sensor-last-trigger').textContent, /2026-10-20 17:31:42.*Struggle running/);
  assert.match(f.node('sensor-last-event').textContent, /2026-10-20 17:31:50.*Ignored: Motion or a program is busy/);
  assert.match(f.node('sensor-counts').textContent, /2 Struggle triggers.*1 ignored hand event since master restart/);
  assert.match(f.node('sensor-eligibility').textContent, /Armed/);
  Object.assign(f.status.status.proximity, { detected: true, armed: false, millivolts: null, rawAdc: 0, adcFloorMv: 75,
    blockedReason: 'Withdraw hand and clear the sensor for 500 ms' });
  await f.refresh();
  assert.equal(f.node('sensor-presence').textContent, 'Hand detected');
  assert.match(f.node('sensor-reading').textContent, /below 75 mV.*ADC 0/);
  assert.match(f.node('sensor-eligibility').textContent, /Withdraw hand/);
  assert.match(f.node('sensor-last-trigger').textContent, /17:31:42/);
  assert.equal(f.requests.some(r => r.path.endsWith('/actions')), false); // Readout never commands motion.
});

test('offline and interrupted remote sensor reports clear live indications but retain labeled history', async () => {
  const f = await dashboard({ statusPatch: { proximity: { enabled: true, pin: 34, detected: false, armed: true,
    millivolts: 95, rawAdc: 80, triggers: 1, lastTriggerTime: '2026-10-20T17:31:42' } } });
  f.status.online = false; await f.refresh();
  assert.equal(f.node('sensor-presence').textContent, 'Master offline');
  assert.equal(f.node('sensor-presence').classList.contains('armed'), false);
  assert.equal(f.node('sensor-reading').textContent, 'Live sensor readings unavailable.');
  assert.match(f.node('sensor-sync').textContent, /last received report/);
  assert.match(f.node('sensor-last-trigger').textContent, /17:31:42/);
  f.status.online = true; await f.refresh(); f.network.fail = true; await f.refresh();
  assert.equal(f.node('sensor-presence').textContent, 'Connection unavailable');
  assert.equal(f.node('sensor-reading').textContent, 'Live sensor readings unavailable.');
  assert.match(f.node('sensor-last-trigger').textContent, /17:31:42/);
});

test('sensor history resets with the master session and never attributes a manual Struggle to the sensor', async () => {
  const f = await dashboard({ statusPatch: { proximity: { enabled: true, detected: false, triggers: 1,
    lastTriggerId: 'sensor-gesture', lastTriggerTime: '2026-10-20T17:31:42' }, program: { id: 'manual-struggle', state: 'running' } } });
  assert.match(f.node('sensor-last-trigger').textContent, /Struggle accepted/);
  assert.doesNotMatch(f.node('sensor-last-trigger').textContent, /running/);
  f.status.status.session = 'new-master-boot';
  f.status.status.proximity = { enabled: true, pin: 34, detected: null, triggers: 0, ignored: 0, blockedReason: 'Sensor reading is unavailable or stale' };
  await f.refresh();
  assert.equal(f.node('sensor-last-trigger').textContent, 'None since master restart');
  assert.equal(f.node('sensor-presence').textContent, 'Waiting for a reading');
  assert.match(f.node('sensor-eligibility').textContent, /unavailable or stale/);
});

test('old firmware and unavailable RTC time produce useful sensor status without inventing a timestamp', async () => {
  const f = await dashboard();
  assert.equal(f.node('sensor-presence').textContent, 'Update master firmware');
  assert.equal(f.node('sensor-last-trigger').textContent, 'Not available in the last master report');
  assert.equal(f.node('sensor-last-event').textContent, 'Not available in the last master report');
  f.status.status.proximity = { enabled: true, pin: 34, detected: true, triggers: 0, ignored: 1,
    lastOutcome: 'ignored', lastReason: 'Clock time is invalid', lastEventAgeMs: 5000, lastEventTime: '' };
  await f.refresh();
  assert.match(f.node('sensor-last-event').textContent, /5s before the last report.*clock time unavailable.*Ignored: Clock time is invalid/);
});
test('component form requests only the selected GitHub image without a local file or separate upload', async () => {
  const f = await dashboard();
  f.node('firmware-target').value = 'octopus2:octopus';
  await f.node('firmware-form').onsubmit({ preventDefault() {} });
  const calls = f.requests.filter(request => request.path.endsWith('/component-update'));
  assert.equal(calls.length, 1);
  assert.deepEqual(JSON.parse(calls[0].options.body), { target: 'octopus2', kind: 'octopus' });
  assert.equal(f.requests.some(r => r.path.endsWith('/firmware') || r.path.endsWith('/updates') || r.path.endsWith('/full-update')), false);
  const html = readFileSync(new URL('../public/index.html', import.meta.url), 'utf8');
  assert.doesNotMatch(html, /type="file"|id="firmware-file"/);
  assert.match(f.node('message').textContent, /Octopus 2 requested from GitHub/);
});
test('a rejected component build is not replayed or followed by an image upload', async () => {
  const f = await dashboard({ uploadError: true });
  await f.node('firmware-form').onsubmit({ preventDefault() {} });
  assert.equal(f.requests.filter(request => request.path.endsWith('/component-update')).length, 1);
  assert.equal(f.requests.some(r => r.path.endsWith('/firmware') || r.path.endsWith('/updates')), false);
  assert.equal(f.node('message').textContent, 'GitHub build access unavailable');
});
test('a selected component survives reopening during build and installs as exactly one step', async () => {
  const f = await dashboard({ fullUpdate: { state: 'building', selected_target: 'master', selected_kind: 'filesystem', steps: [] } });
  assert.match(f.node('full-update-status').textContent, /Preparing Master local dashboard.*GitHub/);
  assert.equal(f.node('firmware-target').disabled, true);
  assert.equal(f.node('install-firmware').disabled, true);
  await f.node('firmware-form').onsubmit({ preventDefault() {} });
  assert.equal(f.requests.some(r => r.path.endsWith('/component-update')), false);
  f.status.fullUpdate.state = 'installing'; f.status.fullUpdate.steps = [{ target: 'master', kind: 'filesystem', state: 'delivered' }];
  await f.refresh(); assert.match(f.node('full-update-status').textContent, /1\/1: master dashboard/);
  f.status.fullUpdate.state = 'complete'; f.status.fullUpdate.steps[0].state = 'complete';
  await f.refresh(); assert.match(f.node('full-update-status').textContent, /Master local dashboard update complete/);
  assert.equal(f.node('install-firmware').disabled, false);
});
test('an active firmware update disables movement and settings while Stop stays available', async () => {
  const f = await dashboard({ active: true });
  assert.ok(f.buttons.every(button => button.disabled));
  for (const id of ['save-time', 'local-time', 'install-firmware', 'calibrate']) assert.equal(f.node(id).disabled, true);
  assert.equal(f.node('stop').disabled, false);
});
test('initial firmware is required and Home inside the target selector cannot command the motors', async () => {
  const f = await dashboard({ supported: false });
  assert.equal(f.node('install-firmware').disabled, true);
  assert.match(f.node('firmware-status').textContent, /initial USB\/SD/i);
  f.listeners.keydown({ key: 'Home', target: { tagName: 'SELECT' }, preventDefault() { throw new Error('Selector key must remain local'); } });
  assert.equal(f.requests.some(request => request.path.endsWith('/actions')), false);
});

test('older firmware without any OTA fields can still use every movement button', async () => {
  const f = await dashboard({ statusPatch: { ota: undefined, operation: { state: 'done' }, program: { state: 'done' }, control: { owner: '' } } });
  assert.ok(f.buttons.every(button => !button.disabled));
  assert.equal(f.node('connection').textContent, 'Connected to Timiac');
  assert.equal(f.node('install-firmware').disabled, true);
});

test('every global command lock has a visible reason and Stop remains available', async () => {
  const cases = [
    [{ control: { owner: 'local-phone' } }, /local dashboard.*15 seconds/],
    [{ ota: { active: true } }, /Firmware update/],
    [{ stopPending: true }, /Stop confirmation/],
    [{ program: { state: 'paused' } }, /movement is in progress/],
    [{ a: { homed: 0 } }, /Calibrate arms/],
    [{ machineReady: false }, /motor boards/],
  ];
  for (const [statusPatch, reason] of cases) {
    const f = await dashboard({ statusPatch });
    assert.ok(f.buttons.every(button => button.disabled));
    assert.match(f.node('connection').textContent, reason);
    assert.equal(f.node('stop').disabled, false);
  }
  const f = await dashboard({ queued: 1 });
  assert.ok(f.buttons.every(button => button.disabled));
  assert.match(f.node('connection').textContent, /queued command/);
});

test('Home key invokes Open and the cloud page has no speed control', async () => {
  const f = await dashboard();
  f.listeners.keydown({ key: 'Home', target: { tagName: 'BODY' }, preventDefault() {} });
  await new Promise(resolve => setImmediate(resolve));
  const actions = f.requests.filter(request => request.path.endsWith('/actions'));
  assert.equal(actions.length, 1);
  assert.equal(JSON.parse(actions[0].options.body).type, 'open');
  const html = readFileSync(new URL('../public/index.html', import.meta.url), 'utf8');
  assert.match(html, /data-action="open">Open/);
  assert.doesNotMatch(html, /type="range"|id="speed"/);
});


test('Full update button requests a fresh build without asking for firmware files', async () => {
  const f = await dashboard();
  assert.equal(f.node('full-update').disabled, false);
  await f.node('full-update').onclick();
  const calls = f.requests.filter(r => r.path.endsWith('/full-update'));
  assert.equal(calls.length, 1); assert.deepEqual(JSON.parse(calls[0].options.body), {});
  assert.equal(f.requests.some(r => r.path.endsWith('/firmware') || r.path.endsWith('/updates')), false);
});

test('reopening the dashboard restores durable Full update progress and prevents duplicate clicks', async () => {
  const f = await dashboard({fullUpdate: {state: 'installing', steps: [{target: 'esp1', state: 'complete'}, {target: 'esp2', state: 'delivered'}]}});
  assert.match(f.node('full-update-status').textContent, /2\/2: esp2/);
  assert.equal(f.node('full-update').disabled, true); assert.equal(f.node('stop').disabled, false);
  assert.ok(f.buttons.every(button => button.disabled));
});

test('a later manual success moves the old Full update failure into history without erasing it', async () => {
  const f = await dashboard({fullUpdate: {state: 'failed', updated_at: 1000, error: 'esp2: Previous session expired'},
    update: {id: 'manual-esp2', target: 'esp2', state: 'complete', createdAt: 2000},
    statusPatch: {ota: {supported: true, id: 'manual-esp2', target: 'esp2', state: 'complete', active: false}}});
  assert.equal(f.node('firmware-status').textContent, 'esp2 · complete');
  assert.equal(f.node('firmware-status').classList.contains('error'), false);
  assert.equal(f.node('full-update-history').hidden, false);
  assert.match(f.node('full-update-history-status').textContent, /Full update failed: esp2: Previous session expired/);
  assert.doesNotMatch(f.node('full-update-status').textContent, /failed|expired/);
  assert.equal(f.node('full-update-status').classList.contains('error'), false);
  assert.equal(f.node('full-update').disabled, false);
});

test('a new manual failure stays visible while the previous full failure is archived', async () => {
  const f = await dashboard({fullUpdate: {state: 'failed', updated_at: 1000, error: 'Old failure'},
    update: {id: 'manual', target: 'master', state: 'failed', error: 'SHA mismatch', createdAt: 2000}});
  assert.match(f.node('firmware-status').textContent, /master · failed: SHA mismatch/);
  assert.equal(f.node('firmware-status').classList.contains('error'), true);
  assert.equal(f.node('full-update-history').hidden, false);
});

test('the current Full update failure remains visible until a later job starts', async () => {
  const f = await dashboard({fullUpdate: {state: 'failed', updated_at: 2000, error: 'SD write failed'},
    update: {id: 'full-step', state: 'failed', createdAt: 1000}});
  assert.match(f.node('full-update-status').textContent, /Full update failed: SD write failed/);
  assert.equal(f.node('full-update-status').classList.contains('error'), true);
  assert.equal(f.node('full-update-history').hidden, true);
});

test('an expired job overrides stale offline hardware progress from the same job', async () => {
  const f = await dashboard({online: false, update: {id: 'stalled', target: 'master', state: 'expired', error: 'No progress'},
    statusPatch: {ota: {supported: true, active: true, id: 'stalled', target: 'master', state: 'downloading', done: 98, size: 100}}});
  assert.equal(f.node('firmware-status').textContent, 'master · expired: No progress');
  assert.equal(f.node('firmware-status').classList.contains('error'), true);
  assert.equal(f.node('connection').textContent, 'Master is offline');
  assert.equal(f.node('install-firmware').disabled, true);
});


test('the last stop remains visible after recovery and the pause reason is explicit', async () => {
  const f = await dashboard({statusPatch: {reliability: {lastStop: 'Bridge link lost during work'}, clock: {running: false, pausedReason: 'Manual close'}}});
  assert.equal(f.node('stop-history').textContent, 'Last stop: Bridge link lost during work');
  assert.equal(f.node('clock-mode').textContent, 'Clock paused: Manual close');
});
