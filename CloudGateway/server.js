import crypto from 'node:crypto';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import express from 'express';
import { Firestore, FieldValue } from '@google-cloud/firestore';
import { OAuth2Client } from 'google-auth-library';

const required = name => {
  const value = process.env[name];
  if (!value) throw new Error(`Missing required environment variable ${name}`);
  return value;
};
const configuration = {
  googleClientId: required('GOOGLE_OAUTH_CLIENT_ID'),
  deviceId: required('TIMIAC_DEVICE_ID'),
  deviceKey: required('TIMIAC_DEVICE_KEY'),
  administrators: new Set(required('TIMIAC_ADMIN_EMAILS').split(',').map(email => email.trim().toLowerCase()).filter(Boolean)),
};
if (!configuration.administrators.size) throw new Error('TIMIAC_ADMIN_EMAILS must contain an email address');

const db = new Firestore();
const google = new OAuth2Client(configuration.googleClientId);
const app = express();
app.disable('x-powered-by');
app.use(express.json({ limit: '48kb' }));

function sameSecret(candidate, expected) {
  const left = Buffer.from(candidate || ''), right = Buffer.from(expected);
  return left.length === right.length && crypto.timingSafeEqual(left, right);
}
function deviceId(req) {
  const id = req.params.deviceId;
  if (id !== configuration.deviceId) throw Object.assign(new Error('Unknown device'), { status: 404 });
  if (!sameSecret(req.get('X-Timiac-Device-Key'), configuration.deviceKey)) throw Object.assign(new Error('Device authentication failed'), { status: 401 });
  return id;
}
async function administrator(req, res, next) {
  try {
    const token = req.get('Authorization')?.match(/^Bearer (.+)$/i)?.[1];
    if (!token) throw Object.assign(new Error('Sign in with an approved Google account'), { status: 401 });
    const verified = await google.verifyIdToken({ idToken: token, audience: configuration.googleClientId });
    const claims = verified.getPayload();
    const email = claims?.email?.toLowerCase();
    if (!claims?.email_verified || !configuration.administrators.has(email)) {
      throw Object.assign(new Error('This Google account is not an approved Timiac administrator'), { status: 403 });
    }
    req.administrator = email;
    next();
  } catch (error) { next(error); }
}
function deviceRef(id) { return db.collection('timiacDevices').doc(id); }
function cleanStatus(status) {
  if (!status || typeof status !== 'object' || Array.isArray(status)) throw Object.assign(new Error('Invalid device status'), { status: 400 });
  return status;
}
async function nextAction(id, ready) {
  const queue = deviceRef(id).collection('actions');
  return db.runTransaction(async transaction => {
    // Keep this as one ordered read so a deployment needs no composite Firestore
    // index. An emergency stop jumps ahead of ordinary queued actions.
    const snapshot = await transaction.get(queue.orderBy('createdAt').limit(20));
    const queued = snapshot.docs.filter(action => action.data().state === 'queued');
    const action = queued.find(item => item.data().type === 'stop') || (ready ? queued[0] : null);
    if (!action) return null;
    transaction.update(action.ref, { state: 'delivered', deliveredAt: FieldValue.serverTimestamp() });
    const value = action.data();
    return { id: action.id, type: value.type, localTime: value.localTime || '' };
  });
}

app.get('/healthz', (_req, res) => res.json({ ok: true }));
app.get('/api/config', (_req, res) => res.json({ googleClientId: configuration.googleClientId, deviceId: configuration.deviceId }));
app.get('/api/devices/:deviceId/status', administrator, async (req, res) => {
  if (req.params.deviceId !== configuration.deviceId) return res.status(404).json({ error: 'Unknown device' });
  const snapshot = await deviceRef(configuration.deviceId).get();
  const data = snapshot.exists ? snapshot.data() : {};
  res.set('Cache-Control', 'no-store').json({ online: Boolean(data.status), updatedAt: data.updatedAt?.toDate?.().toISOString() || '', status: data.status || null });
});
app.post('/api/devices/:deviceId/actions', administrator, async (req, res) => {
  if (req.params.deviceId !== configuration.deviceId) return res.status(404).json({ error: 'Unknown device' });
  const type = String(req.body?.type || '');
  const localTime = String(req.body?.localTime || '');
  if (!['home', 'open', 'close', 'show-time', 'stop', 'set-time'].includes(type)) return res.status(400).json({ error: 'Unsupported action' });
  if (type === 'set-time' && !/^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d$/.test(localTime)) return res.status(400).json({ error: 'Invalid local time' });
  const queued = await deviceRef(configuration.deviceId).collection('actions').add({
    type, localTime, requestedBy: req.administrator, state: 'queued', createdAt: FieldValue.serverTimestamp(),
  });
  res.status(202).set('Cache-Control', 'no-store').json({ id: queued.id, state: 'queued' });
});
app.post('/device/v1/devices/:deviceId/sync', async (req, res, next) => {
  try {
    const id = deviceId(req);
    const status = cleanStatus(req.body?.status);
    await deviceRef(id).set({ status, updatedAt: FieldValue.serverTimestamp() }, { merge: true });
    const ready = !status.stopPending && !status.fault && !['preparing', 'running'].includes(status.operation?.state);
    const action = await nextAction(id, ready);
    res.set('Cache-Control', 'no-store').json({ action });
  } catch (error) { next(error); }
});

const directory = path.dirname(fileURLToPath(import.meta.url));
app.use(express.static(path.join(directory, 'public'), { index: 'index.html', maxAge: 0 }));
app.use((error, _req, res, _next) => res.status(error.status || 500).json({ error: error.message || 'Gateway error' }));
app.listen(process.env.PORT || 8080, () => console.log('Timiac cloud gateway is listening'));
