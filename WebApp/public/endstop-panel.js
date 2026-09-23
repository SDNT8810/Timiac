const AXES = 'XYZABC';

export function renderEndstops(state, connected, get) {
  for (const [key, number] of [['a', 1], ['b', 2]]) {
    const board = state?.[key];
    const online = connected && board?.online === true;
    const fresh = online && !board.busy && Number.isFinite(board.endstopsAgeMs) &&
      board.endstopsAgeMs >= 0 && board.endstopsAgeMs < 2000;
    let readings = 0;
    for (const [index, axis] of [...AXES].entries()) {
      const name = axis + number;
      const value = fresh ? board.endstops?.[index] : null;
      const known = typeof value === 'boolean';
      const status = known ? (value ? 'Triggered' : 'Released') : 'Unknown';
      const lamp = get('endstop-' + name);
      lamp.dataset.state = known ? (value ? 'triggered' : 'released') : 'unknown';
      lamp.setAttribute('aria-label', name + ': ' + status);
      lamp.title = name + ': ' + status;
      get('endstop-state-' + name).textContent = status;
      if (known) ++readings;
    }
    get('endstops-' + key + '-status').textContent = !connected ? 'Disconnected' :
      !online ? 'Board offline' : board.busy ? 'Paused during motion' :
      readings === 6 ? 'Live' : readings ? 'Some readings unavailable' : 'Waiting for readings';
  }
}
