export function boardStatus(state, key, connected) {
  const board = state?.[key];
  if (!connected) return { label: 'Unknown', detail: 'Master disconnected' };
  if (board?.fault?.includes('firmware mismatch')) return { label: 'Firmware mismatch', detail: board.fault };
  if (!(board?.bridgeConnected ?? board?.online ?? board?.ready)) return { label: 'ESP offline', detail:
    board?.lastSeenMs === null ? 'No bridge has joined with this board number; check its power and ESP1/ESP2 firmware role' :
    Number.isFinite(board?.lastSeenMs) ? `Last bridge report ${Math.floor(board.lastSeenMs / 1000)} s ago; reconnecting` : 'Reconnecting to master...' };
  if (board.resetRequired) return { label: 'Reset required', detail: board.fault || 'Marlin halted; check the fault, then reset this Octopus' };
  if (!(board.octopusConnected ?? board.online ?? board.ready)) return { label: 'Octopus offline', detail: board.fault || 'ESP connected - waiting for motor board' };
  if (board.drivers && board.drivers.ok === false && board.octopusConnected) return { label: 'Driver check', detail: board.fault || board.diagnostic || 'Checking six TMC2209 drivers before motion' };
  if (board.fault) return { label: 'Stopped', detail: board.fault };
  if (board.busy) return { label: state.operation?.homing ? 'Homing' : 'Moving', detail: 'ESP and Octopus connected' };
  if (!board.ready) return { label: 'Stopped', detail: 'Check the Octopus connection, then Home' };
  return { label: board.homed === 63 ? 'Ready' : 'Home required', detail: 'ESP and Octopus connected' };
}

export function renderSystemStatus(state, connected, element) {
  for (const key of ['a', 'b']) {
    const status = boardStatus(state, key, connected);
    element('board-status-' + key).textContent = status.label;
    element('board-detail-' + key).textContent = status.detail;
    element('board-' + key).classList.toggle('ready', connected && Boolean(state?.[key]?.ready));
    element('board-' + key).classList.toggle('fault', connected && Boolean(state?.[key]?.fault));
  }
  const wifi = state?.wifi;
  element('wifi-status').textContent = !connected ? 'Master disconnected' : wifi?.pending ? 'Saved - waiting for idle to connect' :
    wifi?.connecting ? `Connecting to ${wifi.ssid}...` : wifi?.connected ? `Connected to ${wifi.ssid} - ${wifi.ip}` :
    wifi?.error || (wifi?.configured ? `Retrying ${wifi.ssid} while idle` : 'No router configured - Timiac Wi-Fi is available');
}

export function bindWifiSettings(element, perform, post, message) {
  element('wifi-form').onsubmit = event => {
    event.preventDefault();
    const ssid = element('wifi-ssid').value, password = element('wifi-password').value;
    return perform(async () => {
      await post('/api/wifi', { ssid, password });
      element('wifi-password').value = '';
      message('Wi-Fi saved. Connection starts while idle; Timiac stays available.');
    });
  };
  element('wifi-forget').onclick = () => perform(async () => {
    await post('/api/wifi', { forget: true });
    element('wifi-password').value = ''; element('wifi-ssid').value = '';
    message('Router settings removed. Timiac Wi-Fi remains available.');
  });
}
