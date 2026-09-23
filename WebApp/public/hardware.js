export function programLines(source) {
  if (source.length > 262144) throw new Error('Program exceeds 256 KB');
  const lines = source.split(/\r?\n/).map((text, i) => ({ text: text.trim(), number: i + 1 }))
    .filter(line => line.text.replace(/\([^()]*\)/g, '').split(';')[0].trim());
  if (!lines.length || lines.length > 4096) throw new Error('Use 1-4096 executable lines');
  if (lines.some(line => line.text.length > 240)) throw new Error('A line exceeds 240 characters');
  return lines;
}
function requestTimeout(ms) {
  if (typeof AbortSignal !== 'undefined' && typeof AbortSignal.timeout === 'function')
    return AbortSignal.timeout(ms);
  const controller = new AbortController();
  setTimeout(() => controller.abort(), ms);
  return controller.signal;
}
export class HardwareController {
  constructor(base = '', fetcher = (...args) => fetch(...args), client = Math.random().toString(36).slice(2) + Date.now().toString(36)) {
    this.base = base; this.fetcher = fetcher; this.client = client; this.status = null;
    this.sequence = 0; this.events = []; this.lastEvent = 0;
  }
  ownsControl() { return this.status?.control?.owner === this.client; }
  async request(path, body) {
    const response = await this.fetcher(this.base + path, body === undefined
      ? { cache: 'no-store', signal: requestTimeout(6000) }
      : { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Timiac': '1' }, body: JSON.stringify(body), signal: requestTimeout(6000) });
    const state = await response.json();
    if (!response.ok) throw new Error(state.error || 'HTTP ' + response.status);
    if (state.session !== this.status?.session) { this.events = []; this.lastEvent = 0; this.sequence = 0; }
    // A slower status response must not replace a newer snapshot from a command.
    if (state.session === this.status?.session && state.revision < this.status.revision) return this.status;
    this.status = state;
    for (const event of state.events || []) if (event.id > this.lastEvent) { this.events.push(event); this.lastEvent = event.id; }
    this.events = this.events.slice(-80);
    return state;
  }
  refresh() { return this.request('/api/status?after=' + this.lastEvent); }
  post(path, data = {}) {
    return this.request(path, { ...data, session: this.status?.session, client: this.client, fence: this.status?.control?.fence });
  }
  claim() { return this.post('/api/claim', { label: 'Phone ' + this.client.slice(-4) }); }
  stop() { return this.request('/api/stop', {}); }
  async execute(gcode, onStatus = () => {}) {
    return this.executeRequest('/api/command', { gcode }, onStatus);
  }
  async action(action, onStatus = () => {}) {
    return this.executeRequest('/api/action', { action }, onStatus);
  }
  async executeRequest(path, payload, onStatus) {
    if (!this.ownsControl()) throw new Error('Take control first');
    const session = this.status.session, id = this.client + '-' + (++this.sequence);
    const started = Date.now();
    let state = await this.post(path, { ...payload, id, seq: this.sequence });
    const operationBudget = state.operation?.id === id && Number.isFinite(state.operation.timeoutMs)
      ? Math.max(10000, Math.min(670000, state.operation.timeoutMs + 10000)) : 310000;
    for (;;) {
      onStatus(state);
      if (state.session !== session) throw new Error('Master restarted; command will not be replayed');
      const result = state.result?.id === id ? state.result : state.program?.builtin && state.program.id === id ? state.program : state.operation;
      if (result.id !== id || result.state === 'unknown') throw new Error('Command result unavailable; check board positions');
      if (result.state === 'done') return state;
      if (result.state === 'failed' || result.state === 'faulted') throw new Error(result.error || 'Command failed');
      if (Date.now() - started > (state.program?.builtin && state.program.id === id ? 1800000 : operationBudget)) throw new Error('Completion timeout; check boards before continuing');
      await new Promise(resolve => setTimeout(resolve, 250));
      state = await this.request('/api/status?operation=' + encodeURIComponent(id) + '&after=' + this.lastEvent);
    }
  }
}
