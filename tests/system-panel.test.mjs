import test from 'node:test';
import assert from 'node:assert/strict';
import { boardStatus, renderSystemStatus } from '../WebApp/public/system-panel.js';

test('status distinguishes an offline ESP, offline Octopus, fault, and ready board', () => {
  const state = { a: { bridgeConnected: false }, operation: {} };
  assert.equal(boardStatus(state, 'a', true).label, 'ESP offline');
  state.a.bridgeConnected = true;
  assert.equal(boardStatus(state, 'a', true).label, 'Octopus offline');
  state.a.fault = 'Homing failed';
  assert.equal(boardStatus(state, 'a', true).detail, 'Homing failed');
  state.a.resetRequired = true;
  state.a.fault = 'A1 endstop stayed triggered after homing backoff';
  assert.equal(boardStatus(state, 'a', true).label, 'Reset required');
  assert.match(boardStatus(state, 'a', true).detail, /A1 endstop/);
  state.a = { bridgeConnected: true, octopusConnected: true, ready: true, homed: 0 };
  assert.equal(boardStatus(state, 'a', true).label, 'Home required');
  state.a.homed = 63;
  assert.equal(boardStatus(state, 'a', true).label, 'Ready');
  state.a.busy = true; state.operation.homing = true;
  assert.equal(boardStatus(state, 'a', true).label, 'Homing');
  assert.equal(boardStatus(state, 'a', false).label, 'Unknown');
});

test('network status reports router connectivity without claiming internet reachability', () => {
  const nodes = new Map();
  const element = id => { if (!nodes.has(id)) nodes.set(id, { textContent: '', classList: { toggle() {} } }); return nodes.get(id); };
  const state = { wifi: { ssid: 'Home', configured: true, pending: true } };
  renderSystemStatus(state, true, element); assert.match(element('wifi-status').textContent, /waiting for idle/);
  state.wifi.pending = false; state.wifi.connecting = true;
  renderSystemStatus(state, true, element); assert.match(element('wifi-status').textContent, /Connecting to Home/);
  state.wifi.connecting = false; state.wifi.connected = true; state.wifi.ip = '192.168.1.50';
  renderSystemStatus(state, true, element); assert.equal(element('wifi-status').textContent, 'Connected to Home - 192.168.1.50');
  renderSystemStatus(state, false, element); assert.equal(element('wifi-status').textContent, 'Master disconnected');
});

test('router Wi-Fi can be connected while the remote dashboard reports a TLS failure', () => {
  const nodes = new Map();
  const element = id => { if (!nodes.has(id)) nodes.set(id, { textContent: '', classList: { toggle() {} } }); return nodes.get(id); };
  const state = {wifi: {connected: true, ssid: 'Studio', ip: '192.168.1.4'},
    cloud: {enabled: true, state: 'connection failed', error: 'Cloud connection failed (-1); TLS -9984: certificate verification failed'}};
  renderSystemStatus(state, true, element);
  assert.equal(element('wifi-status').textContent, 'Connected to Studio - 192.168.1.4');
  assert.match(element('cloud-status').textContent, /TLS -9984: certificate verification failed/);
  state.cloud.state = 'connected'; state.cloud.error = '';
  renderSystemStatus(state, true, element);
  assert.equal(element('cloud-status').textContent, 'Remote dashboard connected');
  renderSystemStatus(state, false, element);
  assert.equal(element('cloud-status').textContent, 'Remote dashboard: master disconnected');
});

test('a missing UART reply is offline, while a confirmed halt requires reset', () => {
  const state = { b: { bridgeConnected: true, octopusConnected: false,
    resetRequired: false, fault: 'M119 acknowledgement timed out; checking Octopus link' } };
  assert.equal(boardStatus(state, 'b', true).label, 'Octopus offline');
  assert.equal(boardStatus(state, 'b', true).detail, state.b.fault);
  state.b.resetRequired = true;
  assert.equal(boardStatus(state, 'b', true).label, 'Reset required');
});

test('startup diagnostics distinguish an absent bridge from an incomplete driver report', () => {
  const state = { a: { bridgeConnected: false, lastSeenMs: null }, b: {
    bridgeConnected: true, octopusConnected: true, drivers: { ok: false },
    diagnostic: 'Waiting for M122 (8210 ms); last reply 300 ms ago: Testing Z connection... Error: All LOW',
  } };
  assert.match(boardStatus(state, 'a', true).detail, /No bridge has joined/);
  assert.equal(boardStatus(state, 'b', true).detail, state.b.diagnostic);
  state.a.lastSeenMs = 18321;
  assert.match(boardStatus(state, 'a', true).detail, /18 s ago/);
  state.b.fault = 'Driver check failed';
  assert.equal(boardStatus(state, 'b', true).detail, state.b.fault);
});
