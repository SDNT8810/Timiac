import { createRemoteJWKSet, jwtVerify } from 'jose';

const keys = createRemoteJWKSet(new URL('https://www.googleapis.com/service_accounts/v1/jwk/securetoken@system.gserviceaccount.com'));
const COOKIE = '__Host-timiac-session';
const ONLINE_MS = 30000, ACTION_MS = 60000, LOGIN_WINDOW_MS = 900000;
const actionTypes = new Set(['home', 'open', 'close', 'show-time', 'next-five', 'next-hour', 'struggle', 'stop', 'set-time', 'set-speed']);
const failure = (message, status = 400) => Object.assign(new Error(message), { status });
const json = (body, status = 200, headers = {}) => new Response(JSON.stringify(body), {
  status, headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store', ...headers },
});
const sessionCookie = (token, seconds) => COOKIE + '=' + token + '; Path=/; HttpOnly; Secure; SameSite=Strict; Max-Age=' + seconds;

function protect(response) {
  const result = new Response(response.body, response);
  result.headers.set('X-Content-Type-Options', 'nosniff');
  result.headers.set('Referrer-Policy', 'same-origin');
  result.headers.set('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'");
  return result;
}
function cookieToken(request) {
  return (request.headers.get('Cookie') || '').split(';').map(part => part.trim())
    .find(part => part.startsWith(COOKIE + '='))?.slice(COOKIE.length + 1) || '';
}
async function readJson(request, maxBytes = 32768) {
  if (!request.headers.get('Content-Type')?.toLowerCase().startsWith('application/json')) throw failure('JSON is required', 415);
  const text = await request.text();
  if (new TextEncoder().encode(text).length > maxBytes) throw failure('Request is too large', 413);
  try {
    const value = JSON.parse(text);
    if (!value || typeof value !== 'object' || Array.isArray(value)) throw new Error();
    return value;
  } catch { throw failure('Invalid JSON'); }
}
function checkOrigin(request, env) {
  if (request.headers.get('Origin') !== env.TIMIAC_PUBLIC_ORIGIN) throw failure('Request origin is not allowed', 403);
}
async function verifyFirebaseToken(token, env) {
  const { payload } = await jwtVerify(token, keys, {
    issuer: 'https://securetoken.google.com/' + env.FIREBASE_PROJECT_ID,
    audience: env.FIREBASE_PROJECT_ID,
    algorithms: ['RS256'],
  });
  return payload;
}
async function administrator(request, env, verifyToken) {
  const token = cookieToken(request);
  if (!token || token.length > 8192) throw failure('Please sign in', 401);
  let claims;
  try { claims = await verifyToken(token, env); } catch { throw failure('Your session expired. Please sign in again', 401); }
  if (claims.sub !== env.TIMIAC_ADMIN_UID || claims.email !== env.TIMIAC_ADMIN_EMAIL || claims.firebase?.sign_in_provider !== 'password')
    throw failure('This account does not have access to Timiac', 403);
  return env.TIMIAC_ADMIN_USERNAME;
}
async function digest(value) {
  const hash = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(value));
  return Array.from(new Uint8Array(hash), byte => byte.toString(16).padStart(2, '0')).join('');
}
async function loginLimit(request, env, now) {
  // Cloudflare supplies this IP header. D1 shares the limit across instances.
  const id = await digest(request.headers.get('CF-Connecting-IP') || 'local');
  const row = await env.DB.prepare('INSERT INTO login_limits(id, attempts, window_start) VALUES(?, 1, ?) ' +
    'ON CONFLICT(id) DO UPDATE SET ' +
    'attempts = CASE WHEN window_start <= ? THEN 1 ELSE attempts + 1 END, ' +
    'window_start = CASE WHEN window_start <= ? THEN excluded.window_start ELSE window_start END ' +
    'RETURNING attempts, window_start').bind(id, now, now - LOGIN_WINDOW_MS, now - LOGIN_WINDOW_MS).first();
  if (row.attempts > 5) throw failure('Too many login attempts. Try again in 15 minutes', 429);
  return id;
}
async function authenticateDevice(request, env) {
  const candidate = request.headers.get('X-Timiac-Device-Key') || '';
  if (candidate.length > 256 || !env.TIMIAC_DEVICE_KEY || !candidate) throw failure('Device authentication failed', 401);
  // Compare fixed-size MACs using native verification, rather than comparing
  // secret strings. Browser sessions never authorize the hardware endpoint.
  const encoder = new TextEncoder(), options = { name: 'HMAC', hash: 'SHA-256' };
  const key = await crypto.subtle.importKey('raw', encoder.encode(env.TIMIAC_DEVICE_KEY), options, false, ['sign']);
  const signature = await crypto.subtle.sign('HMAC', key, encoder.encode('timiac-device'));
  const other = await crypto.subtle.importKey('raw', encoder.encode(candidate), options, false, ['verify']);
  if (!await crypto.subtle.verify('HMAC', other, signature, encoder.encode('timiac-device'))) throw failure('Device authentication failed', 401);
}
async function deviceState(env, now) {
  const row = await env.DB.prepare('SELECT status, updated_at FROM devices WHERE id = ?').bind(env.TIMIAC_DEVICE_ID).first();
  if (!row) return { online: false, updatedAt: '', status: null, queued: 0 };
  const pending = await env.DB.prepare("SELECT COUNT(*) AS count FROM actions WHERE device_id = ? AND state = 'queued' AND expires_at > ?")
    .bind(env.TIMIAC_DEVICE_ID, now).first();
  return { online: now - row.updated_at <= ONLINE_MS, updatedAt: new Date(row.updated_at).toISOString(), status: JSON.parse(row.status), queued: pending.count };
}

export function createGateway({ verifyToken = verifyFirebaseToken, upstream = fetch, clock = Date.now } = {}) {
  return {
    async fetch(request, env) {
      try {
        const { pathname } = new URL(request.url), now = clock();
        if (pathname === '/healthz' && request.method === 'GET') return protect(json({ ok: true }));
        if (pathname === '/api/config' && request.method === 'GET') return protect(json({ deviceId: env.TIMIAC_DEVICE_ID }));
        if (pathname === '/api/login' && request.method === 'POST') {
          checkOrigin(request, env);
          const body = await readJson(request, 2048), limitId = await loginLimit(request, env, now);
          if (typeof body.username !== 'string' || body.username.trim().toLowerCase() !== env.TIMIAC_ADMIN_USERNAME.toLowerCase() ||
              typeof body.password !== 'string' || !body.password || body.password.length > 256) throw failure('Incorrect username or password', 401);
          const response = await upstream('https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=' + env.FIREBASE_API_KEY, {
            method: 'POST', headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ email: env.TIMIAC_ADMIN_EMAIL, password: body.password, returnSecureToken: true }),
          });
          if (!response.ok) throw failure('Incorrect username or password', 401);
          const signedIn = await response.json();
          if (signedIn.localId !== env.TIMIAC_ADMIN_UID || !signedIn.idToken) throw failure('This account does not have access to Timiac', 403);
          await administrator(new Request(request.url, { headers: { Cookie: COOKIE + '=' + signedIn.idToken } }), env, verifyToken);
          await env.DB.prepare('DELETE FROM login_limits WHERE id = ?').bind(limitId).run();
          return protect(json({ username: env.TIMIAC_ADMIN_USERNAME }, 200, {
            'Set-Cookie': sessionCookie(signedIn.idToken, Math.min(Number(signedIn.expiresIn) || 3600, 3600)),
          }));
        }
        if (pathname === '/api/logout' && request.method === 'POST') {
          checkOrigin(request, env);
          return protect(json({ ok: true }, 200, { 'Set-Cookie': sessionCookie('', 0) }));
        }
        if (pathname === '/api/session' && request.method === 'GET')
          return protect(json({ username: await administrator(request, env, verifyToken) }));
        const apiMatch = pathname.match(/^\/api\/devices\/([^/]+)\/(status|actions)$/);
        if (apiMatch) {
          const username = await administrator(request, env, verifyToken);
          if (apiMatch[1] !== env.TIMIAC_DEVICE_ID) throw failure('Unknown device', 404);
          if (apiMatch[2] === 'status' && request.method === 'GET') return protect(json(await deviceState(env, now)));
          if (apiMatch[2] === 'actions' && request.method === 'POST') {
            checkOrigin(request, env);
            const body = await readJson(request, 2048);
            if (!actionTypes.has(body.type)) throw failure('Unsupported action');
            if (body.type === 'set-time' && (typeof body.localTime !== 'string' || !/^20\d\d-\d\d-\d\dT\d\d:\d\d:\d\d$/.test(body.localTime))) throw failure('Invalid local time');
            if (body.type === 'set-speed' && (!Number.isInteger(body.percent) || body.percent < 20 || body.percent > 100)) throw failure('Speed must be a whole percentage from 20 to 100');
            const remote = await deviceState(env, now);
            if (!remote.online || !remote.status?.session) throw failure('The master is offline. Reconnect it before sending a command', 409);
            const state = remote.status;
            if (body.type !== 'stop' && (remote.queued || state.stopPending || state.a?.busy || state.b?.busy ||
                ['preparing', 'running'].includes(state.operation?.state) || ['running', 'paused', 'uploading'].includes(state.program?.state)))
              throw failure('Wait for the current command to finish', 409);
            if (body.type !== 'stop' && state.control?.owner && !state.control.owner.startsWith('cloud:')) throw failure('A local dashboard has control', 423);
            const id = crypto.randomUUID();
            // The INSERT checks the queue size atomically to reject races.
            const queued = await env.DB.prepare('INSERT INTO actions(id, device_id, type, local_time, percent, session, requested_by, created_at, expires_at) ' +
              "SELECT ?, ?, ?, ?, ?, ?, ?, ?, ? WHERE (SELECT COUNT(*) FROM actions WHERE device_id = ? AND state = 'queued' AND expires_at > ?) < ?")
              .bind(id, env.TIMIAC_DEVICE_ID, body.type, body.localTime || '', body.type === 'set-speed' ? body.percent : null,
                state.session, username, now, now + ACTION_MS, env.TIMIAC_DEVICE_ID, now, body.type === 'stop' ? 10 : 1).run();
            if (!queued.meta.changes) throw failure('Another command is already queued', 409);
            return protect(json({ id, state: 'queued' }, 202));
          }
          throw failure('Method not allowed', 405);
        }
        const deviceMatch = pathname.match(/^\/device\/v1\/devices\/([^/]+)\/sync$/);
        if (deviceMatch && request.method === 'POST') {
          if (deviceMatch[1] !== env.TIMIAC_DEVICE_ID) throw failure('Unknown device', 404);
          await authenticateDevice(request, env);
          const body = await readJson(request), status = body.status;
          if (!status || typeof status !== 'object' || Array.isArray(status) || typeof status.session !== 'string' || !status.session || status.session.length > 96)
            throw failure('Invalid device status');
          await env.DB.prepare('INSERT INTO devices(id, status, updated_at) VALUES(?, ?, ?) ' +
            'ON CONFLICT(id) DO UPDATE SET status = excluded.status, updated_at = excluded.updated_at')
            .bind(env.TIMIAC_DEVICE_ID, JSON.stringify(status), now).run();
          const ready = !status.stopPending && !status.fault && !status.a?.busy && !status.b?.busy &&
            !['preparing', 'running'].includes(status.operation?.state) && !['running', 'paused', 'uploading'].includes(status.program?.state);
          const action = await env.DB.prepare("UPDATE actions SET state = 'delivered', delivered_at = ? WHERE id = (" +
            "SELECT id FROM actions WHERE device_id = ? AND state = 'queued' AND expires_at > ? AND session = ? " +
            "AND (type = 'stop' OR ? = 1) ORDER BY CASE WHEN type = 'stop' THEN 0 ELSE 1 END, created_at, rowid LIMIT 1 " +
            ") AND state = 'queued' RETURNING id, type, local_time AS localTime, percent, session")
            .bind(now, env.TIMIAC_DEVICE_ID, now, status.session, ready ? 1 : 0).first();
          if (action?.type === 'stop') {
            await env.DB.prepare("UPDATE actions SET state = 'cancelled' WHERE device_id = ? AND state = 'queued'")
              .bind(env.TIMIAC_DEVICE_ID).run();
          }
          // Commands expire after one minute and are bound to the master's
          // boot session. Keep only one day of receipts for diagnostics.
          await env.DB.prepare('DELETE FROM actions WHERE expires_at <= ? OR (device_id = ? AND session != ?)')
            .bind(now - 86400000, env.TIMIAC_DEVICE_ID, status.session).run();
          return protect(json({ action }));
        }
        if (pathname.startsWith('/api/') || pathname.startsWith('/device/')) throw failure('Not found', 404);
        if (!['GET', 'HEAD'].includes(request.method)) throw failure('Method not allowed', 405);
        return protect(await env.ASSETS.fetch(request));
      } catch (error) {
        return protect(json({ error: error.status ? error.message : 'Gateway temporarily unavailable' }, error.status || 503));
      }
    },
  };
}
export default createGateway();
