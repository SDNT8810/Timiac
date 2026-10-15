import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

async function dashboard({ active = false, supported = true, uploadError = false } = {}) {
  const nodes = new Map(), requests = [], listeners = {};
  const node = id => {
    if (!nodes.has(id)) nodes.set(id, { value: '', textContent: '', hidden: false, disabled: false, files: [],
      classList: { toggle() {} }, click() { if (!this.disabled) return this.onclick?.(); } });
    return nodes.get(id);
  };
  const buttons = ['home', 'close'].map(action => ({ ...node('action-' + action), dataset: { action } }));
  node('firmware-target').value = 'master:app';
  const document = { getElementById: node, activeElement: null,
    querySelectorAll: () => buttons, querySelector: () => buttons[0], addEventListener: (name, fn) => { listeners[name] = fn; } };
  const status = { online: true, queued: 0, status: { machineReady: true, a: { homed: 63 }, b: { homed: 63 },
    ota: { supported, active, state: active ? 'downloading' : 'idle' } } };
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
  for (const id of ['speed', 'save-time', 'local-time', 'install-firmware']) assert.equal(f.node(id).disabled, true);
  assert.equal(f.node('stop').disabled, false);
});
test('initial firmware is required and Home inside the target selector cannot command the motors', async () => {
  const f = await dashboard({ supported: false });
  assert.equal(f.node('install-firmware').disabled, true);
  assert.match(f.node('firmware-status').textContent, /initial USB\/SD/i);
  f.listeners.keydown({ key: 'Home', target: { tagName: 'SELECT' }, preventDefault() { throw new Error('Selector key must remain local'); } });
  assert.equal(f.requests.some(request => request.path.endsWith('/actions')), false);
});
