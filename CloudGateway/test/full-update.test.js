import test from 'node:test';
import assert from 'node:assert/strict';
import { DatabaseSync } from 'node:sqlite';
import { readFileSync } from 'node:fs';
import { createGateway } from '../cloudflare/worker.js';

const timiac = import.meta.url.toLowerCase().includes('/timiac/');
const family = timiac ? 'TIMIAC' : 'LUMIAC', prefix = timiac ? 'Timiac' : 'Lumiac';
const sha = 'a'.repeat(40), machine = timiac ? 'TIMIAC2-123456789abc' : 'LUMIAC1-123456789abc';
const components = timiac ? [['esp1','app'],['esp2','app'],['octopus1','octopus'],['octopus2','octopus'],['master','app'],['master','filesystem']]
  : [['octopus','octopus'],['esp12','app'],['esp32','app']];
function fixture(t, controller = 'esp32') {
  const db = new DatabaseSync(':memory:');
  db.exec(readFileSync(new URL('../cloudflare/schema.sql', import.meta.url), 'utf8'));
  t.after(() => db.close());
  const DB = { prepare(sql) { let args = []; return {
    bind(...values) { args = values.map(v => v instanceof ArrayBuffer ? new Uint8Array(v) : v); return this; },
    async first() { return db.prepare(sql).get(...args) || null; },
    async all() { return { results: db.prepare(sql).all(...args) }; },
    async run() { return { meta: { changes: Number(db.prepare(sql).run(...args).changes) } }; },
  }; } };
  let now = Date.now();
  const origin = 'https://clock.example';
  const env = { DB, [`${family}_DEVICE_ID`]: 'clock', [`${family}_PUBLIC_ORIGIN`]: origin,
    [`${family}_DEVICE_KEY`]: 'device-secret', [`${family}_ADMIN_USERNAME`]: 'Andrea',
    [`${family}_ADMIN_EMAIL`]: 'andrea@example.com', [`${family}_ADMIN_UID`]: 'andrea',
    GITHUB_REPOSITORY: 'owner/project', GITHUB_REF: timiac ? 'main' : 'RF', GITHUB_BUILD_TOKEN: 'github-secret', FIRMWARE_BUILD_KEY: 'build-secret' };
  const dispatches = [];
  const gateway = createGateway({ clock: () => now, verifyToken: async token => {
    if (token !== 'admin') throw Error('Invalid token');
    return { sub: 'andrea', email: 'andrea@example.com', firebase: { sign_in_provider: 'password' } };
  }, upstream: async (url, options) => {
    assert.equal(options.headers.Authorization, 'Bearer github-secret');
    if (url.includes('/commits/')) return Response.json({ sha });
    assert.ok(url.endsWith('/actions/workflows/full-update.yml/dispatches'));
    dispatches.push(JSON.parse(options.body)); return new Response(null, { status: 204 });
  } });
  const admin = { Cookie: `__Host-${family.toLowerCase()}-session=admin` };
  const call = (path, body, headers = admin) => gateway.fetch(new Request(origin + path, {
    method: body === undefined ? 'GET' : 'POST',
    headers: { Origin: origin, 'Content-Type': body instanceof Uint8Array ? 'application/octet-stream' : 'application/json', ...headers },
    ...(body === undefined ? {} : { body: body instanceof Uint8Array ? body : JSON.stringify(body) }),
  }), env);
  const board = { bridgeConnected: true, octopusConnected: true, otaSupported: true, sdOtaSupported: true, homed: 63 };
  const state = { session: 'boot-1', firmware: machine, machineReady: true, a: { ...board }, b: { ...board },
    controller, octopusOnline: true, ota: { supported: true, sdSupported: true, active: false } };
  const sync = () => call('/device/v1/devices/clock/sync', { status: state }, { ['X-' + prefix + '-Device-Key']: 'device-secret' });
  const begin = async (body = {}) => {
    const response = await call('/api/devices/clock/full-update', body);
    assert.equal(response.status, 202, await response.clone().text());
    const id = (await response.json()).id;
    if (!body.validateOnly) { const parked = await (await sync()).json(); assert.equal(parked.action.type, 'stop'); }
    return id;
  };
  const build = (id, endpoint, body = {}, extra = {}) => call('/build/v1/full-updates/' + id + '/' + endpoint, body,
    { 'X-Firmware-Build-Key': 'build-secret', 'X-Firmware-Source': sha, ...extra });
  const upload = async (id, [target, kind], identity = machine) => {
    const bytes = new Uint8Array(kind === 'filesystem' ? 0x160000 : 2048);
    if (kind === 'octopus') { const v = new DataView(bytes.buffer); v.setUint32(0, 0x20010000, true); v.setUint32(4, 0x08008101, true); }
    else bytes[0] = 0xE9;
    const role = kind === 'octopus' ? 'octopus' : kind === 'filesystem' ? 'master-filesystem' : target;
    bytes.set(new TextEncoder().encode(`${family}_OTA_ROLE:${role} ${identity} 2026-10-15.1`), 64);
    const response = await build(id, 'images', bytes, { ['X-' + prefix + '-Target']: target, ['X-' + prefix + '-Kind']: kind });
    assert.equal(response.status, 201, await response.clone().text());
    return response.json();
  };
  const bundle = async id => { for (const component of components) await upload(id, component); };
  const full = async () => (await (await call('/api/devices/clock/full-update')).json()).update;
  return { db, env, call, state, sync, begin, build, upload, bundle, full, dispatches, advance: ms => { now += ms; } };
}

test('Full update requires admin, same origin, Internet and initial OTA installation', async t => {
  const f = fixture(t);
  assert.equal((await f.call('/api/devices/clock/full-update', {}, {})).status, 401);
  assert.equal((await f.call('/api/devices/clock/full-update', {}, { Cookie: `__Host-${family.toLowerCase()}-session=admin`, Origin: 'https://other.example' })).status, 403);
  assert.equal((await f.call('/api/devices/clock/full-update', {})).status, 409);
  f.state.ota.supported = false; await f.sync();
  assert.equal((await f.call('/api/devices/clock/full-update', {})).status, 409);
});

test('build is pinned, complete-bundle checks precede all flashing, concurrent controls are blocked', async t => {
  const f = fixture(t); await f.sync(); const id = await f.begin();
  assert.deepEqual(f.dispatches, [{ ref: f.env.GITHUB_REF, inputs: { update_id: id, source_sha: sha } }]);
  assert.equal((await f.call('/api/devices/clock/full-update', {})).status, 409);
  assert.equal((await f.call('/api/devices/clock/actions', { type: 'home' })).status, 409);
  assert.equal((await f.build(id, 'complete')).status, 409);
  assert.equal((await f.sync()).status, 200);
  assert.equal(f.db.prepare('SELECT count(*) n FROM ota_jobs').get().n, 0);
  await f.bundle(id); assert.equal((await f.build(id, 'complete')).status, 200);
  assert.equal(f.db.prepare('SELECT count(*) n FROM ota_jobs').get().n, 0);
});

for (const controller of timiac ? ['master'] : ['esp12', 'esp32']) test('sequential install survives reboot and requires matching SHA receipts: ' + controller, async t => {
  const f = fixture(t, controller); await f.sync(); const id = await f.begin(); await f.bundle(id); await f.build(id, 'complete');
  const expected = components.filter(([target]) => timiac || target === 'octopus' || target === controller);
  for (let n = 0; n < expected.length; n++) {
    const response = await f.sync(), update = (await response.json()).update;
    assert.ok(update, 'Missing update');
    assert.equal(update.target, expected[n][0]); assert.equal(update.kind, expected[n][1]);
    f.state.ota = { ...f.state.ota, id: update.id, sha256: '0'.repeat(64), state: 'complete', active: false };
    assert.equal((await (await f.sync()).json()).update, undefined); // Wrong hash cannot advance.
    f.state.ota.sha256 = update.sha256;
    if (update.target === 'master' && update.kind === 'app' || update.target === controller) f.state.session = 'reboot-' + n;
  }
  await f.sync();
  const result = await f.full(); assert.equal(result.state, 'complete');
  assert.equal(result.steps.filter(s => s.state === 'complete').length, expected.length);
  assert.equal((await (await f.sync()).json()).update, undefined); // Never replay finished updates.
});

test('failure halts the entire sequence without retrying or flashing another board', async t => {
  const f = fixture(t); await f.sync(); const id = await f.begin(); await f.bundle(id); await f.build(id, 'complete');
  const update = (await (await f.sync()).json()).update;
  f.state.ota = { ...f.state.ota, id: update.id, sha256: update.sha256, state: 'failed', error: 'SD write failed' };
  assert.equal((await (await f.sync()).json()).update, undefined);
  assert.equal((await f.full()).state, 'failed'); assert.match((await f.full()).error, /SD write failed/);
  await f.sync(); assert.equal(f.db.prepare('SELECT count(*) n FROM ota_jobs').get().n, 1);
});

test('Stop cancels a build and rejects late build callbacks', async t => {
  const f = fixture(t); await f.sync(); const id = await f.begin();
  assert.equal((await f.call('/api/devices/clock/actions', { type: 'stop' })).status, 202);
  assert.equal((await f.full()).state, 'cancelled');
  assert.equal((await f.build(id, 'complete')).status, 409);
  await f.sync(); assert.equal(f.db.prepare('SELECT count(*) n FROM ota_jobs').get().n, 0);
});

test('build key and pinned source are checked; build-only validation never creates hardware jobs', async t => {
  const f = fixture(t); const id = await f.begin({ validateOnly: true });
  assert.equal((await f.build(id, 'complete', {}, { 'X-Firmware-Build-Key': 'wrong' })).status, 401);
  assert.equal((await f.build(id, 'complete', {}, { 'X-Firmware-Source': 'b'.repeat(40) })).status, 409);
  await f.bundle(id); assert.equal((await f.build(id, 'complete')).status, 200);
  assert.equal((await f.full()).state, 'complete');
  assert.equal(f.db.prepare('SELECT count(*) n FROM ota_jobs').get().n, 0);
});

test('mixed hardware identities cannot be committed; expired builds release the update lock', async t => {
  const f = fixture(t); await f.sync(); const id = await f.begin(); await f.bundle(id);
  await f.upload(id, components[0], machine.replace('123456789abc', 'abcdef123456'));
  assert.equal((await f.build(id, 'complete')).status, 409);
  f.advance(7200001); assert.equal((await f.full()).state, 'failed');
  assert.equal(f.db.prepare('SELECT count(*) n FROM ota_jobs').get().n, 0);
});


test('Stop during installation cancels the remaining sequence after the current flash', async t => {
  const f = fixture(t); await f.sync(); const id = await f.begin(); await f.bundle(id); await f.build(id, 'complete');
  const update = (await (await f.sync()).json()).update;
  assert.equal((await f.call('/api/devices/clock/actions', {type: 'stop'})).status, 202);
  f.state.ota = {...f.state.ota, id: update.id, sha256: update.sha256, state: 'complete'};
  await f.sync(); await f.sync();
  assert.equal((await f.full()).state, 'cancelled');
  assert.equal(f.db.prepare('SELECT count(*) n FROM ota_jobs').get().n, 1);
});

test('build failures release the lock and expose a useful error', async t => {
  const f = fixture(t); const id = await f.begin({validateOnly: true});
  assert.equal((await f.build(id, 'failed', {error: 'Compiler failed'})).status, 200);
  assert.equal((await f.full()).state, 'failed'); assert.equal((await f.full()).error, 'Compiler failed');
  assert.equal(f.db.prepare('SELECT count(*) n FROM actions').get().n, 0);
});
