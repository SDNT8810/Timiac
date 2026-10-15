import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { AXES, clockPoseValues, generateHourMovement, generateStruggle } from '../Simulator/src/clock-motion.js';

function moves(code) {
  return code.split(/\r?\n/).filter(line => /^G1 /.test(line)).map(line => ({
    values: AXES.map(axis => Number(line.match(new RegExp(`(?:^| )${axis}([\\d.]+)(?: |$)`))[1])),
    feed: Number(line.match(/ F([\d.]+)/)[1]),
  }));
}

test('every Next Hour target opens once and closes directly into time at 70 percent of the former feed', () => {
  for (let minute = 0; minute < 1440; minute++) {
    const time = `${Math.floor(minute / 60)}:${minute % 60}`;
    const frames = moves(generateHourMovement(time));
    assert.equal(frames.length, 2);
    assert.deepEqual(frames[0].values, Array(12).fill(100));
    assert.deepEqual(frames[1].values, clockPoseValues(time));
    assert.equal(frames[1].values[Math.floor((minute % 60) / 5)], 100);
    assert.ok(frames.every(frame => frame.feed === 700));
  }
  const example = readFileSync(new URL('../Simulator/gcodes/hour-movement.gcode', import.meta.url), 'utf8');
  assert.deepEqual(moves(example), moves(generateHourMovement('01:00')));
});

test('Struggle makes two 40 mm out-and-back cycles from each actual arm position without dwells', () => {
  for (const initial of [Array(12).fill(0), Array(12).fill(100), clockPoseValues('03:30'),
    AXES.map((_, i) => (i % 3) * 50)]) {
    const code = generateStruggle(initial);
    assert.doesNotMatch(code, /^G4/m);
    const frames = moves(code);
    assert.equal(frames.length, 5);
    const entry = initial.map(value => value >= 50 ? 90 : 10);
    assert.deepEqual(frames[0].values, entry);
    for (let i = 1; i < frames.length; i++) {
      for (let arm = 0; arm < 12; arm++) {
        assert.equal(Math.abs(frames[i].values[arm] - frames[i - 1].values[arm]) * .5, 40);
        assert.ok(frames[i].values[arm] >= 10 && frames[i].values[arm] <= 90);
      }
    }
    assert.deepEqual(frames[2].values, entry);
    assert.deepEqual(frames[4].values, entry);
    // The all-arm sweeps use the same XYZ vector-feed ceiling as hardware.
    assert.ok(Math.abs(frames[1].feed - 800 / Math.sqrt(3)) < .001);
  }
  const fixture = moves(readFileSync(new URL('../Simulator/gcodes/circular-struggle.gcode', import.meta.url), 'utf8'));
  assert.deepEqual(fixture.map(frame => frame.values), moves(generateStruggle(Array(12).fill(100))).map(frame => frame.values));
});
