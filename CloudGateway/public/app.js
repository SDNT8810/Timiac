const $ = id => document.getElementById(id);
let config, timer, pendingAction = '';
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
  const response = await fetch(path, { ...options, credentials: 'same-origin', headers: { 'Content-Type': 'application/json' }, cache: 'no-store' });
  const body = await response.json();
  if (!response.ok) {
    if (response.status === 401 && path !== '/api/login') showLogin(body.error);
    throw new Error(body.error || 'HTTP ' + response.status);
  }
  return body;
}
function stateLabel(board) { if (!board?.bridgeConnected) return 'ESP offline'; if (!board.octopusConnected) return 'Octopus offline'; if (board.fault) return 'Stopped'; if (board.busy) return 'Moving'; return board.homed === 63 ? 'Ready' : 'Home required'; }
function render(remote) {
  const state = remote.status || {};
  $('connection').textContent = !remote.online ? 'Master is offline' : state.fault || state.operation?.error || (state.machineReady ? 'Connected to Timiac' : 'Waiting for motor boards');
  $('clock').textContent = state.clock?.valid ? state.clock.localTime.slice(11) : '--:--:--';
  $('date').textContent = state.clock?.internetSync === 'synchronized' ? 'Internet-synchronized clock' : state.clock?.error || 'Local clock time';
  $('clock-mode').textContent = state.clock?.running ? 'Clock running' : 'Clock paused';
  $('board-a').textContent = stateLabel(state.a); $('board-b').textContent = stateLabel(state.b);
  const localControl = state.control?.owner && !state.control.owner.startsWith('cloud:');
  const busy = !remote.online || state.stopPending || state.a?.busy || state.b?.busy || remote.queued > 0 || localControl ||
    ['preparing', 'running'].includes(state.operation?.state) || ['running', 'paused', 'uploading'].includes(state.program?.state);
  document.querySelectorAll('[data-action]').forEach(button => button.disabled = busy || !state.machineReady || (button.dataset.action !== 'home' && (state.a?.homed !== 63 || state.b?.homed !== 63)));
  $('stop').disabled = !remote.online;
  $('speed').disabled = busy; $('save-time').disabled = busy; $('local-time').disabled = busy;
  if (document.activeElement !== $('speed') && !pendingAction) {
    $('speed').value = state.speedPercent || 50; $('speed-value').textContent = $('speed').value + '%';
  }
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
$('speed').oninput = () => { $('speed-value').textContent = $('speed').value + '%'; };
$('speed').onchange = () => queue('set-speed', { percent: Number($('speed').value) }).catch(error => { pendingAction = ''; say(error.message, true); refresh(); });
$('time-form').onsubmit = event => {
  event.preventDefault(); queue('set-time', { localTime: $('local-time').value.length === 16 ? $('local-time').value + ':00' : $('local-time').value }).catch(error => say(error.message, true));
};
$('sign-out').onclick = async () => { try { await api('/api/logout', { method: 'POST' }); showLogin(); } catch (error) { say(error.message, true); } };
document.addEventListener('keydown', event => {
  if (event.key === 'Home' && !$('dashboard').hidden && !['INPUT', 'TEXTAREA'].includes(event.target.tagName)) {
    event.preventDefault(); const button = document.querySelector('[data-action="home"]'); if (!button.disabled) button.click();
  }
});
start().catch(error => showLogin(error.message === 'Please sign in' ? '' : error.message));
