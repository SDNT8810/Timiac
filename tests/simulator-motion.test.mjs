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

test('all Struggle arms share 30/10/30 mm openings, then close directly into the selected time', () => {
  for (const initial of [Array(12).fill(0), Array(12).fill(100), clockPoseValues('03:30'),
    AXES.map((_, i) => (i % 3) * 50)]) for (const time of ['00:00', '01:00', '03:30', '23:59']) {
    const code = generateStruggle(initial, time);
    assert.doesNotMatch(code, /^G4/m);
    const frames = moves(code);
    assert.equal(frames.length, 4);
    assert.deepEqual(frames[0].values, Array(12).fill(60)); // 30 mm open
    assert.deepEqual(frames[1].values, Array(12).fill(20)); // 10 mm open
    assert.deepEqual(frames[2].values, Array(12).fill(60));
    assert.deepEqual(frames[3].values, clockPoseValues(time));
    // The all-arm sweeps use the same XYZ vector-feed ceiling as hardware.
    assert.ok(Math.abs(frames[1].feed - 800 / Math.sqrt(3)) < .001);
  }
  const fixture = moves(readFileSync(new URL('../Simulator/gcodes/circular-struggle.gcode', import.meta.url), 'utf8'));
  assert.deepEqual(fixture.map(frame => frame.values), moves(generateStruggle(Array(12).fill(100))).map(frame => frame.values));
});

test('the shorter Struggle including its final time pose stays below 27 nominal seconds at full speed', () => {
  for (const initial of [Array(12).fill(0), Array(12).fill(100), clockPoseValues('03:30')]) {
    let previous = initial, seconds = 0;
    for (const frame of moves(generateStruggle(initial))) {
      seconds += Math.max(...frame.values.map((value, i) => Math.abs(value - previous[i]))) / frame.feed * 60;
      previous = frame.values;
    }
    // Includes entry from fully open; 1x is 100%, 0.5x is the usual 50%.
    assert.ok(seconds < 27);
  }
});
