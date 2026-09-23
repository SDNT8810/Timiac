// Demo only: no physical board, network bridge, or serial access.
import http from 'node:http';
import { readFile } from 'node:fs/promises';
import { spawnSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';
import path from 'node:path';
const PUBLIC = fileURLToPath(new URL('./public/', import.meta.url));
const CLI = fileURLToPath(new URL('../.build/tests/gcode_cli' + (process.platform === 'win32' ? '.exe' : ''), import.meta.url));
const freshBoard = () => ({ online: true, ready: true, busy: false, homed: 0, fault: '', positions: [0, null, null, null, null, null] });
export function createThreeEspDemo({ delay = 20 } = {}) {
  const session = 'demo-' + Math.random().toString(36).slice(2);
  let a = freshBoard(), b = freshBoard(), revision = 0, generation = 0, eventId = 0, fault = '';
  let control = { owner: '', label: '', fence: 0 }, renewed = 0, lastSeq = 0;
  let operation = { id: '', state: 'idle', error: '' };
  let program = { id: '', state: 'empty', total: 0, uploaded: 0, completed: 0, error: '', translation: '' };
  let source = [], steps = [], manual = [];
  const events = [], receipts = new Map();
  const log = text => { events.push({ id: ++eventId, text }); if (events.length > 32) events.shift(); };
  const active = () => ['preparing', 'running'].includes(operation.state);
  const playing = () => ['running', 'paused'].includes(program.state);
  const ready = () => a.ready && b.ready && !fault;
  const snapshot = () => {
    if (Date.now() - renewed > 15000) control.owner = '';
    return { mode: 'demo', session, revision: ++revision, axesPerBoard: 1, travelMm: 48, maxFeed: 369.6,
      a, b, machineReady: ready(), fault, stopPending: false, operation, control, program, events };
  };
  function compile(lines) {
    const result = spawnSync(CLI, [], { input: lines.join('\n') + '\n', encoding: 'utf8', windowsHide: true, maxBuffer: 2 * 1024 * 1024 });
    if (result.error) throw Error('Build the demo parser: python tests/run_firmware_tests.py');
    const parsed = result.stdout.trim().split('\n').filter(Boolean).map(line => JSON.parse(line));
    const error = parsed.find(step => step.error);
    if (error || result.status) throw Error(error?.error || 'Local parser failed');
    return parsed;
  }
  function stop(reason) {
    ++generation; fault = reason; operation.state = 'failed'; operation.error = reason;
    if (program.state !== 'empty') { program.state = 'faulted'; program.error = reason; }
    for (const target of [a, b]) { target.ready = false; target.busy = false; target.homed = 0; target.fault = reason; }
    log(reason);
  }
  function runStep(step, id, complete = () => {}) {
    if (!ready()) throw Error('Both boards must be ready');
    for (const [key, target] of [['a', a], ['b', b]])
      if (step[key].some(line => line.startsWith('G1 ')) && !target.homed) throw Error('Home the moving X axis first');
    operation = { id, state: 'running', error: '' }; a.busy = b.busy = true;
    const current = generation;
    setTimeout(() => {
      if (current !== generation) return;
      for (const [key, target] of [['a', a], ['b', b]]) for (const line of step[key]) {
        log(key.toUpperCase() + ' > ' + line);
        if (line === 'G28 X') { target.homed = 1; target.positions[0] = 0; }
        if (line.startsWith('G1 ')) target.positions[0] = Number(line.match(/X([\d.]+)/)[1]);
        if (['M18', 'M84'].includes(line)) target.homed = 0;
        if (line === 'M119') log('x_min: open (simulated)');
      }
      a.busy = b.busy = false; operation.state = 'done';
      if (receipts.has(id)) receipts.get(id).result = { ...operation };
      complete();
    }, delay).unref();
  }
  function pump() {
    if (program.state !== 'running' || active()) return;
    if (program.completed === program.total) { program.state = 'done'; return; }
    try { runStep(steps[program.completed], program.id + '-' + program.completed, () => { ++program.completed; pump(); }); }
    catch (error) { program.state = 'faulted'; program.error = error.message; }
  }
  const server = http.createServer(async (req, res) => {
    const send = (code, data) => { res.writeHead(code, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' }); res.end(JSON.stringify(data)); };
    try {
      const url = new URL(req.url, 'http://localhost');
      if (req.method === 'GET' && url.pathname === '/api/status') {
        const state = snapshot(), id = url.searchParams.get('operation');
        if (id) state.result = receipts.get(id)?.result || (operation.id === id ? operation : { id, state: 'unknown' });
        return send(200, state);
      }
      if (url.pathname.startsWith('/api/')) {
        if (req.method !== 'POST' || req.headers['x-timiac'] !== '1') return send(403, { error: 'Missing control header' });
        let body = ''; for await (const chunk of req) { body += chunk; if (body.length > 2300) return send(413, { error: 'Request too large' }); }
        const data = JSON.parse(body), route = url.pathname;
        if (route === '/api/stop') { stop('Operator stop'); return send(200, snapshot()); }
        if (data.session !== session) return send(409, { error: 'Master restarted; refresh status' });
        snapshot();
        if (route === '/api/claim') {
          if (control.owner && control.owner !== data.client) return send(423, { error: 'Another phone has control' });
          if (!control.owner) { control = { owner: data.client, label: data.label || 'Phone', fence: control.fence + 1 }; lastSeq = 0; manual = []; }
          renewed = Date.now(); return send(200, snapshot());
        }
        if (!control.owner || data.client !== control.owner || data.fence !== control.fence) return send(423, { error: 'Take control first' });
        renewed = Date.now();
        if (route === '/api/lease') { /* renewed */ }
        else if (route === '/api/release') control.owner = '';
        else if (route === '/api/command') {
          const receipt = receipts.get(data.id);
          if (receipt) {
            if (receipt.raw !== data.gcode) return send(409, { error: 'Conflicting command ID' });
            return send(200, snapshot());
          }
          if (active() || playing() || program.state === 'uploading') throw Error('Machine busy');
          if (!Number.isInteger(data.seq) || data.seq <= lastSeq) throw Error('Stale sequence');
          const step = compile([...manual, data.gcode]).at(-1);
          runStep(step, data.id); lastSeq = data.seq; manual.push(data.gcode); receipts.set(data.id, { raw: data.gcode });
        } else if (route === '/api/program/begin') {
          if (active() || playing()) throw Error('Machine busy');
          if (!Number.isInteger(data.total) || data.total < 1 || data.total > 4096) throw Error('Invalid program length');
          program = { id: 'program-' + (++generation), state: 'uploading', total: data.total, uploaded: 0, completed: 0, error: '', translation: '' };
          source = []; steps = [];
        } else if (route.startsWith('/api/program/')) {
          if (data.program !== program.id) throw Error('Program changed');
          if (route.endsWith('/append')) {
            if (program.state !== 'uploading' || data.index !== program.uploaded) throw Error('Upload out of sequence');
            const parsed = compile([...source, data.gcode]), step = parsed.at(-1);
            if (!step.a.length && !step.b.length) throw Error('Empty program line');
            source.push(data.gcode); steps = parsed; ++program.uploaded; program.translation = JSON.stringify(step);
            if (program.uploaded === program.total) program.state = 'ready';
          } else if (route.endsWith('/start')) {
            if (program.state !== 'ready') throw Error('Upload the whole program first');
            program.state = 'running'; pump();
          } else if (route.endsWith('/pause')) {
            if (!playing()) throw Error('No running program');
            program.state = 'paused';
          } else if (route.endsWith('/resume')) {
            if (program.state !== 'paused') throw Error('Program is not paused');
            program.state = 'running'; pump();
          } else return send(404, { error: 'Unknown route' });
        } else return send(404, { error: 'Unknown route' });
        return send(200, snapshot());
      }
      const assets = new Map([['/', 'index.html'], ...['index.html', 'app.js', 'hardware.js', 'clock-panel.js', 'system-panel.js', 'endstop-panel.js', 'styles.css'].map(name => ['/' + name, name])]);
      const asset = assets.get(url.pathname);
      if (req.method !== 'GET' || !asset) return send(404, { error: 'Not found' });
      res.writeHead(200, { 'Content-Type': (asset.endsWith('.js') ? 'text/javascript' : asset.endsWith('.css') ? 'text/css' : 'text/html') + '; charset=utf-8' });
      res.end(await readFile(path.join(PUBLIC, asset)));
    } catch (error) { if (!res.headersSent) send(400, { error: error.message }); else res.end(); }
  });
  server.fault = stop;
  log('Three-ESP demo: simulated motors, actual master G-code parser.');
  return server;
}
if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href)
  createThreeEspDemo().listen(Number(process.env.PORT || 4001), '127.0.0.1', () => console.log('Timiac demo: http://127.0.0.1:4001'));
