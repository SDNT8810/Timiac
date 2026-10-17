import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

async function dashboard({ active = false, supported = true, uploadError = false, statusPatch = {}, online = true, queued = 0, fullUpdate = null, update = null } = {}) {
  const nodes = new Map(), requests = [], listeners = {};
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
    if (path.endsWith('/status')) return Response.json(status);
    if (path.endsWith('/firmware')) return uploadError ? Response.json({ error: 'Wrong board image' }, { status: 400 }) : Response.json({ id: 'image', revision: '2026-10-15.5' });
    if (path.endsWith('/updates')) return Response.json({ id: 'job', state: 'queued' });
    return Response.json({});
  };
  runInNewContext(readFileSync(new URL('../public/app.js', import.meta.url), 'utf8'), {
    document, fetch, setInterval: () => 1, clearInterval() {}, sessionStorage: { removeItem() {} },
  });
  await new Promise(resolve => setImmediate(resolve));
  return { node, buttons, listeners, requests };
}
test('firmware form uploads the selected board bytes then queues exactly that image', async () => {
  const f = await dashboard(), file = new File([new Uint8Array([1, 2, 3])], 'firmware.bin');
  f.node('firmware-target').value = 'octopus2:octopus'; f.node('firmware-file').files = [file];
  await f.node('firmware-form').onsubmit({ preventDefault() {} });
  const upload = f.requests.find(request => request.path.endsWith('/firmware'));
  assert.equal(upload.options.body, file);
  assert.equal(upload.options.headers['Content-Type'], 'application/octet-stream');
  assert.equal(upload.options.headers['X-Timiac-Target'], 'octopus2');
  assert.equal(upload.options.headers['X-Timiac-Kind'], 'octopus');
  const jobs = f.requests.filter(request => request.path.endsWith('/updates'));
  assert.equal(jobs.length, 1); assert.deepEqual(JSON.parse(jobs[0].options.body), { imageId: 'image' });
  assert.equal(f.node('firmware-file').value, '');
});
test('a rejected firmware upload never queues a flash and returns a useful error', async () => {
  const f = await dashboard({ uploadError: true });
  f.node('firmware-file').files = [new File(['bad file'], 'wrong.bin')];
  await f.node('firmware-form').onsubmit({ preventDefault() {} });
  assert.equal(f.requests.filter(request => request.path.endsWith('/updates')).length, 0);
  assert.equal(f.node('message').textContent, 'Wrong board image');
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
