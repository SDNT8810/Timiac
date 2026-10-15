const fail = (message, status = 400) => Object.assign(new Error(message), { status });
const json = (body, status = 200) => new Response(JSON.stringify(body), { status, headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' } });
const uuid = value => typeof value === 'string' && /^[0-9a-f]{8}-(?:[0-9a-f]{4}-){3}[0-9a-f]{12}$/.test(value);
const combinations = { master: ['app', 'filesystem'], esp1: ['app'], esp2: ['app'], octopus1: ['octopus'], octopus2: ['octopus'] };
const fields = 'id, target, kind, size, sha256, revision, machine, crc32, created_at';
const hex = bytes => Array.from(bytes, byte => byte.toString(16).padStart(2, '0')).join('');
const crcTable = Uint32Array.from({ length: 256 }, (_, n) => {
  for (let i = 0; i < 8; i++) n = (n >>> 1) ^ ((n & 1) ? 0xEDB88320 : 0);
  return n >>> 0;
});
export function firmwareCrc32(bytes) {
  let crc = 0xFFFFFFFF;
  for (const byte of bytes) crc = (crc >>> 8) ^ crcTable[(crc ^ byte) & 255];
  return (crc ^ 0xFFFFFFFF) >>> 0;
}
export function validateFirmware(bytes, target, kind) {
  if (!combinations[target]?.includes(kind)) throw fail('Choose a valid firmware target');
  const limit = kind === 'filesystem' ? 0x160000 : kind === 'octopus' ? 0x78000 : target === 'master' ? 0x140000 : 0x100000;
  if (bytes.length < 1024 || bytes.length > limit || (kind === 'filesystem' && bytes.length !== limit)) throw fail('Firmware size does not match this board', 413);
  if (kind === 'app' && bytes[0] !== 0xE9) throw fail('Select the application .bin, not a merged image or bootloader');
  if (kind === 'octopus') {
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength), stack = view.getUint32(0, true), reset = view.getUint32(4, true);
    if (stack < 0x20000000 || stack > 0x20020000 || stack % 4 || !(reset & 1) || reset < 0x08008001 || reset >= 0x08080000)
      throw fail('Octopus firmware must preserve the factory 32 KB bootloader');
  }
  const text = new TextDecoder().decode(bytes); // ASCII image markers survive non-fatal binary decoding.
  const marker = kind === 'octopus' ? 'octopus' : kind === 'filesystem' ? 'master-filesystem' : target;
  if (!text.includes('TIMIAC_OTA_ROLE:' + marker)) throw fail('Wrong board image or firmware missing remote-update support');
  const revision = text.match(/20\d\d-\d\d-\d\d\.\d{1,3}(?!\d)/)?.[0], machine = text.match(/TIMIAC2-[0-9a-f]{12}/)?.[0];
  if (!revision || !machine) throw fail('Firmware revision or Timiac machine identity is missing');
  return { revision, machine };
}
async function binaryBody(request, limit) {
  if (request.headers.get('Content-Type') !== 'application/octet-stream') throw fail('A binary .bin file is required', 415);
  if (Number(request.headers.get('Content-Length')) > limit) throw fail('Firmware file is too large', 413);
  const reader = request.body?.getReader(); if (!reader) throw fail('Select a firmware file');
  const chunks = []; let size = 0;
  for (;;) {
    const { done, value } = await reader.read(); if (done) break;
    size += value.length;
    if (size > limit) { await reader.cancel(); throw fail('Firmware file is too large', 413); }
    chunks.push(value);
  }
  const result = new Uint8Array(size); let offset = 0;
  for (const chunk of chunks) { result.set(chunk, offset); offset += chunk.length; }
  return result;
}
export async function firmwareApi(request, env, { administrator, checkOrigin, readJson, deviceState, now }) {
  const { pathname } = new URL(request.url), match = pathname.match(/^\/api\/devices\/([^/]+)\/(firmware|updates)$/);
  if (!match) return null;
  await administrator();
  if (match[1] !== env.TIMIAC_DEVICE_ID) throw fail('Unknown device', 404);
  if (request.method === 'GET' && match[2] === 'firmware') {
    const images = await env.DB.prepare('SELECT ' + fields + ' FROM ota_images WHERE complete = 1 ORDER BY created_at DESC LIMIT 12').all();
    const jobs = await env.DB.prepare('SELECT id, image_id, state, error, created_at FROM ota_jobs ORDER BY created_at DESC LIMIT 8').all();
    return json({ images: images.results, jobs: jobs.results });
  }
  if (request.method !== 'POST') throw fail('Method not allowed', 405);
  checkOrigin();
  if (match[2] === 'firmware') {
    const target = request.headers.get('X-Timiac-Target'), kind = request.headers.get('X-Timiac-Kind');
    const bytes = await binaryBody(request, 0x160000), metadata = validateFirmware(bytes, target, kind);
    const sha256 = hex(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes))), id = crypto.randomUUID(), crc32 = kind === 'octopus' ? firmwareCrc32(bytes) : 0;
    // Keep one week of completed receipts/artifacts without accumulating every
    // firmware upload forever in this clock's existing free-tier database.
    await env.DB.prepare("DELETE FROM full_update_steps WHERE full_id IN (SELECT id FROM full_updates WHERE state NOT IN ('building','installing') AND created_at < ?)").bind(now - 7 * 86400000).run();
    await env.DB.prepare("DELETE FROM full_update_images WHERE full_id IN (SELECT id FROM full_updates WHERE state NOT IN ('building','installing') AND created_at < ?)").bind(now - 7 * 86400000).run();
    await env.DB.prepare("DELETE FROM full_updates WHERE state NOT IN ('building','installing') AND created_at < ?").bind(now - 7 * 86400000).run();
    await env.DB.prepare("DELETE FROM ota_jobs WHERE state NOT IN ('queued','delivered') AND created_at < ?").bind(now - 7 * 86400000).run();
    await env.DB.prepare('DELETE FROM ota_chunks WHERE image_id IN (SELECT id FROM ota_images WHERE created_at < ? AND id NOT IN (SELECT image_id FROM ota_jobs) AND id NOT IN (SELECT image_id FROM full_update_images))').bind(now - 7 * 86400000).run();
    await env.DB.prepare('DELETE FROM ota_images WHERE created_at < ? AND id NOT IN (SELECT image_id FROM ota_jobs) AND id NOT IN (SELECT image_id FROM full_update_images)').bind(now - 7 * 86400000).run();
    await env.DB.prepare('INSERT INTO ota_images(id,target,kind,size,sha256,revision,machine,crc32,created_at,complete) VALUES(?,?,?,?,?,?,?,?,?,0)')
      .bind(id, target, kind, bytes.length, sha256, metadata.revision, metadata.machine, crc32, now).run();
    for (let offset = 0; offset < bytes.length; offset += 65536)
      await env.DB.prepare('INSERT INTO ota_chunks(image_id,offset,data) VALUES(?,?,?)').bind(id, offset, bytes.slice(offset, offset + 65536).buffer).run();
    await env.DB.prepare('UPDATE ota_images SET complete = 1 WHERE id = ?').bind(id).run();
    return json({ id, target, kind, size: bytes.length, sha256, ...metadata }, 201);
  }
  const body = await readJson();
  if (!uuid(body.imageId)) throw fail('Choose an uploaded firmware image');
  const image = await env.DB.prepare('SELECT ' + fields + ' FROM ota_images WHERE id = ? AND complete = 1').bind(body.imageId).first();
  if (!image) throw fail('Firmware image not found', 404);
  const remote = await deviceState(), state = remote.status;
  if (['building', 'installing'].includes(remote.fullUpdate?.state)) throw fail('Full update is already in progress', 409);
  if (!remote.online || !state?.session) throw fail('Master is offline', 409);
  if (!state.ota?.supported) throw fail('Install the initial Wi-Fi update firmware on the master by USB first', 409);
  const board = image.target.endsWith('1') ? state.a : state.b;
  if (image.target !== 'master' && !board?.bridgeConnected) throw fail('Reconnect the target ESP bridge to the master before updating', 409);
  if (image.target.startsWith('esp') && !board?.otaSupported) throw fail('Install this ESP bridge update by USB first', 409);
  if (image.target.startsWith('octopus') && (!board?.otaSupported || !board?.sdOtaSupported)) throw fail('Install the initial Octopus SD update and leave its card inserted', 409);
  if (remote.queued || state.ota.active || state.stopPending || state.a?.busy || state.b?.busy ||
      ['preparing', 'running'].includes(state.operation?.state) || ['running', 'paused', 'uploading'].includes(state.program?.state)) throw fail('Wait for the current operation to finish', 409);
  if (state.control?.owner && !state.control.owner.startsWith('cloud:')) throw fail('A local dashboard has control', 423);
  if (state.firmware && image.machine !== state.firmware) throw fail('This package changes the hardware identity; install a matched package locally', 409);
  const id = crypto.randomUUID();
  const inserted = await env.DB.prepare("INSERT INTO ota_jobs(id,image_id,device_id,session,state,created_at,expires_at) SELECT ?,?,?,?,'queued',?,? WHERE NOT EXISTS (SELECT 1 FROM ota_jobs WHERE state IN ('queued','delivered') AND expires_at > ?) AND NOT EXISTS (SELECT 1 FROM actions WHERE state='queued' AND expires_at > ?) AND NOT EXISTS (SELECT 1 FROM full_updates WHERE state IN ('building','installing'))")
    .bind(id, image.id, env.TIMIAC_DEVICE_ID, state.session, now, now + 60000, now, now).run();
  if (!inserted.meta.changes) throw fail('Another command or firmware update is already queued', 409);
  return json({ id, state: 'queued' }, 202);
}
export async function firmwareDownload(request, env, authenticateDevice) {
  const match = new URL(request.url).pathname.match(/^\/device\/v1\/devices\/([^/]+)\/firmware\/([^/]+)$/);
  if (!match) return null;
  if (match[1] !== env.TIMIAC_DEVICE_ID || !uuid(match[2])) throw fail('Not found', 404);
  await authenticateDevice();
  if (request.method !== 'GET') throw fail('Method not allowed', 405);
  const image = await env.DB.prepare('SELECT size, sha256 FROM ota_images WHERE id = ? AND complete = 1').bind(match[2]).first();
  if (!image) throw fail('Firmware image not found', 404);
  const rows = await env.DB.prepare('SELECT data FROM ota_chunks WHERE image_id = ? ORDER BY offset').bind(match[2]).all();
  const bytes = new Uint8Array(image.size); let offset = 0;
  for (const row of rows.results) {
    const chunk = row.data instanceof ArrayBuffer ? new Uint8Array(row.data) : Uint8Array.from(row.data);
    bytes.set(chunk, offset); offset += chunk.length;
  }
  if (offset !== bytes.length) throw fail('Firmware storage is incomplete', 503);
  return new Response(bytes, { headers: { 'Content-Type': 'application/octet-stream', 'Content-Length': String(bytes.length),
    'Cache-Control': 'no-store, no-transform', 'X-Timiac-SHA256': image.sha256 } });
}
export async function syncFirmware(env, status, now, action, advance = async () => {}) {
  const ota = status.ota;
  if (uuid(ota?.id) && ['complete', 'failed'].includes(ota.state) && /^[a-f0-9]{64}$/.test(ota.sha256 || ''))
    await env.DB.prepare("UPDATE ota_jobs SET state = ?, error = ? WHERE id = ? AND device_id = ? AND state = 'delivered' AND image_id IN (SELECT id FROM ota_images WHERE sha256 = ?)")
      .bind(ota.state, String(ota.error || '').slice(0, 300), ota.id, env.TIMIAC_DEVICE_ID, ota.sha256).run();
  await env.DB.prepare("UPDATE ota_jobs SET state = 'expired', error = 'Update expired or its controller session changed' WHERE (state = 'queued' AND session != ?) OR (state IN ('queued','delivered') AND expires_at <= ?)")
    .bind(status.session, now).run();
  if (action?.type === 'stop') {
    await env.DB.prepare("UPDATE ota_jobs SET state='cancelled' WHERE state='queued'").run(); return null;
  }
  await advance();
  if (action || !ota?.supported || ota.active || status.stopPending || status.a?.busy || status.b?.busy ||
      (status.control?.owner && !status.control.owner.startsWith('cloud:')) ||
      ['preparing', 'running'].includes(status.operation?.state) || ['running', 'paused', 'uploading'].includes(status.program?.state)) return null;
  const job = await env.DB.prepare("UPDATE ota_jobs SET state='delivered', expires_at=? WHERE id=(SELECT id FROM ota_jobs WHERE state='queued' AND device_id=? AND session=? AND expires_at>? ORDER BY created_at LIMIT 1) AND state='queued' RETURNING id,image_id")
    .bind(now + 600000, env.TIMIAC_DEVICE_ID, status.session, now).first();
  if (!job) return null;
  const image = await env.DB.prepare('SELECT ' + fields + ' FROM ota_images WHERE id = ? AND complete = 1').bind(job.image_id).first();
  if (!image) return null;
  return { ...image, id: job.id, imageId: image.id, session: status.session };
}
