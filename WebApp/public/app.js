import { HardwareController, programLines } from './hardware.js';
import { localDate, clockActionNeedsRtc, renderClockReadouts } from './clock-panel.js';
import { renderSystemStatus, bindWifiSettings } from './system-panel.js';
import { renderEndstops } from './endstop-panel.js';
const $ = selector => document.querySelector(selector);
const api = new HardwareController();
const INSTALLED_AXES = ['X1', 'Y1', 'Z1', 'A1', 'B1', 'C1', 'X2', 'Y2', 'Z2', 'A2', 'B2', 'C2'];
const LOCAL_AXES = 'XYZABC';
let selected = 'X1', busy = false, connected = false, lastRenew = 0, initializedInput = false;
let programName = 'Twelve-motor program', uploadSource = '', uploadId = '';
const message = (text, error = false) => { $('#message').textContent = text; $('#message').classList.toggle('error', error); };
function locks() {
  const active = ['preparing', 'running'].includes(api.status?.operation?.state);
  const playing = ['running', 'paused'].includes(api.status?.program?.state);
  document.querySelectorAll('[data-control]').forEach(el => el.disabled = !connected || !api.ownsControl() || busy);
  for (const id of ['move', 'home', 'home-all', 'release', 'upload'])
    $('#' + id).disabled ||= active || playing;
  $('#run').disabled ||= api.status?.program?.state !== 'ready' || !api.status?.machineReady || uploadSource !== $('#program').value || uploadId !== api.status?.program?.id;
  $('#pause').disabled ||= !playing;
  $('#pause').textContent = api.status?.program?.state === 'paused' ? 'Resume' : 'Pause';
  $('#claim').disabled = !connected || busy || api.ownsControl();
  $('#relinquish').disabled = !connected || !api.ownsControl() || busy;
  const state = api.status;
  const homed = state?.a?.homed === 63 && state?.b?.homed === 63;
  document.querySelectorAll('[data-clock-action]').forEach(button => {
    button.disabled ||= active || ['uploading', 'running', 'paused'].includes(state?.program?.state) || !state?.machineReady || state?.stopPending ||
      (button.dataset.clockAction !== 'home' && !homed) || (clockActionNeedsRtc(button.dataset.clockAction, state) && !state?.clock?.valid);
  });
  $('#save-time').disabled ||= !state?.clock?.present;
}
function render(state) {
  connected = true;
  $('#demo-banner').hidden = state.mode !== 'demo';
  $('#connection').textContent = state.mode === 'demo' ? 'DEMO' : 'MASTER CONNECTED';
  $('#connection').classList.add('online');
  $('#owner').textContent = api.ownsControl() ? 'You have control' : state.control?.owner ? 'Control: ' + state.control.label : 'No phone has control';
  renderSystemStatus(state, connected, id => $('#' + id));
  renderEndstops(state, connected, id => $('#' + id));
  const b = selected[1] === '1' ? 'a' : 'b', value = state[b]?.positions?.[LOCAL_AXES.indexOf(selected[0])];
  $('#reported').textContent = value == null ? '— mm' : value.toFixed(2) + ' mm';
  $('#target').max = state.travelMm; $('#feed').max = state.maxFeed;
  $('#logs').textContent = api.events.map(e => e.text).join('\n'); $('#logs').scrollTop = $('#logs').scrollHeight;
  const job = state.program;
  $('#progress').max = job.total || 1; $('#progress').value = job.completed;
  $('#program-state').textContent = job.state + ' · ' + job.completed + '/' + job.total + (job.error ? ' · ' + job.error : '');
  if (state.fault) message(state.fault + (state.stopPending ? ' · Stop delivery pending' : ''), true);
  const time = renderClockReadouts(state, connected, id => $('#' + id));
  if (time && !initializedInput) { $('#local-time').value = time; initializedInput = true; }
  locks();
}
async function action(fn) {
  if (busy) return;
  busy = true; locks();
  try { await fn(); } catch (error) { message(error.message, true); }
  finally { busy = false; if (api.status) render(api.status); else locks(); }
}
async function execute(gcode) {
  message('Waiting for both boards to complete…');
  await api.execute(gcode, render); message('Command completed.');
}
document.querySelectorAll('[data-axis]').forEach(button => button.onclick = () => {
  selected = button.dataset.axis;
  document.querySelectorAll('[data-axis]').forEach(b => b.classList.toggle('selected', b === button));
  $('#selected-title').textContent = selected; $('#selected-route').textContent = 'Octopus ' + selected[1] + ' · local ' + selected[0];
  if (api.status) render(api.status);
});
$('#claim').onclick = () => action(async () => { render(await api.claim()); message('Control acquired. Verify sensors before homing.'); });
$('#relinquish').onclick = () => action(() => api.post('/api/release'));
$('#move').onclick = () => action(() => {
  const position = Number($('#target').value), feed = Number($('#feed').value);
  if (!$('#target').value || !$('#feed').value || !Number.isFinite(position) || !Number.isFinite(feed)) throw new Error('Enter a target and speed');
  return execute('G90 ' + selected + ' ' + position + ' F' + feed);
});
$('#home').onclick = () => action(() => execute('G28 ' + selected));
$('#home-all').onclick = () => action(() => execute('G28 ' + INSTALLED_AXES.join(' ')));
$('#release').onclick = () => action(() => execute('M18'));
$('#terminal').onsubmit = event => { event.preventDefault(); action(() => execute($('#command').value)); };
document.querySelectorAll('[data-command]').forEach(button => button.onclick = () => action(() => execute(button.dataset.command)));
document.querySelectorAll('[data-clock-action]').forEach(button => button.onclick = () => action(async () => {
  message(button.textContent + ' in progress…');
  await api.action(button.dataset.clockAction, render); message('Movement complete.');
}));
$('#phone-time').onclick = () => { $('#local-time').value = localDate(); initializedInput = true; };
$('#local-time').oninput = () => { initializedInput = true; };
$('#time-form').onsubmit = event => {
  event.preventDefault();
  let value = $('#local-time').value;
  if (value.length === 16) value += ':00';
  action(async () => { render(await api.post('/api/time', { localTime: value })); message('Date and time saved to your Timiac.'); });
};
$('#stop').onclick = async () => {
  try { render(await api.stop()); message('Motion stopped. Wait for both boards, then Home before moving again.', true); }
  catch (error) { message('Stop delivery unconfirmed. ' + error.message, true); }
};
bindWifiSettings(id => $('#' + id), action, async (path, data) => render(await api.post(path, data)), message);
$('#example').onclick = () => {
  $('#program').value = '; Run only after all 12 axes are individually commissioned\nG28 ' + INSTALLED_AXES.join(' ') + '\nG90 ' + INSTALLED_AXES.map(axis => axis + ' 1').join(' ') + ' F180\nG4 P500\nG90 ' + INSTALLED_AXES.map(axis => axis + ' 0').join(' ') + ' F180';
  programName = 'Twelve-motor 1 mm test'; locks();
};
$('#program').oninput = locks;
$('#file').onchange = () => action(async () => {
  const file = $('#file').files[0]; if (!file) return;
  if (file.size > 262144) throw new Error('File exceeds 256 KB');
  $('#program').value = await file.text(); programName = file.name;
  message('File loaded. Check & upload before running.');
});
$('#upload').onclick = () => action(async () => {
  const source = $('#program').value, lines = programLines(source);
  uploadSource = ''; uploadId = ''; $('#preview-code').textContent = '';
  let state = await api.post('/api/program/begin', { total: lines.length, name: programName });
  const id = state.program.id;
  for (const [index, line] of lines.entries()) {
    state = await api.post('/api/program/append', { program: id, index, gcode: line.text });
    if (index < 100) $('#preview-code').textContent += 'Line ' + line.number + ': ' + line.text + '\n' + state.program.translation + '\n';
    message('Checked and uploaded ' + (index + 1) + '/' + lines.length);
  }
  uploadSource = source; uploadId = id;
  if (lines.length > 100) $('#preview-code').textContent += 'Preview shows the first 100 steps.\n';
  message('Entire program checked and stored on master. Review before pressing Run.');
});
$('#run').onclick = () => action(async () => {
  if (uploadSource !== $('#program').value || uploadId !== api.status.program.id) throw new Error('Check & upload the current editor contents first');
  render(await api.post('/api/program/start', { program: uploadId })); message('Program running on master.');
});
$('#pause').onclick = () => action(() => api.post('/api/program/' + (api.status.program.state === 'paused' ? 'resume' : 'pause'), { program: api.status.program.id }));
async function poll() {
  try {
    render(await api.refresh());
    if (api.ownsControl() && Date.now() - lastRenew > 4000) { lastRenew = Date.now(); render(await api.post('/api/lease')); }
  } catch (error) {
    connected = false; $('#connection').textContent = 'DISCONNECTED'; $('#connection').classList.remove('online');
    renderClockReadouts(api.status, false, id => $('#' + id));
    renderSystemStatus(api.status, false, id => $('#' + id));
    renderEndstops(api.status, false, id => $('#' + id));
    message('Controller status request failed: ' + (error?.message || String(error)), true); locks();
  }
  setTimeout(poll, 500);
}
locks(); poll();
