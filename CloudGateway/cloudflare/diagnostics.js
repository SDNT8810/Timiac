// Hardware-authenticated messages are archived independently of connectivity.
// An offline clock can still be diagnosed from its last snapshot and receipts.
export async function saveDeviceDiagnostics(env, status, now) {
  const boot = status.session;
  const events = [{ id: 'boot', boot, at: 0, text: 'Master connected: firmware ' + String(status.software || 'unknown') }];
  const ota = status.ota;
  if (typeof ota?.id === 'string' && ota.id.length === 36) {
    const phase = String(ota.state || '').slice(0, 48) + ':' + String(ota.phase || '').slice(0, 96);
    const done = Number.isSafeInteger(ota.done) && ota.done >= 0 ? ota.done : 0;
    events.push({ id: 'ota:' + ota.id + ':' + phase + ':' + Math.floor(done / 65536), boot, at: status.reliability?.uptimeMs || 0,
      text: 'OTA ' + String(ota.target || '') + ' ' + String(ota.kind || '') + ' ' + phase + ' ' + done + '/' + (ota.size || 0) + ' ' + String(ota.error || '') });
  }
  for (const event of Array.isArray(status.diagnostics) ? status.diagnostics.slice(0, 8) : []) {
    if (!event || typeof event.id !== 'string' || !/^[pr]\d{1,10}$/.test(event.id) || typeof event.boot !== 'string' || !event.boot || event.boot.length > 96 ||
        typeof event.text !== 'string' || !Number.isSafeInteger(event.at) || event.at < 0) continue;
    events.push(event);
  }
  for (const event of events) {
    await env.DB.prepare('INSERT OR IGNORE INTO diagnostic_events(event_key,device_id,boot,uptime_ms,utc_seconds,message,persistent,received_at) VALUES(?,?,?,?,?,?,?,?)')
      .bind(env.TIMIAC_DEVICE_ID + ':' + event.boot + ':' + event.id, env.TIMIAC_DEVICE_ID, event.boot, event.at,
        Number.isSafeInteger(event.utc) && event.utc >= 0 ? event.utc : 0, event.text.slice(0, 300), event.persistent ? 1 : 0, now).run();
  }
  await env.DB.prepare('DELETE FROM diagnostic_events WHERE received_at<?').bind(now - 30 * 86400000).run();
}
export async function downloadDiagnostics(env) {
  const snapshot = await env.DB.prepare('SELECT status,updated_at FROM devices WHERE id=?').bind(env.TIMIAC_DEVICE_ID).first();
  const events = await env.DB.prepare('SELECT boot,uptime_ms,utc_seconds,message,persistent,received_at FROM diagnostic_events WHERE device_id=? ORDER BY received_at DESC,event_key DESC LIMIT 1000').bind(env.TIMIAC_DEVICE_ID).all();
  const jobs = await env.DB.prepare('SELECT j.id,j.session,j.state,j.error,j.created_at,j.expires_at,j.progress_done,j.progress_state,i.target,i.kind,i.revision,i.size,i.sha256 FROM ota_jobs j JOIN ota_images i ON i.id=j.image_id WHERE j.device_id=? ORDER BY j.created_at DESC LIMIT 30').bind(env.TIMIAC_DEVICE_ID).all();
  return new Response(JSON.stringify({ device: env.TIMIAC_DEVICE_ID, lastContact: snapshot?.updated_at || null,
    lastKnownStatus: snapshot ? JSON.parse(snapshot.status) : null, events: events.results, updates: jobs.results }, null, 2), {
    headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store', 'Content-Disposition': 'attachment; filename="timiac-diagnostics.json"' },
  });
}
