const $ = id => document.getElementById(id);
let config, timer, pendingAction = '', uploadingFirmware = false;
const localDate = () => {
  const d = new Date(), p = n => String(n).padStart(2, '0');
  return d.getFullYear() + '-' + p(d.getMonth()+1) + '-' + p(d.getDate()) + 'T' + p(d.getHours()) + ':' + p(d.getMinutes()) + ':' + p(d.getSeconds());
};
function say(text, error = false) { $('message').textContent = text; $('message').classList.toggle('error', error); }
function showLogin(message = '') {
  clearInterval(timer); timer = null; pendingAction = '';
  $('sign-in').hidden = false; $('dashboard').hidden = true;
  $('login-message').textContent = message; $('password').value = '';
}
async function api(path, options = {}) {
  const response = await fetch(path, { ...options, credentials: 'same-origin', headers: { 'Content-Type': 'application/json', ...options.headers }, cache: 'no-store' });
  const body = await response.json();
  if (!response.ok) {
    if (response.status === 401 && path !== '/api/login') showLogin(body.error);
    throw new Error(body.error || 'HTTP ' + response.status);
  }
  return body;
}
function stateLabel(board) { if (!board?.bridgeConnected) return 'ESP offline'; if (board.updating) return 'Updating firmware'; if (!board.octopusConnected) return 'Octopus offline'; if (board.fault) return 'Stopped'; if (board.busy) return 'Moving'; return board.homed === 63 ? 'Ready' : 'Home required'; }
function commandBlockReason(remote) {
  const state = remote.status || {};
  if (!remote.online) return 'Master is offline';
  if (['building', 'installing'].includes(remote.fullUpdate?.state)) return 'Full update in progress. Stop is available.';
  if (uploadingFirmware || state.ota?.active) return 'Firmware update in progress';
  if (state.stopPending) return 'Waiting for Stop confirmation';
  if (state.fault) return state.fault;
  if (state.control?.owner && !state.control.owner.startsWith('cloud:'))
    return 'A local dashboard has control. Close its tab and wait 15 seconds to use remote control.';
  if (remote.queued > 0) return 'Waiting for the master to receive the queued command (up to one minute)';
  if (state.a?.busy || state.b?.busy || ['preparing', 'running'].includes(state.operation?.state) ||
      ['running', 'paused', 'uploading'].includes(state.program?.state)) return 'A movement is in progress. Stop is available.';
  if (state.clock?.startupPending) return 'Waiting for automatic startup homing';
  if (!state.machineReady) return state.a?.fault || state.b?.fault || 'Waiting for both motor boards';
  return '';
}
function render(remote) {
  const state = remote.status || {};
  const reason = commandBlockReason(remote);
  const homed = state.a?.homed === 63 && state.b?.homed === 63;
  $('connection').textContent = reason || (!homed ? 'Homing required. Use Calibrate arms under Maintenance.' : 'Connected to Timiac');
  $('clock').textContent = state.clock?.valid ? state.clock.localTime.slice(11) : '--:--:--';
  $('date').textContent = state.clock?.internetSync === 'synchronized' ? 'Internet-synchronized clock' : state.clock?.error || 'Local clock time';
  $('clock-mode').textContent = state.clock?.running ? homed ? 'Clock running' : 'Clock waiting for calibrated arms' : 'Clock paused' + (state.clock?.pausedReason ? ': ' + state.clock.pausedReason : '');
  $('stop-history').textContent = state.reliability?.lastStop ? 'Last stop: ' + state.reliability.lastStop : '';
  $('board-a').textContent = stateLabel(state.a); $('board-b').textContent = stateLabel(state.b);
  const busy = Boolean(reason);
  document.querySelectorAll('[data-action]').forEach(button => {
    const action = button.dataset.action;
    const needsTime = action === 'show-time' || action === 'struggle' ||
      (['next-five', 'next-hour'].includes(action) && !(state.clock?.armMinute >= 0));
    const block = reason || (!homed ? 'Home all arms first' : needsTime && !state.clock?.valid ? 'Set the clock time first' : '');
    button.disabled = Boolean(block); button.title = block;
  });
  $('stop').disabled = !remote.online;
  $('save-time').disabled = busy; $('local-time').disabled = busy;
  $('calibrate').disabled = busy;
  $('install-firmware').disabled = busy || !state.ota?.supported;
  const ota = state.ota;
  const full = remote.fullUpdate;
  const job = remote.update;
  $('full-update-log').hidden = !full?.buildUrl;
  if (full?.buildUrl) $('full-update-log').href = full.buildUrl;
  const fullActive = ['building', 'installing'].includes(full?.state);
  $('full-update').disabled = busy || fullActive || !state.ota?.supported;
  const completed = full?.steps?.filter(step => step.state === 'complete').length || 0;
  const current = full?.steps?.find(step => step.state !== 'complete');
  const previousFull = full && !fullActive && job?.createdAt > full.updated_at;
  const fullResult = !full ? 'Build all firmware and install it automatically. No files to select.'
    : full.state === 'building' ? 'Building all firmware. This can take several minutes. You may close this page.'
    : full.state === 'installing' ? `Installing ${completed + 1}/${full.steps.length}: ${current?.target || ''} ${current?.kind === 'filesystem' ? 'dashboard' : ''}. Keep power connected.`
    : full.state === 'complete' ? full.validate_only ? 'Build verified. No hardware was updated.' : 'Full update complete. Calibrate/Home the arms before resuming.'
    : `Full update ${full.state}: ${full.error || 'Check the connection and try again.'}`;
  $('full-update-status').textContent = previousFull ? 'Individual component updates were started after the previous Full update. Latest result is shown below.' : fullResult;
  $('full-update-status').classList.toggle('error', !previousFull && full?.state === 'failed');
  $('full-update-history').hidden = !previousFull;
  $('full-update-history-status').textContent = previousFull ? fullResult : '';
  const jobTerminal = ['complete', 'failed', 'expired', 'cancelled'].includes(job?.state);
  const useJob = job && (job.id !== ota?.id || jobTerminal);
  const progress = ota?.size ? ` · ${Math.min(100, Math.round(ota.done / ota.size * 100))}%` : '';
  $('firmware-status').textContent = !ota?.supported ? 'Install the initial USB/SD update package before using remote updates.'
    : useJob ? `${job.target || 'Update'} · ${job.state}${job.error ? ': ' + job.error : ''}`
    : ota?.id ? `${ota.target} · ${ota.phase || ota.state}${ota.active ? progress : ''}${ota.error ? ': ' + ota.error : ''}`
    : 'Ready for remote updates. Leave a microSD card in each Octopus.';
  $('firmware-status').classList.toggle('error', useJob ? ['failed', 'expired'].includes(job.state) : ota?.state === 'failed');
  if (pendingAction && state.remoteAction?.id === pendingAction) {
    say(state.remoteAction.error || state.remoteAction.type + ' accepted by the master.', Boolean(state.remoteAction.error));
    pendingAction = '';
  } else if (pendingAction && !remote.queued && !remote.online) {
    say('Master disconnected. Commands expire after one minute.', true); pendingAction = '';
  }
}
async function refresh() { try { render(await api('/api/devices/' + config.deviceId + '/status')); } catch (error) { say(error.message, true); } }
async function queue(type, values = {}) {
  const action = await api('/api/devices/' + config.deviceId + '/actions', { method: 'POST', body: JSON.stringify({ type, ...values }) });
  pendingAction = action.id; say(type + ' queued for the master.'); await refresh();
}
async function start() {
  await api('/api/session'); config = await api('/api/config');
  sessionStorage.removeItem('timiac-google-token');
  $('sign-in').hidden = true; $('dashboard').hidden = false; $('local-time').value = localDate();
  await refresh(); clearInterval(timer); timer = setInterval(refresh, 5000);
}
$('login-form').onsubmit = async event => {
  event.preventDefault(); $('login-button').disabled = true; $('login-message').textContent = '';
  try {
    await api('/api/login', { method: 'POST', body: JSON.stringify({ username: $('username').value, password: $('password').value }) });
    $('password').value = ''; await start();
  } catch (error) { $('login-message').textContent = error.message; }
  finally { $('login-button').disabled = false; }
};
document.querySelectorAll('[data-action]').forEach(button => button.onclick = () => queue(button.dataset.action).catch(error => say(error.message, true)));
$('stop').onclick = () => queue('stop').catch(error => say(error.message, true));
$('calibrate').onclick = () => queue('home').catch(error => say(error.message, true));
$('time-form').onsubmit = event => {
  event.preventDefault(); queue('set-time', { localTime: $('local-time').value.length === 16 ? $('local-time').value + ':00' : $('local-time').value }).catch(error => say(error.message, true));
};
$('firmware-form').onsubmit = async event => {
  event.preventDefault();
  const file = $('firmware-file').files[0]; if (!file || uploadingFirmware) return;
  const [target, kind] = $('firmware-target').value.split(':');
  uploadingFirmware = true; $('install-firmware').disabled = true; say('Uploading and checking firmware…');
  try {
    const image = await api('/api/devices/' + config.deviceId + '/firmware', { method: 'POST',
      headers: { 'Content-Type': 'application/octet-stream', 'X-Timiac-Target': target, 'X-Timiac-Kind': kind }, body: file });
    await api('/api/devices/' + config.deviceId + '/updates', { method: 'POST', body: JSON.stringify({ imageId: image.id }) });
    say(`${target} update queued. Keep power connected; the clock will remain paused.`);
    $('firmware-file').value = '';
  } catch (error) { say(error.message, true); }
  finally { uploadingFirmware = false; await refresh(); }
};
$('sign-out').onclick = async () => { try { await api('/api/logout', { method: 'POST' }); showLogin(); } catch (error) { say(error.message, true); } };
document.addEventListener('keydown', event => {
  if (event.key === 'Home' && !$('dashboard').hidden && !['INPUT', 'TEXTAREA', 'SELECT'].includes(event.target.tagName)) {
    event.preventDefault(); const button = document.querySelector('[data-action="open"]'); if (!button.disabled) button.click();
  }
});
start().catch(error => showLogin(error.message === 'Please sign in' ? '' : error.message));

$('full-update').onclick = async () => {
  $('full-update').disabled = true;
  uploadingFirmware = true;
  try {
    await api('/api/devices/' + config.deviceId + '/full-update', { method: 'POST', body: '{}' });
    say('Building all firmware. Updates will install automatically when the build succeeds.');
  } catch (error) { say(error.message, true); }
  finally { uploadingFirmware = false; await refresh(); }
};
