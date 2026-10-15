import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';
import { AXES, STRUGGLE_PATTERNS } from '../Simulator/src/clock-motion.js';

const source = readFileSync(new URL('../Simulator/src/main.js', import.meta.url), 'utf8');
const css = readFileSync(new URL('../Simulator/src/style.css', import.meta.url), 'utf8');
const index = readFileSync(new URL('../Simulator/index.html', import.meta.url), 'utf8');

function renderShell() {
  const app = { innerHTML: '' };
  const context = { AXES, STRUGGLE_PATTERNS, document: { querySelector: () => app } };
  for (const [name, file] of Object.entries({ breatheCode: 'breathing', curiousCode: 'curious-creature', rippleCode: 'tidal-ripple', scuttleCode: 'scuttle', hourMovementCode: 'hour-movement' })) {
    context[name] = readFileSync(new URL(`../Simulator/gcodes/${file}.gcode`, import.meta.url), 'utf8');
  }
  runInNewContext(source.slice(source.indexOf('const presets ='), source.indexOf('const els =')), context);
  return app.innerHTML;
}

function cssRule(selector) {
  const escaped = selector.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  return css.match(new RegExp(`(?:^|\\n)${escaped} \\{([^}]+)\\}`))?.[1] || '';
}

test('approved Timiac shell restores dense desktop and clock-first mobile without exposing old Motion studies', () => {
  const html = renderShell();
  assert.match(html, /<strong>TIMIAC<\/strong>/, 'restore approved TIMIAC branding');
  assert.match(index, /<title>Timiac · Kinetic Clock Studio<\/title>/);
  assert.match(index, /viewport-fit=cover/);
  assert.match(index, /name="author" content="Davoud Nikkhouy"/);
  assert.match(index, /property="og:url" content="https:\/\/timiac.it\/"/);
  assert.match(cssRule('.topbar'), /display: none/);
  assert.match(cssRule('.viewport-meta'), /display: none/);
  assert.match(cssRule('.workspace'), /grid-template-columns: 210px minmax\(520px, 1fr\) 300px/);
  assert.match(cssRule('.viewport-shell'), /grid-template-rows: minmax\(0, 1fr\) 64px/);
  assert.doesNotMatch(html, /data-view="front"|class="view-switch"/);
  assert.match(html, /<section[^>]+id="mobile-motion"[^>]+temporarily-hidden[^>]+hidden>/);
  assert.match(html, /<button[^>]+temporarily-hidden[^>]+data-mobile-open="motion"[^>]+hidden>/);
  for (const panel of ['time', 'arms', 'code', 'about']) assert.match(html, new RegExp(`data-mobile-open="${panel}"`));
  for (const id of ['quick-live-clock', 'quick-next-hour', 'quick-next-five', 'fold-all', 'half-all', 'extend-all', 'speed', 'credits-dialog']) assert.match(html, new RegExp(`id="${id}"`));
  assert.match(html, /CLOCK DESIGN © ANDREA MANCUSO STUDIO/);
  assert.match(html, /SIMULATION & WEBSITE © 2026 DAVOUD NIKKHOUY/);
  assert.match(source, /new TextGeometry\(label/);
  assert.match(source, /mobile-\$\{width >= height \? 'landscape' : 'portrait'\}/);
  assert.match(css, /@media \(max-width: 979px\), \(max-height: 520px\) and \(pointer: coarse\)/);
  assert.match(css, /\.mobile-dock--motion-hidden \{ grid-template-columns: repeat\(4, minmax\(0, 1fr\)\)/);
});

test('every mobile sheet is labelled by its visible heading, including hidden Motion studies', () => {
  const html = renderShell();
  for (const [name, title] of Object.entries({ motion: 'Motion studies', time: 'Clock pose', arms: 'Arm positions', code: 'Motion program' })) {
    const sheet = html.match(new RegExp(`<(section|aside) id="mobile-${name}"[^>]*>[\\s\\S]*?</\\1>`))?.[0];
    assert.ok(sheet, `${name} sheet exists`);
    const headingId = sheet.match(/^[^>]*aria-labelledby="([^"]+)"/)?.[1];
    assert.ok(headingId, `${name} sheet references its heading for an accessible dialog name`);
    assert.ok(sheet.includes(`<h2 id="${headingId}">${title}</h2>`));
    assert.equal(html.split(`id="${headingId}"`).length - 1, 1, 'heading IDs are unique');
  }
});

test('all seven new Struggle patterns are reachable through the existing Time sheet', () => {
  const timePanel = renderShell().match(/<section id="mobile-time"[\s\S]*?<\/section>/)?.[0];
  assert.ok(timePanel, 'Time sheet exists');
  assert.match(timePanel, /<select id="struggle-pattern"/, 'chooser belongs in Clock pose, not the hidden catalogue');
  assert.equal((timePanel.match(/<option value=/g) || []).length, 7);
  STRUGGLE_PATTERNS.forEach(pattern => assert.ok(timePanel.includes(pattern.name)));
  assert.match(timePanel, /id="preview-struggle"/);
  const randomButton = timePanel.match(/<button id="struggle"[^>]*>/)?.[0];
  assert.ok(randomButton);
  assert.doesNotMatch(randomButton, /hidden/);
  assert.match(timePanel, /RANDOM STRUGGLE/);
  assert.match(timePanel, /0–20 MM/);
  assert.match(source, /els.previewStruggle.addEventListener\('click', \(\) => startStruggle\(\{ variant: Number\(els.strugglePattern.value\) \}\)\)/);
});

test('manual arm controls use the current twelve axes and percentage values over a 50 mm stroke', () => {
  const context = { AXES, els: { monitor: { innerHTML: '' } } };
  runInNewContext(source.slice(source.indexOf('const ARM_CLOCK_LABELS ='), source.indexOf('const armElements =')), context);
  const controls = context.els.monitor.innerHTML;
  assert.match(controls, /stroke in percent/, 'numeric controls must not claim that 100 means 100 mm');
  assert.equal((controls.match(/class="axis-slider"/g) || []).length, 12);
  assert.equal((controls.match(/class="axis-number"/g) || []).length, 12);
  assert.doesNotMatch(controls, /Arm [FG][ ,]/);
  AXES.forEach(axis => assert.ok(controls.includes(`Arm ${axis} stroke in percent`)));
  assert.match(renderShell(), /0–100% of the physical 50 mm stroke/);
  const manualStart = source.indexOf('function applyManualPose(');
  assert.ok(manualStart >= 0, 'manual controls generate a real current-engine program');
  const initial = AXES.map((_, i) => i * 7);
  let rebuiltInitial;
  const els = Object.fromEntries(['editor', 'sequenceName', 'fileName', 'clockMode', 'loop'].map(id => [id, { value: '', textContent: '' }]));
  const player = { parsed: null, onComplete: () => {}, position: 0 };
  const clockState = { live: true, struggling: true, displayTime: '03:30' };
  const manualContext = { AXES, els, player, clockState, displayedValues: initial, INITIAL_FEED: 900,
    document: { body: { classList: { remove() {} } }, querySelectorAll: () => [] },
    setPlaying() {}, renderPosition() {},
    rebuildProgram({ initialState }) { rebuiltInitial = initialState; player.parsed = { duration: 4 }; },
  };
  runInNewContext(`${source.slice(manualStart, source.indexOf('els.monitor.addEventListener', manualStart))}; applyManualPose([120, -1, ...Array(10).fill(35)], 'A');`, manualContext);
  assert.deepEqual(Array.from(rebuiltInitial), initial, 'manual changes start from the visible pose');
  assert.match(els.editor.value, /G1 A100 B0 C35 D35 E35 H35 I35 J35 K35 L35 U35 V35 F900/);
  assert.match(els.editor.value, /50 mm/);
  assert.equal(player.position, 4);
  assert.equal(clockState.live, false);
  assert.equal(clockState.struggling, false);
  assert.equal(player.onComplete, null);
  for (const id of ['fold-all', 'half-all', 'extend-all']) assert.match(source, new RegExp(`querySelector\\('#${id}'\\).addEventListener\\('click', \\(\\) => applyManualPose`));
});

test('feed readout uses percentage points per minute without converting the engine feed', () => {
  assert.match(renderShell(), /<span>FEED %\/MIN<\/span><strong id="feed-rate">/);
  assert.match(source, /els.feedRate.textContent = segment \? `\$\{Math.round\(segment.feed\)\}` : '—'/);
});

function manualInputHarness(value = 100) {
  const listeners = new Map();
  const number = { value: '', dataset: { axisIndex: '0' }, matches: selector => selector.includes('.axis-number') };
  const slider = { value: String(value), dataset: { axisIndex: '0' }, matches: selector => selector.includes('.axis-slider') };
  const monitor = {
    addEventListener(name, callback) { listeners.set(name, callback); },
    querySelector: selector => selector === '.axis-slider' ? slider : number,
  };
  const changes = [];
  const context = { AXES, els: { monitor }, displayedValues: [value, ...Array(11).fill(35)],
    armElements: [{ monitor }], applyManualPose: (values, axis) => changes.push({ values, axis }),
  };
  const start = source.indexOf("els.monitor.addEventListener('input'");
  runInNewContext(source.slice(start, source.indexOf("document.querySelector('#fold-all')", start)), context);
  return { number, slider, changes, context,
    dispatch(name, target = number) { listeners.get(name)?.({ target }); },
  };
}

test('committing an arm number restores the actual displayed pose and companion slider', () => {
  for (const event of ['change', 'focusout']) {
    for (const text of ['120', '', 'not-a-number']) {
      const h = manualInputHarness();
      h.number.value = text;
      h.slider.value = '23';
      h.dispatch(event);
      assert.equal(h.number.value, '100', `${event} normalizes ${JSON.stringify(text)}`);
      assert.equal(h.slider.value, '100');
      assert.equal(h.changes.length, 0, 'commit must not rebuild or move the pose');
    }
    const h = manualInputHarness(0);
    h.number.value = '-12';
    h.dispatch(event);
    assert.equal(h.number.value, '0');
    h.context.displayedValues[0] = 47.6;
    h.number.value = '';
    h.dispatch(event);
    assert.equal(h.number.value, '48', 'use the current displayed pose, not a cached value');
    assert.equal(h.slider.value, '48');
    h.slider.value = '27';
    h.dispatch(event, h.slider);
    assert.equal(h.slider.value, '27', 'number commit normalization must leave range gestures alone');
  }
});

test('arm input preserves intermediate typing and ignores empty or malformed numbers', () => {
  const h = manualInputHarness();
  h.number.value = '120';
  h.dispatch('input');
  assert.equal(h.number.value, '120', 'typing remains untouched until committed');
  assert.equal(h.changes.length, 1);
  assert.equal(h.changes[0].values[0], 120, 'the manual pose path still owns clamping');
  for (const value of ['', 'not-a-number', 'Infinity']) {
    h.number.value = value;
    h.dispatch('input');
  }
  assert.equal(h.changes.length, 1, 'invalid edits must not produce a zero or NaN pose');
  h.slider.value = '27';
  h.dispatch('input', h.slider);
  assert.equal(h.changes.length, 2);
  assert.equal(h.changes[1].values[0], 27);
  assert.equal(h.changes[1].axis, 'A');
});

test('restored speed selector scales the current 75 percent playback base instead of replacing it', () => {
  const tickSource = source.slice(source.indexOf('function tick('), source.indexOf('function rebuildProgram('));
  const speedStart = source.indexOf('function playbackSpeed(');
  const helper = speedStart >= 0 ? source.slice(speedStart, source.indexOf('function renderPosition(', speedStart)) : '';
  for (const multiplier of [0.5, 1, 1.5, 2]) {
    const player = { playing: true, lastFrame: 1000, position: 0, parsed: { duration: 100 }, onComplete: null };
    const context = { player, FIXED_SPEED: 0.75, els: { loop: { checked: false }, speed: { value: String(multiplier) } },
      setPlaying() {}, renderPosition() {}, requestAnimationFrame: () => 1,
    };
    runInNewContext(`${helper}\n${tickSource}; tick(1080);`, context);
    assert.ok(Math.abs(player.position - 0.08 * 0.75 * multiplier) < 1e-9, `${multiplier}× must be relative to the fixed 75% base`);
  }
  assert.match(renderShell(), /<option value="1" selected>1.0×<\/option>/);
  assert.match(source, /currentTime.textContent = formatTime\(player.position \/ playbackSpeed\(\)\)/);
  assert.match(source, /totalTime.textContent = formatTime\(player.parsed.duration \/ playbackSpeed\(\)\)/);
});

function cameraResizeHarness() {
  const vector = () => ({ x: 0, y: 0, z: 0, set(x, y, z) { Object.assign(this, { x, y, z }); } });
  const stage = { clientWidth: 390, clientHeight: 640 };
  const camera = { fov: 37, aspect: 1, position: vector(), updateProjectionMatrix() {} };
  const controls = { target: vector(), update() {} };
  const context = { camera, controls, els: { stage }, mobileLayoutQuery: { matches: true },
    renderer: { setSize() {} }, publishCameraDistance() {},
    THREE: { MathUtils: { degToRad: degrees => degrees * Math.PI / 180 } },
  };
  const stateStart = source.indexOf('let responsiveMode =');
  const state = source.slice(stateStart, source.indexOf('  scene.add(', stateStart));
  const resizeStart = source.indexOf('  const resize = () =>');
  const resize = source.slice(resizeStart, source.indexOf('  const resizeObserver =', resizeStart));
  runInNewContext(`${state}\n${resize}\nglobalThis.resize = resize;`, context);
  return { stage, camera, controls, resize: context.resize };
}

test('camera refits on same-orientation mobile width changes in both directions', () => {
  const h = cameraResizeHarness();
  h.resize();
  const phoneDistance = h.camera.position.z;
  Object.assign(h.stage, { clientWidth: 768, clientHeight: 900 });
  h.resize();
  assert.ok(h.camera.position.z < phoneDistance - 100, 'wider portrait tablet needs a closer fit');
  Object.assign(h.stage, { clientWidth: 390, clientHeight: 640 });
  h.resize();
  assert.equal(h.camera.position.z, phoneDistance, 'returning to phone width restores its fit');
});

test('same-width mobile height changes preserve user orbit, zoom and target', () => {
  const h = cameraResizeHarness();
  h.resize();
  h.camera.position.set(120, -80, 1100);
  h.controls.target.set(10, 20, 30);
  for (const height of [700, 600, 640]) {
    h.stage.clientHeight = height;
    h.resize();
    assert.deepEqual([h.camera.position.x, h.camera.position.y, h.camera.position.z], [120, -80, 1100]);
    assert.deepEqual([h.controls.target.x, h.controls.target.y, h.controls.target.z], [10, 20, 30]);
    assert.equal(h.camera.aspect, h.stage.clientWidth / height, 'projection still follows stage height');
  }
});

function mobileHarness() {
  const callbacks = { window: {}, document: {} };
  const document = { activeElement: null, body: null, querySelectorAll: () => [], addEventListener(name, fn) { callbacks.document[name] = fn; } };
  function node() {
    const attrs = new Map();
    const classes = new Set();
    return {
      dataset: {}, inert: false, hidden: false, handlers: {},
      classList: { add: value => classes.add(value), remove: value => classes.delete(value), contains: value => classes.has(value) },
      setAttribute: (name, value) => attrs.set(name, value), removeAttribute: name => attrs.delete(name), getAttribute: name => attrs.get(name),
      addEventListener(name, fn) { this.handlers[name] = fn; },
      focus() { document.activeElement = this; }, getClientRects: () => [1], matches: () => false,
    };
  }
  const query = { matches: true, addEventListener() {} };
  const sheets = ['time', 'arms', 'code'].map(name => {
    const item = node(); item.dataset.mobileSheet = name;
    const close = node(), editor = node();
    item.querySelector = () => close;
    item.querySelectorAll = () => [close, editor];
    item.controls = [close, editor];
    return item;
  });
  const triggers = ['time', 'arms', 'code', 'about'].map(name => {
    const item = node(); item.dataset.mobileOpen = name; return item;
  });
  document.body = node();
  document.querySelectorAll = selector => selector === '.mobile-sheet' ? sheets : selector === '[data-mobile-open]' ? triggers : [];
  const els = { topbar: node(), viewportShell: node(), mobileDock: node(), mobileBackdrop: node(), creditsDialog: node(), play: node() };
  let plays = 0;
  els.play.click = () => plays++;
  els.creditsDialog.showModal = () => { els.creditsDialog.open = true; };
  const context = { document, els, window: { matchMedia: () => query, addEventListener(name, fn) { callbacks.window[name] = fn; } }, requestAnimationFrame: fn => fn() };
  const modalSource = source.slice(source.indexOf('const mobileLayoutQuery ='), source.indexOf('function armMarkup('));
  const keyboardSource = source.slice(source.lastIndexOf("document.addEventListener('keydown'"), source.indexOf('syncLiveClock({ initial: true })'));
  runInNewContext(`${modalSource}\n${keyboardSource}`, context);
  return { context, els, query, sheets, triggers, document, callbacks, plays: () => plays };
}

test('mobile modal sheets trap focus, dismiss cleanly, and block background playback shortcuts', () => {
  const h = mobileHarness();
  const trigger = h.triggers[2], code = h.sheets[2];
  trigger.handlers.click();
  h.callbacks.document.keydown({ target: code.controls[0], code: 'Space', key: ' ', preventDefault() {} });
  assert.equal(h.plays(), 0, 'Space in a modal must not play the inert background simulator');
  assert.equal(code.getAttribute('role'), 'dialog');
  assert.equal(code.getAttribute('aria-modal'), 'true');
  for (const item of [h.els.topbar, h.els.viewportShell, h.els.mobileDock]) assert.equal(item.inert, true);
  assert.equal(h.document.activeElement, code.controls[0]);
  h.callbacks.window.keydown({ key: 'Tab', preventDefault() {} });
  assert.equal(h.document.activeElement, code.controls[1]);
  h.callbacks.window.keydown({ key: 'Tab', preventDefault() {} });
  assert.equal(h.document.activeElement, code.controls[0]);
  h.callbacks.window.keydown({ key: 'Tab', shiftKey: true, preventDefault() {} });
  assert.equal(h.document.activeElement, code.controls[1]);
  h.callbacks.window.keydown({ key: 'Escape' });
  assert.equal(code.getAttribute('role'), undefined);
  assert.equal(trigger.getAttribute('aria-expanded'), 'false');
  assert.equal(h.document.activeElement, trigger);
  assert.equal(h.els.viewportShell.inert, false);
  trigger.handlers.click();
  h.els.mobileBackdrop.handlers.click();
  assert.equal(code.getAttribute('aria-hidden'), 'true');
  const about = h.triggers[3];
  about.handlers.click();
  assert.equal(about.getAttribute('aria-expanded'), 'true');
  h.callbacks.document.keydown({ target: code.controls[0], code: 'Space', key: ' ', preventDefault() {} });
  assert.equal(h.plays(), 0, 'native credits dialog also blocks simulator shortcuts');
  h.els.creditsDialog.open = false;
  h.els.creditsDialog.handlers.close();
  assert.equal(about.getAttribute('aria-expanded'), 'false');
  assert.equal(h.document.activeElement, about);
});
