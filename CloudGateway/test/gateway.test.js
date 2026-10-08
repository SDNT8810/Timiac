import test from 'node:test';
import assert from 'node:assert/strict';
import { DatabaseSync } from 'node:sqlite';
import { readFileSync } from 'node:fs';
import { generateKeyPair, SignJWT, jwtVerify } from 'jose';
import { createGateway } from '../cloudflare/worker.js';

const { privateKey, publicKey } = await generateKeyPair('RS256');
const issuer = 'https://securetoken.google.com/test-project';
async function token(overrides = {}, audience = 'test-project', expiry = '1h') {
  return new SignJWT({ email: 'andrea@timiac.it', firebase: { sign_in_provider: 'password' }, ...overrides })
    .setProtectedHeader({ alg: 'RS256' }).setSubject(overrides.sub || 'andrea')
    .setIssuer(issuer).setAudience(audience).setIssuedAt().setExpirationTime(expiry).sign(privateKey);
}
const goodToken = await token();
function fixture(t) {
  const sql = new DatabaseSync(':memory:');
  sql.exec(readFileSync(new URL('../cloudflare/schema.sql', import.meta.url), 'utf8'));
  t.after(() => sql.close());
  const DB = { prepare(query) {
    let args = [];
    return { bind(...values) { args = values; return this; },
      async first() { return sql.prepare(query).get(...args) || null; },
      async run() { const result = sql.prepare(query).run(...args); return { meta: { changes: Number(result.changes) } }; },
    };
  }};
  let now = Date.now();
  const env = { DB, TIMIAC_PUBLIC_ORIGIN: 'https://control.timiac.it', TIMIAC_DEVICE_ID: 'clock-01',
    TIMIAC_ADMIN_USERNAME: 'Andrea', TIMIAC_ADMIN_EMAIL: 'andrea@timiac.it', TIMIAC_ADMIN_UID: 'andrea',
    FIREBASE_PROJECT_ID: 'test-project', FIREBASE_API_KEY: 'test-only-api-key', TIMIAC_DEVICE_KEY: 'test-only-device-key',
    ASSETS: { fetch: async () => new Response('<html>Sign in</html>', { headers: { 'Content-Type': 'text/html' } }) },
  };
  const gateway = createGateway({
    clock: () => now,
    verifyToken: async value => (await jwtVerify(value, publicKey, { issuer, audience: 'test-project', algorithms: ['RS256'] })).payload,
    upstream: async (_url, options) => JSON.parse(options.body).password === 'test-password' ?
      Response.json({ localId: 'andrea', idToken: goodToken, expiresIn: '3600' }) : Response.json({ error: 'bad password' }, { status: 400 }),
  });
  const call = (path, body, headers = {}) => gateway.fetch(new Request('https://control.timiac.it' + path, {
    method: body === undefined ? 'GET' : 'POST',
    headers: { Origin: env.TIMIAC_PUBLIC_ORIGIN, 'Content-Type': 'application/json', ...headers },
    ...(body === undefined ? {} : { body: JSON.stringify(body) }),
  }), env);
  const admin = { Cookie: '__Host-timiac-session=' + goodToken };
  const device = { 'X-Timiac-Device-Key': env.TIMIAC_DEVICE_KEY };
  const state = { session: 'boot-one', machineReady: true, operation: { state: 'complete' }, a: { homed: 63 }, b: { homed: 63 } };
  const sync = status => call('/device/v1/devices/clock-01/sync', { status: status || state }, device);
  const action = (type, values = {}) => call('/api/devices/clock-01/actions', { type, ...values }, admin);
  return { call, admin, device, sync, action, state, sql, advance: ms => { now += ms; } };
}

test('hardware status and commands require an authenticated administrator', async t => {
  const f = fixture(t);
  assert.equal((await f.call('/api/devices/clock-01/status')).status, 401);
  assert.equal((await f.call('/api/devices/clock-01/actions', { type: 'home' }, f.device)).status, 401);
  assert.equal((await f.call('/api/devices/other/status', undefined, f.admin)).status, 404);
});
test('password login returns only the username and a secure session cookie', async t => {
  const f = fixture(t), response = await f.call('/api/login', { username: 'Andrea', password: 'test-password' });
  assert.equal(response.status, 200);
  assert.deepEqual(await response.json(), { username: 'Andrea' });
  const cookie = response.headers.get('Set-Cookie');
  for (const flag of ['HttpOnly', 'Secure', 'SameSite=Strict', 'Path=/', 'Max-Age=3600']) assert.ok(cookie.includes(flag));
  assert.equal((await f.call('/api/session', undefined, f.admin)).status, 200);
});
test('incorrect usernames and passwords fail without exposing provider errors', async t => {
  const f = fixture(t);
  for (const body of [{ username: 'Andrea', password: 'wrong' }, { username: 'somebody', password: 'test-password' }]) {
    const r = await f.call('/api/login', body); assert.equal(r.status, 401);
    assert.deepEqual(await r.json(), { error: 'Incorrect username or password' });
  }
});
test('login attempts are limited in the database and recover after 15 minutes', async t => {
  const f = fixture(t);
  for (let i = 0; i < 5; i++) assert.equal((await f.call('/api/login', { username: 'Andrea', password: 'wrong' })).status, 401);
  assert.equal((await f.call('/api/login', { username: 'Andrea', password: 'test-password' })).status, 429);
  f.advance(900001);
  assert.equal((await f.call('/api/login', { username: 'Andrea', password: 'test-password' })).status, 200);
});
test('cross-site login, logout and control requests are rejected', async t => {
  const f = fixture(t);
  for (const [path, body] of [['/api/login', { username: 'Andrea', password: 'test-password' }], ['/api/logout', {}], ['/api/devices/clock-01/actions', { type: 'home' }]]) {
    assert.equal((await f.call(path, body, { ...f.admin, Origin: 'https://untrusted.example' })).status, 403);
  }
  const r = await f.call('/api/logout', {}, f.admin); assert.equal(r.status, 200);
  assert.ok(r.headers.get('Set-Cookie').includes('Max-Age=0'));
});
test('expired or wrong-audience tokens fail and other users or Google logins are denied', async t => {
  const f = fixture(t);
  for (const [value, status] of [[await token({}, 'wrong-project'), 401], [await token({}, 'test-project', 1), 401],
    [await token({ sub: 'someone-else' }), 403], [await token({ firebase: { sign_in_provider: 'google.com' } }), 403],
    [goodToken.slice(0, -5) + 'xxxxx', 401]]) {
    assert.equal((await f.call('/api/session', undefined, { Cookie: '__Host-timiac-session=' + value })).status, status);
  }
});
test('hardware sync requires its own key, and never accepts a browser session', async t => {
  const f = fixture(t);
  assert.equal((await f.call('/device/v1/devices/clock-01/sync', { status: f.state }, f.admin)).status, 401);
  assert.equal((await f.call('/device/v1/devices/clock-01/sync', { status: f.state }, { 'X-Timiac-Device-Key': 'wrong' })).status, 401);
  assert.equal((await f.sync()).status, 200);
  assert.equal((await f.call('/device/v1/devices/clock-01/sync', { status: [] }, f.device)).status, 400);
});
test('unseen and stale masters are offline and cannot receive new commands', async t => {
  const f = fixture(t);
  let r = await f.call('/api/devices/clock-01/status', undefined, f.admin);
  assert.deepEqual(await r.json(), { online: false, updatedAt: '', status: null, queued: 0 });
  assert.equal((await f.action('home')).status, 409);
  await f.sync(); f.advance(30001);
  assert.equal((await f.action('home')).status, 409);
  r = await f.call('/api/devices/clock-01/status', undefined, f.admin);
  assert.equal((await r.json()).online, false);
});
test('commands are delivered once, and old delivered receipts never block the queue', async t => {
  const f = fixture(t); await f.sync();
  for (let i = 0; i < 30; i++) {
    const queued = await f.action('home'); assert.equal(queued.status, 202);
    const id = (await queued.json()).id, result = await (await f.sync()).json();
    assert.equal(result.action.id, id); assert.equal(result.action.session, 'boot-one');
    assert.equal((await (await f.sync()).json()).action, null);
  }
});
test('expired commands and commands from an earlier boot are never replayed', async t => {
  const f = fixture(t); await f.sync(); await f.action('close'); f.advance(60001);
  assert.equal((await (await f.sync()).json()).action, null);
  await f.action('home');
  assert.equal((await (await f.sync({ ...f.state, session: 'boot-two' })).json()).action, null);
});
test('Stop jumps ahead of motion and cancels every older queued command', async t => {
  const f = fixture(t); await f.sync(); await f.action('close');
  const busy = { ...f.state, operation: { state: 'running' } };
  assert.equal((await (await f.sync(busy)).json()).action, null);
  assert.equal((await f.action('stop')).status, 202);
  assert.equal((await (await f.sync(busy)).json()).action.type, 'stop');
  assert.equal((await (await f.sync()).json()).action, null);
});
test('busy programs and local control refuse remote movement but allow Stop', async t => {
  for (const state of [{ program: { state: 'running' } }, { control: { owner: 'local-phone' } }]) {
    const f = fixture(t); await f.sync({ ...f.state, ...state });
    assert.ok([409, 423].includes((await f.action('home')).status));
    assert.equal((await f.action('stop')).status, 202);
  }
});
test('the speed range is validated and valid speed values reach the hardware unchanged', async t => {
  const f = fixture(t); await f.sync();
  for (const percent of [19, 101, 50.5, '50', null]) assert.equal((await f.action('set-speed', { percent })).status, 400);
  for (const percent of [20, 50, 100]) {
    assert.equal((await f.action('set-speed', { percent })).status, 202);
    assert.equal((await (await f.sync()).json()).action.percent, percent);
  }
  assert.equal((await f.action('set-time', { localTime: 'not-a-date' })).status, 400);
  assert.equal((await f.action('gcode')).status, 400);
});
test('only one normal command can be queued, including concurrent requests', async t => {
  const f = fixture(t); await f.sync();
  const replies = await Promise.all([f.action('home'), f.action('close')]);
  assert.deepEqual(replies.map(r => r.status).sort(), [202, 409]);
});
test('static pages have a restrictive policy and API responses are never cached', async t => {
  const f = fixture(t), page = await f.call('/');
  assert.equal(page.status, 200);
  assert.ok(page.headers.get('Content-Security-Policy').includes("frame-ancestors 'none'"));
  assert.equal((await f.call('/api/config')).headers.get('Cache-Control'), 'no-store');
  assert.equal((await f.call('/api/not-found')).status, 404);
});
