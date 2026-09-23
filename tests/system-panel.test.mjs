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
