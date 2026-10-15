// Durable build/install coordinator. No browser or permanently connected PC is required.
import { firmwareApi } from './firmware.js';

const fail = (message, status = 400) => Object.assign(new Error(message), { status });
const json = (body, status = 200) => Response.json(body, { status, headers: { 'Cache-Control': 'no-store' } });
const active = "state IN ('building','installing')";
const uuid = value => /^[0-9a-f]{8}-(?:[0-9a-f]{4}-){3}[0-9a-f]{12}$/.test(value || '');
const family = env => env.TIMIAC_DEVICE_ID ? 'TIMIAC' : 'LUMIAC';
const device = env => env[family(env) + '_DEVICE_ID'];
const origin = env => env[family(env) + '_PUBLIC_ORIGIN'];
const order = env => family(env) === 'TIMIAC'
  ? [['esp1', 'app'], ['esp2', 'app'], ['octopus1', 'octopus'], ['octopus2', 'octopus'], ['master', 'app'], ['master', 'filesystem']]
  : [['octopus', 'octopus'], ['esp12', 'app'], ['esp32', 'app']];
export const fullUpdateActive = update => ['building', 'installing'].includes(update?.state);

export async function cancelFullUpdate(env, now) {
  await env.DB.prepare(`UPDATE full_updates SET state='cancelled', error='Cancelled by Stop', updated_at=? WHERE ${active}`).bind(now).run();
  await env.DB.prepare("UPDATE ota_jobs SET state='cancelled' WHERE state='queued' AND id IN (SELECT job_id FROM full_update_steps s JOIN full_updates f ON f.id=s.full_id WHERE f.state='cancelled')").run();
}
export async function fullUpdateState(env, now) {
  await env.DB.prepare(`UPDATE full_updates SET state='failed', error='Full update timed out; check the build log and connection before trying again', updated_at=? WHERE ${active} AND expires_at<=?`).bind(now, now).run();
  await env.DB.prepare("UPDATE ota_jobs SET state='cancelled' WHERE state='queued' AND id IN (SELECT job_id FROM full_update_steps s JOIN full_updates f ON f.id=s.full_id WHERE f.state='failed')").run();
  const row = await env.DB.prepare('SELECT id,state,error,source_sha,created_at,updated_at,validate_only FROM full_updates WHERE device_id=? ORDER BY created_at DESC,rowid DESC LIMIT 1').bind(device(env)).first();
  if (!row) return null;
  const steps = await env.DB.prepare('SELECT s.position,s.target,s.kind,j.state,j.error FROM full_update_steps s LEFT JOIN ota_jobs j ON j.id=s.job_id WHERE full_id=? ORDER BY position').bind(row.id).all();
  return { ...row, steps: steps.results, buildUrl: 'https://github.com/' + env.GITHUB_REPOSITORY + '/actions/workflows/full-update.yml' };
}
function checkHardware(env, state) {
  if (!state?.session || !state.ota?.supported) throw fail('Install the initial USB/SD update package first', 409);
  if (state.fault) throw fail('Resolve the board fault before updating', 409);
  if (family(env) === 'TIMIAC') {
    for (const board of [state.a, state.b]) {
      if (!board?.bridgeConnected || !board?.octopusConnected || !board?.otaSupported || !board?.sdOtaSupported)
        throw fail('Both ESP bridges and Octopus boards must be connected, with update support and microSD cards installed', 409);
      if (board.busy || board.fault) throw fail('Stop movement and resolve board faults before updating', 409);
    }
    if (state.stopPending || ['preparing', 'running'].includes(state.operation?.state) || ['running', 'paused', 'uploading'].includes(state.program?.state))
      throw fail('Stop the current operation before updating', 409);
    if (state.control?.owner && !state.control.owner.startsWith('cloud:')) throw fail('A local dashboard has control', 423);
  } else if (!['esp12', 'esp32'].includes(state.controller) || !state.octopusOnline || !state.ota.sdSupported) {
    throw fail('Connect the Octopus with update support and its microSD card installed', 409);
  }
}
async function buildAuth(request, env) {
  const candidate = request.headers.get('X-Firmware-Build-Key') || '';
  if (!env.FIRMWARE_BUILD_KEY || !candidate || candidate.length > 256) throw fail('Build authentication failed', 401);
  const opts = { name: 'HMAC', hash: 'SHA-256' }, enc = new TextEncoder();
  const key = await crypto.subtle.importKey('raw', enc.encode(env.FIRMWARE_BUILD_KEY), opts, false, ['sign']);
  const mac = await crypto.subtle.sign('HMAC', key, enc.encode('firmware-build'));
  const other = await crypto.subtle.importKey('raw', enc.encode(candidate), opts, false, ['verify']);
  if (!await crypto.subtle.verify('HMAC', other, mac, enc.encode('firmware-build'))) throw fail('Build authentication failed', 401);
}
async function github(env, upstream, path, body) {
  const response = await upstream('https://api.github.com/repos/' + env.GITHUB_REPOSITORY + path, {
    method: body ? 'POST' : 'GET', headers: { Authorization: 'Bearer ' + env.GITHUB_BUILD_TOKEN,
      Accept: 'application/vnd.github+json', 'X-GitHub-Api-Version': '2022-11-28', 'User-Agent': 'Clock-Firmware-Builder', 'Content-Type': 'application/json' },
    ...(body ? { body: JSON.stringify(body) } : {}),
  });
  if (!response.ok) throw fail('Could not start the firmware build (GitHub HTTP ' + response.status + '). Check build access and try again', 502);
  return response.status === 204 ? null : response.json();
}
export async function fullUpdateApi(request, env, { administrator, checkOrigin, readJson, deviceState, now, upstream }) {
  const path = new URL(request.url).pathname;
  const api = path.match(/^\/api\/devices\/([^/]+)\/full-update$/);
  const build = path.match(/^\/build\/v1\/full-updates\/([^/]+)\/(images|complete|failed)$/);
  if (!api && !build) return null;
  if (api) {
    await administrator();
    if (api[1] !== device(env)) throw fail('Unknown device', 404);
    if (request.method === 'GET') return json({ update: await fullUpdateState(env, now) });
    if (request.method !== 'POST') throw fail('Method not allowed', 405);
    checkOrigin();
    if (!env.GITHUB_BUILD_TOKEN || !env.FIRMWARE_BUILD_KEY || !env.GITHUB_REPOSITORY || !env.GITHUB_REF) throw fail('Automatic builds are not configured yet', 503);
    const body = await readJson();
    if (Object.keys(body).some(key => key !== 'validateOnly') || (body.validateOnly !== undefined && typeof body.validateOnly !== 'boolean')) throw fail('Invalid full update request');
    const validateOnly = body.validateOnly === true;
    const remote = await deviceState();
    if (!validateOnly) {
      if (!remote.online) throw fail('Connect the clock to the Internet before starting Full update', 409);
      checkHardware(env, remote.status);
      if (remote.queued || remote.status.ota.active) throw fail('Wait for the queued command or update to finish', 409);
    }
    const current = await fullUpdateState(env, now);
    if (fullUpdateActive(current)) throw fail('A full update is already in progress', 409);
    const commit = await github(env, upstream, '/commits/' + encodeURIComponent(env.GITHUB_REF));
    if (!/^[a-f0-9]{40}$/.test(commit?.sha || '')) throw fail('Could not identify the source revision', 502);
    const id = crypto.randomUUID();
    const inserted = await env.DB.prepare(`INSERT INTO full_updates(id,device_id,state,error,source_sha,controller,validate_only,created_at,updated_at,expires_at) SELECT ?,?,'building','',?,?,?,?,?,? WHERE NOT EXISTS(SELECT 1 FROM full_updates WHERE ${active}) AND NOT EXISTS(SELECT 1 FROM ota_jobs WHERE state IN ('queued','delivered') AND expires_at>?) AND NOT EXISTS(SELECT 1 FROM actions WHERE state='queued' AND expires_at>?)`)
      .bind(id, device(env), commit.sha, remote.status?.controller || 'master', validateOnly ? 1 : 0, now, now, now + 7200000, now, now).run();
    if (!inserted.meta.changes) throw fail('Another command or update is already queued', 409);
    // Park the clock/loop while the build runs, so an automatic minute change
    // cannot race the first flash. Build-only verification never sends Stop.
    if (!validateOnly) await env.DB.prepare("INSERT INTO actions(id,device_id,type,session,requested_by,created_at,expires_at) SELECT ?,?,'stop',?,'full-update',?,? WHERE EXISTS(SELECT 1 FROM full_updates WHERE id=? AND state='building')")
      .bind(crypto.randomUUID(), device(env), remote.status.session, now, now + 60000, id).run();
    try {
      await github(env, upstream, '/actions/workflows/full-update.yml/dispatches', { ref: env.GITHUB_REF, inputs: { update_id: id, source_sha: commit.sha } });
    } catch (error) {
      await env.DB.prepare("UPDATE full_updates SET state='failed',error=?,updated_at=? WHERE id=? AND state='building'").bind(error.message, now, id).run();
      throw error;
    }
    return json({ id, state: 'building' }, 202);
  }
  await buildAuth(request, env);
  if (!uuid(build[1])) throw fail('Unknown full update', 404);
  if (request.method !== 'POST') throw fail('Method not allowed', 405);
  await fullUpdateState(env, now);
  const row = await env.DB.prepare('SELECT * FROM full_updates WHERE id=?').bind(build[1]).first();
  if (!row) throw fail('Unknown full update', 404);
  if (row.state !== 'building') throw fail('This build is no longer accepting firmware', 409);
  if (request.headers.get('X-Firmware-Source') !== row.source_sha) throw fail('Build source does not match the requested revision', 409);
  if (build[2] === 'failed') {
    const body = await readJson();
    await env.DB.prepare("UPDATE full_updates SET state='failed',error=?,updated_at=? WHERE id=? AND state='building'").bind(String(body.error || 'Firmware build failed. Check the build log.').slice(0, 300), now, row.id).run();
    return json({ ok: true });
  }
  if (build[2] === 'images') {
    const prefix = family(env) === 'TIMIAC' ? 'Timiac' : 'Lumiac';
    const target = request.headers.get('X-' + prefix + '-Target'), kind = request.headers.get('X-' + prefix + '-Kind');
    if (!order(env).some(pair => pair[0] === target && pair[1] === kind)) throw fail('Unknown bundle component');
    const upload = new Request(origin(env) + '/api/devices/' + device(env) + '/firmware', { method: 'POST', headers: request.headers, body: request.body, duplex: 'half' });
    const response = await firmwareApi(upload, env, { administrator: async () => {}, checkOrigin: () => {}, now });
    const image = await response.json();
    await env.DB.prepare("INSERT INTO full_update_images(full_id,target,kind,image_id) SELECT ?,?,?,? WHERE EXISTS(SELECT 1 FROM full_updates WHERE id=? AND state='building') ON CONFLICT(full_id,target,kind) DO UPDATE SET image_id=excluded.image_id")
      .bind(row.id, target, kind, image.id, row.id).run();
    return json(image, 201);
  }
  const images = (await env.DB.prepare('SELECT i.* FROM full_update_images b JOIN ota_images i ON i.id=b.image_id WHERE b.full_id=? AND i.complete=1').bind(row.id).all()).results;
  if (images.length !== order(env).length || order(env).some(([target, kind]) => !images.some(i => i.target === target && i.kind === kind))) throw fail('Build every firmware before installing the bundle', 409);
  if (new Set(images.map(i => i.machine)).size !== 1 || new Set(images.map(i => i.revision)).size !== 1) throw fail('All firmware must belong to the same release and hardware identity', 409);
  if (!row.validate_only) {
    const remote = await deviceState();
    if (!remote.online) throw fail('Clock went offline during the build. Reconnect and start Full update again', 409);
    checkHardware(env, remote.status);
    if ((remote.status.controller || 'master') !== row.controller) throw fail('The connected controller changed during the build', 409);
    if (remote.status.firmware && images[0].machine !== remote.status.firmware) throw fail('This build changes the hardware identity; install a matched package locally', 409);
    const steps = order(env).filter(([target]) => family(env) === 'TIMIAC' || target === 'octopus' || target === row.controller);
    for (let position = 0; position < steps.length; position++) {
      const [target, kind] = steps[position], image = images.find(i => i.target === target && i.kind === kind);
      await env.DB.prepare("INSERT OR IGNORE INTO full_update_steps(full_id,position,target,kind,image_id,job_id) SELECT ?,?,?,?,?,? WHERE EXISTS(SELECT 1 FROM full_updates WHERE id=? AND state='building')")
        .bind(row.id, position, target, kind, image.id, crypto.randomUUID(), row.id).run();
    }
  }
  await env.DB.prepare("UPDATE full_updates SET state=?,updated_at=?,expires_at=? WHERE id=? AND state='building'")
    .bind(row.validate_only ? 'complete' : 'installing', now, now + 2400000, row.id).run();
  return json({ ok: true, state: row.validate_only ? 'complete' : 'installing' });
}

// Called after validating the previous job's SHA receipt, on every hardware sync.
export async function advanceFullUpdate(env, status, now) {
  const full = await fullUpdateState(env, now);
  if (full?.state !== 'installing') return;
  const step = await env.DB.prepare("SELECT s.*,j.state AS job_state,j.error AS job_error FROM full_update_steps s LEFT JOIN ota_jobs j ON j.id=s.job_id WHERE full_id=? AND (j.state IS NULL OR j.state!='complete') ORDER BY position LIMIT 1").bind(full.id).first();
  if (!step) {
    await env.DB.prepare("UPDATE full_updates SET state='complete',updated_at=? WHERE id=? AND state='installing'").bind(now, full.id).run(); return;
  }
  if (['failed', 'expired', 'cancelled'].includes(step.job_state)) {
    await env.DB.prepare("UPDATE full_updates SET state='failed',error=?,updated_at=? WHERE id=? AND state='installing'").bind(step.target + ': ' + (step.job_error || step.job_state), now, full.id).run(); return;
  }
  if (step.job_state || status.ota?.active) return;
  try { checkHardware(env, status); } catch { return; } // Wait for boards to reconnect after reboot.
  await env.DB.prepare("INSERT OR IGNORE INTO ota_jobs(id,image_id,device_id,session,state,created_at,expires_at) SELECT ?,?,?,?,'queued',?,? WHERE EXISTS(SELECT 1 FROM full_updates WHERE id=? AND state='installing') AND NOT EXISTS(SELECT 1 FROM ota_jobs WHERE state IN ('queued','delivered') AND expires_at>?) AND NOT EXISTS(SELECT 1 FROM actions WHERE state='queued' AND expires_at>?)")
    .bind(step.job_id, step.image_id, device(env), status.session, now, now + 60000, full.id, now, now).run();
}
