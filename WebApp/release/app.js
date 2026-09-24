import { HardwareController } from '/hardware.js';
import { localDate, clockActionNeedsRtc, renderClockReadouts } from '/clock-panel.js';
import { renderSystemStatus, bindWifiSettings } from '/system-panel.js';
const controller = new HardwareController();
const $ = id => document.getElementById(id);
let busy = false, connected = false, stopping = false, initializedInput = false;
let stopGeneration = 0;
function message(text, error = false) { $('message').textContent = text; $('message').classList.toggle('error', error); }
function render(state = controller.status) {
  const otherOwner = state?.control?.owner && !controller.ownsControl();
  const programBusy = ['uploading', 'running', 'paused'].includes(state?.program?.state);
  const moving = programBusy || ['preparing', 'running'].includes(state?.operation?.state);
  const blocked = busy || stopping || !connected || otherOwner || !state?.machineReady || moving || state?.stopPending;
  const homed = state?.a?.homed === 63 && state?.b?.homed === 63;
  document.querySelectorAll('[data-action]').forEach(button => {
    const needsRtc = clockActionNeedsRtc(button.dataset.action, state);
    button.disabled = Boolean(blocked || (button.dataset.action !== 'home' && !homed) || (needsRtc && !state?.clock?.valid));
  });
  $('save-time').disabled = Boolean(busy || !connected || otherOwner || !state?.clock?.present);
  for (const id of ['wifi-save', 'wifi-forget']) $(id).disabled = Boolean(busy || !connected || otherOwner);
  $('connection').textContent = connected ? 'Connected to Timiac' : 'Disconnected';
  $('machine-status').textContent = !connected ? 'Connection lost. Waiting for Timiac…' : state.fault || (otherOwner ? 'Another device has control. Please wait.' : moving ? 'Arms are moving…' : !state.machineReady ? 'Waiting for both motor boards…' : !homed ? 'Ready to home. Home all arms before moving.' : 'All 12 arms are ready.');
  if (connected && programBusy && !state.fault) $('machine-status').textContent = `${state.program.name || 'Movement'} · ${state.program.completed}/${state.program.total} steps`;
  if (connected && !moving && !otherOwner && !state.fault && homed && state.machineReady && state.proximity?.enabled && !state.proximity.armed) {
    $('machine-status').textContent = 'All 12 arms ready. Move hand away to arm the sensor.';
  }
  renderSystemStatus(state, connected, $);
  $('travel').textContent = `All arms to ${state?.travelMm ?? 48} mm`;
  const time = renderClockReadouts(state, connected, $);
  if (time && !initializedInput) { $('local-time').value = time; initializedInput = true; }
}
async function acquire() {
  await controller.refresh(); connected = true;
  if (!controller.ownsControl()) await controller.claim();
}
async function perform(work) {
  if (busy || stopping) return;
  busy = true; render(); message('');
  const generation = stopGeneration;
  try { await acquire(); if (generation === stopGeneration) await work(); }
  catch (error) { message(error.message, true); }
  finally { busy = false; render(); }
}
document.querySelectorAll('[data-action]').forEach(button => button.addEventListener('click', () => perform(async () => {
  if (stopping) return;
  message(`${button.querySelector('strong').textContent} in progress…`);
  await controller.action(button.dataset.action, render);
  message('Movement complete.');
})));
$('stop').addEventListener('click', async () => {
  ++stopGeneration;
  stopping = true; render();
  try { await controller.stop(); message('Stop requested. Wait for board status; interrupted homing requires Octopus reset.'); }
  catch (error) { message(`Stop could not be confirmed: ${error.message}`, true); }
  finally { stopping = false; render(); }
});
bindWifiSettings($, perform, (path, data) => controller.post(path, data), message);
$('phone-time').addEventListener('click', () => { $('local-time').value = localDate(); initializedInput = true; });
$('local-time').addEventListener('input', () => { initializedInput = true; });
$('time-form').addEventListener('submit', event => {
  event.preventDefault();
  let value = $('local-time').value;
  if (value.length === 16) value += ':00';
  perform(async () => { await controller.post('/api/time', { localTime: value }); message('Date and time saved to your Timiac.'); });
});
async function poll() {
  try {
    await controller.refresh(); connected = true;
    if (controller.ownsControl()) await controller.post('/api/lease');
  } catch { connected = false; }
  render(); setTimeout(poll, 1000);
}
poll();
