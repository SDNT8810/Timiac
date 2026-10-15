import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

const source = readFileSync(new URL('../Simulator/src/main.js', import.meta.url), 'utf8');
const css = readFileSync(new URL('../Simulator/src/style.css', import.meta.url), 'utf8');

test('clock quick actions expose a Struggle button that delegates to the existing random movement', () => {
  const quickMarkup = source.slice(source.indexOf('<div class="clock-quick-actions"'), source.indexOf('<canvas id="clock-3d"'));
  assert.match(quickMarkup, /<button id="quick-struggle"[^>]*aria-label="Struggle"[^>]*>STRUGGLE<\/button>/);
  assert.equal((quickMarkup.match(/<button /g) || []).length, 4);
  const binding = source.split('\n').find(line => line.includes("querySelector('#quick-struggle').addEventListener"));
  assert.ok(binding, 'quick Struggle must have a click handler');
  let listener, launches = 0;
  runInNewContext(binding, {
    document: { querySelector: selector => { assert.equal(selector, '#quick-struggle'); return { addEventListener: (name, callback) => { assert.equal(name, 'click'); listener = callback; } }; } },
    els: { struggle: { click: () => launches++ } },
  });
  listener();
  assert.equal(launches, 1);
  const barStyle = css.slice(css.indexOf('  .clock-quick-actions {'), css.indexOf('  .clock-quick-actions button {'));
  assert.match(barStyle, /grid-template-columns:\s*1\.35fr 1fr 1fr 1fr;/);
});
