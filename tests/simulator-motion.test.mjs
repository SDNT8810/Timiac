import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';
import { AXES, clockPoseValues, generateHourMovement, generateStruggle, STRUGGLE_PATTERNS, chooseStruggleVariant, FIXED_SPEED } from '../Simulator/src/clock-motion.js';

function moves(code) {
  return code.split(/\r?\n/).filter(line => /^G1 /.test(line)).map(line => ({
    values: AXES.map(axis => Number(line.match(new RegExp(`(?:^| )${axis}([\\d.]+)(?: |$)`))[1])),
    feed: Number(line.match(/ F([\d.]+)/)[1]),
  }));
}

test('every Next Hour target opens, flutters within 37-48 mm for three seconds, then closes directly into time', () => {
  for (let minute = 0; minute < 1440; minute++) {
    const time = `${Math.floor(minute / 60)}:${minute % 60}`;
    const frames = moves(generateHourMovement(time));
    assert.equal(frames.length, 5);
    assert.deepEqual(frames[0].values, Array(12).fill(100));
    assert.deepEqual(frames.at(-1).values, clockPoseValues(time));
    assert.equal(frames.at(-1).values[Math.floor((minute % 60) / 5)], 100);
    assert.equal(frames[0].feed, 700); assert.equal(frames.at(-1).feed, 700);
    let previous = frames[0].values, seconds = 0;
    for (const frame of frames.slice(1, -1)) {
      assert.ok(frame.values.every(value => value >= 74 && value <= 96));
      assert.ok(new Set(frame.values).size > 1);
      seconds += Math.max(...frame.values.map((value, i) => Math.abs(value - previous[i]))) / frame.feed * 60 / FIXED_SPEED;
      previous = frame.values;
    }
    assert.ok(Math.abs(seconds - 3) < 0.001);
  }
  const example = readFileSync(new URL('../Simulator/gcodes/hour-movement.gcode', import.meta.url), 'utf8');
  assert.deepEqual(moves(example), moves(generateHourMovement('01:00')));
});

test('all seven Struggle previews stay within 20 mm and close directly into the selected time', () => {
  assert.equal(STRUGGLE_PATTERNS.length, 7);
  for (const initial of [Array(12).fill(0), Array(12).fill(100), clockPoseValues('03:30'),
    AXES.map((_, i) => (i % 3) * 50)]) for (const time of ['00:00', '01:00', '03:30', '23:59']) for (let variant = 0; variant < 7; variant++) {
    const code = generateStruggle(initial, time, variant);
    assert.doesNotMatch(code, /^G4/m);
    const frames = moves(code);
    assert.equal(frames.length, 4);
    assert.deepEqual(frames.at(-1).values, clockPoseValues(time));
    for (const frame of frames.slice(0, -1)) {
      assert.ok(frame.values.every(value => value >= 0 && value <= 40)); // 40% of 50 mm = 20 mm.
      assert.ok(new Set(frame.values).size > 1); // No all-arm symmetric sweep.
    }
    assert.ok(frames.every(frame => frame.feed > 0 && frame.feed <= 1040));
  }
  const fixture = moves(readFileSync(new URL('../Simulator/gcodes/circular-struggle.gcode', import.meta.url), 'utf8'));
  assert.deepEqual(fixture.map(frame => frame.values), moves(generateStruggle(Array(12).fill(100))).map(frame => frame.values));
});

test('each insect study is distinct, asymmetric, and matches the firmware source data', () => {
  const source = JSON.parse(readFileSync(new URL('../shared/struggle-patterns.json', import.meta.url), 'utf8'));
  assert.equal(new Set(STRUGGLE_PATTERNS.map(pattern => JSON.stringify(pattern.frames))).size, 7);
  STRUGGLE_PATTERNS.forEach((pattern, index) => {
    assert.equal(pattern.name, source[index].name);
    assert.deepEqual(pattern.frames.map(frame => frame.positions), source[index].frames.map(frame => frame.openMm.map(value => value * 2)));
    assert.deepEqual(pattern.frames.map(frame => frame.feed), source[index].frames.map(frame => frame.feed));
    assert.ok(pattern.frames.some(frame => frame.positions.some((value, arm) => value !== frame.positions[(arm + 6) % 12])));
    assert.ok(pattern.frames.some(frame => frame.positions.some((value, arm) => value !== frame.positions[(12 - arm) % 12])));
  });
});

test('each Struggle segment is exactly 30 percent faster, including the final time pose', () => {
  for (let variant = 0; variant < 7; variant++) {
    let previous = clockPoseValues('03:30');
    const frames = moves(generateStruggle(previous, '03:30', variant));
    frames.forEach((frame, index) => {
      const delta = frame.values.map((value, arm) => value - previous[arm]);
      const span = Math.max(...delta.map(Math.abs));
      const distance = Math.max(span, ...[[0, 11, 10, 9, 8, 7], [6, 5, 4, 3, 2, 1]].map(board =>
        Math.hypot(...board.slice(0, 3).map(arm => delta[arm])) || Math.hypot(...board.slice(3).map(arm => delta[arm]))));
      const oldFeed = Math.min(STRUGGLE_PATTERNS[variant].frames[index]?.feed ?? 800, 800) * span / distance;
      assert.ok(Math.abs(frame.feed - oldFeed * 1.3) <= .00051);
      previous = frame.values;
    });
  }
});

test('random Struggle selection can reach every pattern without an immediate repeat', () => {
  for (let previous = -1; previous < 7; previous++) {
    const seen = new Set();
    for (let entropy = 0; entropy < 700; entropy++) {
      const selected = chooseStruggleVariant(previous, entropy / 700);
      assert.ok(selected >= 0 && selected < 7 && selected !== previous);
      seen.add(selected);
    }
    assert.equal(seen.size, previous < 0 ? 7 : 6);
  }
  assert.throws(() => generateStruggle(Array(12).fill(0), '01:00', 7), /Unknown/);
});

test('all seven Struggles keep a short path including entry and the final time pose', () => {
 for (let variant = 0; variant < 7; variant++) {
  for (const initial of [Array(12).fill(0), Array(12).fill(100), clockPoseValues('03:30')]) {
    let previous = initial, seconds = 0;
    for (const frame of moves(generateStruggle(initial, '01:00', variant))) {
      seconds += Math.max(...frame.values.map((value, i) => Math.abs(value - previous[i]))) / frame.feed * 60;
      previous = frame.values;
    }
    assert.ok(seconds / FIXED_SPEED < 29, `${STRUGGLE_PATTERNS[variant].name}: ${seconds / FIXED_SPEED}`);
  }
 }
});

test('the simulator previews a chosen pattern, allows switching studies, and keeps random playback stable while busy', () => {
  const source = readFileSync(new URL('../Simulator/src/main.js', import.meta.url), 'utf8');
  const start = source.slice(source.indexOf('function startStruggle('), source.indexOf('function startHourMovement('));
  const node = () => ({ value: '', textContent: '' });
  const els = Object.fromEntries(['timeInput', 'strugglePattern', 'struggleDescription', 'editor', 'sequenceName', 'fileName', 'clockMode', 'loop'].map(id => [id, node()]));
  els.timeInput.value = '03:30';
  const clockState = { live: true, struggling: false, lastStruggleVariant: -1 };
  const player = { parsed: null, onComplete: null };
  let current = clockPoseValues('03:30');
  const context = { AXES, STRUGGLE_PATTERNS, chooseStruggleVariant, generateStruggle, els, player, clockState,
    document: { body: { classList: { add() {}, remove() {} } }, querySelectorAll: () => [] },
    stateAt: () => ({ values: current }), formatClockTime: () => '03:30', setPlaying() {},
    rebuildProgram: () => { player.parsed = moves(els.editor.value); current = player.parsed.at(-1).values; },
  };
  runInNewContext(`${start}; startStruggle({variant: 2});`, context);
  assert.match(els.sequenceName.textContent, /3\/7.*CAUGHT WING/);
  assert.equal(els.strugglePattern.value, '2');
  assert.deepEqual(player.parsed.at(-1).values, clockPoseValues('03:30'));
  runInNewContext('startStruggle({variant: 6});', context);
  assert.match(els.sequenceName.textContent, /7\/7.*LAST WRIGGLE/);
  const code = els.editor.value;
  runInNewContext('startStruggle();', context);
  assert.equal(els.editor.value, code); // Random button cannot reroll a running sequence.
  player.onComplete();
  assert.equal(clockState.struggling, false);
  runInNewContext('startStruggle();', context);
  assert.notEqual(clockState.lastStruggleVariant, 6);
  assert.equal(clockState.live, false);
});
