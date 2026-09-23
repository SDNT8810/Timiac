const $ = id => document.getElementById(id);
let config, credential = sessionStorage.getItem('timiac-google-token') || '';
const localDate = () => { const d = new Date(), p = n => String(n).padStart(2, '0'); return `${d.getFullYear()}-${p(d.getMonth()+1)}-${p(d.getDate())}T${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`; };
function say(text, error = false) { $('message').textContent = text; $('message').classList.toggle('error', error); }
async function api(path, options = {}) {
  const response = await fetch(path, { ...options, headers: { ...options.headers, Authorization: `Bearer ${credential}`, 'Content-Type': 'application/json' }, cache: 'no-store' });
  const body = await response.json(); if (!response.ok) throw new Error(body.error || `HTTP ${response.status}`); return body;
}
function stateLabel(board) { if (!board?.bridgeConnected) return 'ESP offline'; if (!board.octopusConnected) return 'Octopus offline'; if (board.fault) return 'Stopped'; if (board.busy) return 'Moving'; return board.homed === 63 ? 'Ready' : 'Home required'; }
function render(remote) {
  const state = remote.status;
  $('connection').textContent = !remote.online ? 'Master is offline' : state.fault || state.operation?.error || (state.machineReady ? 'Connected to Timiac' : 'Waiting for motor boards');
  $('clock').textContent = state.clock?.valid ? state.clock.localTime.slice(11) : '--:--:--';
  $('date').textContent = state.clock?.internetSync === 'synchronized' ? 'Internet-synchronized DS3231 time' : state.clock?.error || 'Local DS3231 time';
  $('board-a').textContent = stateLabel(state.a); $('board-b').textContent = stateLabel(state.b);
  const busy = !state.machineReady || state.stopPending || ['preparing', 'running'].includes(state.operation?.state);
  document.querySelectorAll('[data-action]').forEach(button => button.disabled = busy || (button.dataset.action !== 'home' && (state.a?.homed !== 63 || state.b?.homed !== 63)));
  $('stop').disabled = !remote.online;
}
async function refresh() { try { render(await api(`/api/devices/${config.deviceId}/status`)); } catch (error) { say(error.message, true); } }
async function queue(type, localTime = '') { await api(`/api/devices/${config.deviceId}/actions`, { method: 'POST', body: JSON.stringify({ type, localTime }) }); say(`${type} queued for the master.`); }
async function start() {
  config = await (await fetch('/api/config', { cache: 'no-store' })).json();
  if (!credential) { $('sign-in').hidden = false; return; }
  $('sign-in').hidden = true; $('dashboard').hidden = false; $('local-time').value = localDate(); await refresh(); setInterval(refresh, 3000);
}
window.handleGoogleCredential = async response => { credential = response.credential; sessionStorage.setItem('timiac-google-token', credential); try { await start(); } catch (error) { credential = ''; sessionStorage.removeItem('timiac-google-token'); $('login-message').textContent = error.message; } };
window.addEventListener('load', async () => { config = await (await fetch('/api/config')).json(); google.accounts.id.initialize({ client_id: config.googleClientId, callback: window.handleGoogleCredential }); google.accounts.id.renderButton($('google-button'), { theme: 'outline', size: 'large' }); start(); });
document.querySelectorAll('[data-action]').forEach(button => button.onclick = () => queue(button.dataset.action).catch(error => say(error.message, true)));
$('stop').onclick = () => queue('stop').catch(error => say(error.message, true));
$('time-form').onsubmit = event => { event.preventDefault(); queue('set-time', $('local-time').value.length === 16 ? $('local-time').value + ':00' : $('local-time').value).catch(error => say(error.message, true)); };
$('sign-out').onclick = () => { credential = ''; sessionStorage.removeItem('timiac-google-token'); location.reload(); };
