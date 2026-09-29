// Local development only. This server never connects to physical boards.
import http from 'node:http';
import { readFile } from 'node:fs/promises';
import { fileURLToPath, pathToFileURL } from 'node:url';
import path from 'node:path';
import { nativeCommand, AXES } from './public/control.js';
const PUBLIC = fileURLToPath(new URL('./public/', import.meta.url));
const emptyBoard = () => ({ ready: true, busy: false, fault: '', positions: Array(6).fill(null), homed: Array(6).fill(false), absolute: true });
export function createDemoServer({ delay = 180 } = {}) {
  let a = emptyBoard(), b = emptyBoard(), generation = 0;
  let operation = { id: '', state: 'idle', error: '' };
  const session = `demo-${Date.now()}-${Math.random()}`, logs = ['Demo started. No hardware is connected.'];
  const log = text => { logs.push(text); if (logs.length > 24) logs.shift(); };
  const state = () => ({ mode: 'demo', session, travelMm: 50, maxFeed: 660, peerConnected: true, stopPending: false, a, b, operation, logs });
  const stop = reason => {
    generation++; operation.state = 'failed'; operation.error = reason;
    for (const board of [a, b]) { board.ready = false; board.busy = false; board.fault = reason; board.homed.fill(false); board.positions.fill(null); }
    log(`STOP: ${reason}`);
  };
  function apply(board, line, label) {
    log(`${label} > ${line}`);
    const code = line.match(/^[GM]\d+/)?.[0];
    if (code === 'G90') board.absolute = true;
    else if (code === 'G91') board.absolute = false;
    else if (code === 'G28') {
      const tail = line.slice(3).trim();
      for (let i = 0; i < 6; i++) if (!tail || tail.includes(AXES[i])) { board.positions[i] = 0; board.homed[i] = true; }
    } else if (['G0', 'G1'].includes(code)) {
      for (const [, axis, numeric] of line.slice(code.length).matchAll(/([XYZABC])\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+))/g)) {
        const i = AXES.indexOf(axis);
        if (!board.homed.every(Boolean)) throw new Error(`${label}: home all six board axes before moving`);
        const value = board.absolute ? Number(numeric) : board.positions[i] + Number(numeric);
        if (value < 0 || value > 40) throw new Error(`${label}: position outside 0–40 mm`);
        board.positions[i] = value;
      }
    } else if (['M18', 'M84'].includes(code)) board.homed.fill(false);
    else if (code === 'M119') log(`${label} < x_min: open y_min: open z_min: open a_min: open b_min: open c_min: open (demo)`);
    else if (code === 'M115') log(`${label} < FIRMWARE_NAME:Timiac Demo`);
    else if (code === 'M122') log(`${label} < Driver connections simulated OK`);
    else if (code === 'M114') log(`${label} < ${[...AXES].map((axis, i) => `${axis}:${board.positions[i] ?? '?'}`).join(' ')}`);
    else if (!['M400', 'G4', 'G21'].includes(code)) throw new Error(`Demo does not emulate ${code}`);
    log(`${label} < ok`);
  }
  const server = http.createServer(async (req, res) => {
    const send = (code, data) => { res.writeHead(code, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' }); res.end(JSON.stringify(data)); };
    try {
      const url = new URL(req.url, 'http://localhost');
      if (url.pathname.startsWith('/api/')) {
        if (req.method === 'GET' && url.pathname === '/api/status') return send(200, state());
        if (req.method !== 'POST') return send(405, { error: 'Method not allowed' });
        if (req.headers['x-timiac'] !== '1') return send(403, { error: 'Missing control header' });
        let body = '';
        for await (const chunk of req) { body += chunk; if (Buffer.byteLength(body) > 2300) return send(413, { error: 'Request too large' }); }
        let data;
        try { data = JSON.parse(body); } catch { return send(400, { error: 'Invalid JSON' }); }
        if (url.pathname === '/api/stop') { stop('Operator stop'); return send(200, state()); }
        if (url.pathname !== '/api/command') return send(404, { error: 'Unknown endpoint' });
        if (data.session !== session) return send(409, { error: 'Controller restarted; refresh status' });
        if (typeof data.id !== 'string' || !data.id || data.id.length > 64 || !['a', 'b'].every(key => Array.isArray(data[key]) && data[key].length <= 6))
          return send(400, { error: 'Expected id and command arrays' });
        try {
          for (const key of ['a', 'b']) for (const line of data[key]) {
            if (typeof line !== 'string' || !nativeCommand(line, key)) throw new Error('Invalid command');
          }
        } catch (error) { return send(400, { error: error.message }); }
        if (data.id === operation.id) return send(200, state());
        if (operation.state === 'running') return send(409, { error: 'Another command is running' });
        if (!data.a.length && !data.b.length) return send(400, { error: 'No commands' });
        if ((data.a.length && !a.ready) || (data.b.length && !b.ready)) return send(503, { error: 'Requested board is offline or stopped' });
        operation = { id: data.id, state: 'running', error: '' };
        a.busy = Boolean(data.a.length); b.busy = Boolean(data.b.length);
        const current = ++generation;
        setTimeout(() => {
          if (current !== generation) return;
          try {
            for (const key of ['a', 'b']) {
              const board = key === 'a' ? a : b;
              for (const line of data[key]) apply(board, line, key.toUpperCase());
              if (data[key].length) { apply(board, 'M400', key.toUpperCase()); apply(board, 'M114', key.toUpperCase()); }
            }
            a.busy = b.busy = false; operation.state = 'done';
          } catch (error) { stop(error.message); }
        }, delay).unref();
        return send(200, state());
      }
      if (req.method !== 'GET') return send(405, { error: 'Method not allowed' });
      const routes = new Map([
        ['/', ['index.html', 'text/html']], ['/index.html', ['index.html', 'text/html']],
        ['/app.js', ['app.js', 'text/javascript']], ['/control.js', ['control.js', 'text/javascript']],
        ['/styles.css', ['styles.css', 'text/css']],
      ]);
      const asset = routes.get(url.pathname);
      if (!asset) return send(404, { error: 'Not found' });
      res.writeHead(200, { 'Content-Type': `${asset[1]}; charset=utf-8`, 'Cache-Control': 'no-cache' });
      res.end(await readFile(path.join(PUBLIC, asset[0])));
    } catch (error) { if (!res.headersSent) send(500, { error: error.message }); else res.end(); }
  });
  return server;
}
if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  const port = Number(process.env.PORT || 4001), host = process.env.HOST || '127.0.0.1';
  createDemoServer().listen(port, host, () => console.log(`Timiac DEMO — no hardware connected\nhttp://${host}:${port}`));
}
