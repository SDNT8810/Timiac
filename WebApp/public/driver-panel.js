import { machine } from './machine-generated.js';
const { axes, sockets } = machine;
function unavailableReason(board, connected) {
  if (!connected) return 'Master disconnected';
  if (board?.bridgeConnected === false) return 'ESP offline; driver cannot be checked';
  if (!board?.octopusConnected) return 'Octopus offline; driver cannot be checked';
  if (board.busy) return 'Checks paused during motion';
  const age = board.drivers?.ageMs;
  if (age === undefined) return 'Update the ESP bridge for live driver checks';
  if (age === null) return 'Waiting for a complete driver report';
  if (!Number.isFinite(age) || age < 0 || age >= 15000) return 'Reading expired; waiting for a fresh check';
  return '';
}
export function driverRows(board, connected) {
  const available = !unavailableReason(board, connected);
  const checks = board?.drivers;
  return [...axes].map((axis, i) => {
    const bit = 1 << i;
    const tested = available && checks && (checks.tested & bit);
    // Marlin's Set current is cached and a failed read can look like a valid
    // mode bit. Neither is evidence of a working, configured driver without UART.
    const settings = tested && (checks.connected & bit) && checks.columns;
    return { axis, socket: sockets[i],
      uart: !tested ? 'Unknown' : checks.connected & bit ? 'OK' : 'No response',
      current: !settings || !(checks.settings & 1) ? 'Unknown' : checks.current & bit ? `${machine.currentMa} mA` : 'Mismatch',
      microsteps: !settings || !(checks.settings & 2) ? 'Unknown' : checks.microsteps & bit ? String(machine.microsteps) : 'Mismatch',
      mode: !settings || !(checks.settings & 4) ? 'Unknown' : checks.mode & bit ? 'SpreadCycle' : 'Mismatch',
    };
  });
}
export function driverIndicators(board, connected) {
  const unavailable = unavailableReason(board, connected);
  return driverRows(board, connected).map(row => {
    if (unavailable) return { ...row, state: 'unknown', result: 'Not checked', detail: unavailable };
    if (row.uart === 'No response') return { ...row, state: 'fault', result: 'No response', detail: 'Suspect driver, socket, UART jumper or power' };
    if (row.uart === 'Unknown') return { ...row, state: 'unknown', result: 'Not checked', detail: 'No connection result for this driver' };
    if ([row.current, row.microsteps, row.mode].includes('Mismatch'))
      return { ...row, state: 'warning', result: 'Settings mismatch', detail: 'Driver responds; configuration needs checking' };
    if ([row.current, row.microsteps, row.mode].includes('Unknown'))
      return { ...row, state: 'unknown', result: 'Check incomplete', detail: 'Driver responds; some settings are missing' };
    return { ...row, state: 'ok', result: 'Checks passed', detail: 'UART and settings OK; motor output untested' };
  });
}
export function renderDrivers(state, connected, element) {
  element('firmware-build').textContent = connected ? [state?.firmware || 'Older master firmware', state?.software].filter(Boolean).join(' / ') : 'Master disconnected';
  for (const [key, boardNumber] of [['a', 1], ['b', 2]]) {
    const board = state?.[key], rows = driverIndicators(board, connected);
    const count = value => rows.filter(row => row.state === value).length;
    const age = board?.drivers?.ageMs;
    element(`driver-summary-${boardNumber}`).textContent = unavailableReason(board, connected) ||
      `${count('ok')} passed · ${count('fault')} no response · ${count('warning')} settings mismatch · ${count('unknown')} incomplete — checked ${Math.floor(age / 1000)} s ago`;
    for (const row of rows) {
      element(`driver-${row.axis}${boardNumber}-socket`).textContent = `MOTOR${row.socket}`;
      element(`driver-${row.axis}${boardNumber}-socket-table`).textContent = `MOTOR${row.socket}`;
      const indicator = element(`driver-${row.axis}${boardNumber}-status`);
      indicator.dataset.state = row.state;
      indicator.setAttribute('aria-label', `${row.axis}${boardNumber}, MOTOR${row.socket}: ${row.result}. ${row.detail}`);
      element(`driver-${row.axis}${boardNumber}-result`).textContent = row.result;
      element(`driver-${row.axis}${boardNumber}-detail`).textContent = row.detail;
      for (const field of ['uart', 'current', 'microsteps', 'mode']) {
        const cell = element(`driver-${row.axis}${boardNumber}-${field}`);
        cell.textContent = row[field];
        cell.dataset.state = row[field] === 'Mismatch' || row[field] === 'No response' ? 'fault' : row[field] === 'Unknown' ? 'unknown' : 'ok';
      }
    }
  }
}
