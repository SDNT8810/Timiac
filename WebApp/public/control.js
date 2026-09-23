// Public local letters reach Marlin unchanged. Only the board suffix is removed.
export const AXES = 'XYZABC';
// Simulator arm names, clockwise from arm 1. F remains the standard feed word.
export const SIM_AXES = 'ABCDEHIJKLUV';
const number = value => Number(value.toFixed(3));
export function armAddress(arm) {
  if (!Number.isInteger(arm) || arm < 1 || arm > 12) throw new Error('Select arm 1–12');
  return { board: arm <= 6 ? 'a' : 'b', axis: AXES[(arm - 1) % 6] };
}
export function moveArm(arm, position, feed, travel = 48, maxFeed = 700) {
  if (!Number.isFinite(position) || position < 0 || position > travel) throw new Error(`Position must be 0–${travel} mm`);
  if (!Number.isFinite(feed) || feed <= 0 || feed > maxFeed) throw new Error(`Feed must be above 0 and at most ${maxFeed} mm/min`);
  const { board, axis } = armAddress(arm);
  const result = { a: [], b: [] };
  result[board] = ['G21', 'G90', `G1 ${axis}${number(position)} F${number(feed)}`];
  return result;
}
export function homeArm(arm) {
  const { board, axis } = armAddress(arm);
  return { a: [], b: [], [board]: [`G28 ${axis}`] };
}
export function cleanLine(raw) {
  const line = raw.replace(/\([^()]*\)/g, '').split(';')[0].trim().toUpperCase();
  if (line.includes('(') || line.includes(')')) throw new Error('Unbalanced or nested comment');
  return line;
}
export function nativeCommand(raw, target) {
  if (!['a', 'b', 'both'].includes(target)) throw new Error('Choose a board');
  const line = cleanLine(raw);
  if (!line) return null;
  if (!/^[GM]\d/.test(line) || /[^\x20-\x7e]|\*/.test(line) || line.length > 160)
    throw new Error('Use one G/M command, at most 160 characters, without checksums');
  if (/^M0*(112|999|110)(?!\d)/.test(line)) throw new Error('Use Stop for emergency shutdown commands');
  return { a: target !== 'b' ? [line] : [], b: target !== 'a' ? [line] : [] };
}

// Simulator arm names are never transmitted directly to Marlin.
export function compileProgram(source, dialect, travel = 48, maxFeed = 700) {
  if (source.length > 262144) throw new Error('Program exceeds 256 KB');
  if (!Number.isFinite(travel) || travel <= 0 || !Number.isFinite(maxFeed) || maxFeed <= 0)
    throw new Error('Invalid machine limits');
  let absolute = true, feed = 600;
  const positions = Array(12).fill(null), output = [];
  const scale = travel / 100;
  for (const [lineIndex, raw] of source.split(/\r?\n/).entries()) {
    try {
      const line = cleanLine(raw);
      if (!line) continue;
      if (dialect !== 'simulator') {
        output.push({ ...nativeCommand(line, dialect), line: lineIndex + 1, raw });
        continue;
      }
      const match = line.match(/^G\s*(\d+)\s*/);
      if (!match) {
        if (line === 'M400') output.push({ a: ['M400'], b: ['M400'], line: lineIndex + 1, raw });
        else throw new Error('Simulator mode supports G0/G1, G4, G28, G90/G91 and M400');
        continue;
      }
      const command = Number(match[1]), tail = line.slice(match[0].length);
      const result = { a: [], b: [], line: lineIndex + 1, raw };
      if ([90, 91].includes(command)) {
        if (tail.trim()) throw new Error('Unexpected positioning-mode parameters');
        absolute = command === 90; continue;
      }
      if (command === 28) {
        if (tail.trim()) throw new Error('Simulator G28 homes all twelve arms; use Manual for one arm');
        // Native home is 0 mm, equivalent to 100% simulator extension.
        positions.fill(100); result.a = ['G28']; result.b = ['G28']; output.push(result); continue;
      }
      const matches = [...tail.matchAll(/([A-Z])\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+))/g)];
      if (tail.replace(/([A-Z])\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+))/g, '').trim())
        throw new Error('Invalid parameter');
      const entries = matches.map(m => [m[1], Number(m[2])]);
      if (entries.some(([, v]) => !Number.isFinite(v))) throw new Error('Non-finite number');
      if (command === 4) {
        if (entries.length !== 1 || !['P', 'S'].includes(entries[0][0])) throw new Error('Use G4 Pmilliseconds or G4 Sseconds');
        const ms = entries[0][1] * (entries[0][0] === 'S' ? 1000 : 1);
        if (ms < 0 || ms > 120000) throw new Error('Dwell must be 0–120 seconds');
        result.a = result.b = [`G4 P${number(ms)}`]; output.push(result); continue;
      }
      if (![0, 1].includes(command)) throw new Error(`Unsupported G${command}`);
      const fValues = entries.filter(([key]) => key === 'F').map(([, v]) => v);
      if (fValues.length > 1) throw new Error('F is feed rate and may appear only once');
      const params = new Map();
      for (const [key, value] of entries.filter(([key]) => key !== 'F')) {
        if (!SIM_AXES.includes(key) || params.has(key)) throw new Error(`Invalid or repeated arm ${key}`);
        params.set(key, value);
      }
      if (fValues.length === 1) feed = fValues[0];
      if (feed <= 0) throw new Error('Feed must be positive');
      const words = { a: [], b: [] };
      for (const [key, value] of params) {
        const index = SIM_AXES.indexOf(key);
        if (!absolute && positions[index] === null) throw new Error(`Relative ${key} needs G28 or a previous absolute position`);
        const target = absolute ? value : positions[index] + value;
        if (target < 0 || target > 100) throw new Error(`Arm ${key} is outside simulator range 0–100`);
        positions[index] = target;
        const address = armAddress(index + 1);
        words[address.board].push(`${address.axis}${number((100 - target) * scale)}`);
      }
      for (const board of ['a', 'b']) if (words[board].length) {
        result[board] = ['G21', 'G90', `G1 ${words[board].join(' ')} F${number(Math.min(maxFeed, feed * scale))}`];
      }
      if (result.a.length || result.b.length) output.push(result);
    } catch (error) { throw new Error(`Line ${lineIndex + 1}: ${error.message}`); }
  }
  if (!output.length) throw new Error('No executable commands');
  return output;
}

export class Controller {
  constructor(base = '', fetcher = fetch) { this.base = base; this.fetcher = fetcher; this.status = null; }
  async request(path, body) {
    const response = await this.fetcher(this.base + path, body ? {
      method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Timiac': '1' },
      body: JSON.stringify(body), signal: AbortSignal.timeout(6000),
    } : { cache: 'no-store', signal: AbortSignal.timeout(6000) });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || `HTTP ${response.status}`);
    this.status = data; return data;
  }
  refresh() { return this.request('/api/status'); }
  stop() { return this.request('/api/stop', {}); }
  async execute(command, onStatus = () => {}) {
    const session = this.status?.session;
    if (!session) throw new Error('Connect to the controller first');
    const id = `${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}`;
    let state = await this.request('/api/command', { a: command.a, b: command.b, id, session });
    const started = Date.now();
    while (true) {
      onStatus(state);
      if (state.session !== session) throw new Error('Controller restarted; program stopped');
      if (state.operation.id !== id) throw new Error('Another control session replaced this command');
      if (state.operation.state === 'done') return state;
      if (state.operation.state === 'failed') throw new Error(state.operation.error || 'Command failed');
      if (Date.now() - started > 310000) throw new Error('Completion timed out; check both boards before continuing');
      await new Promise(resolve => setTimeout(resolve, 250));
      state = await this.refresh();
    }
  }
}
