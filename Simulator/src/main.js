import './style.css';
import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { cadPose, LINK_TOPOLOGY, outputFoldAngle, toMechanism3D } from './mechanism.js';
import breatheCode from '../gcodes/breathing.gcode?raw';
import curiousCode from '../gcodes/curious-creature.gcode?raw';
import rippleCode from '../gcodes/tidal-ripple.gcode?raw';
import scuttleCode from '../gcodes/scuttle.gcode?raw';
import struggleCode from '../gcodes/struggle.gcode?raw';
import circularStruggleCode from '../gcodes/circular-struggle.gcode?raw';
import randomStruggleCode from '../gcodes/random-struggle.gcode?raw';
import harmonicStruggleCode from '../gcodes/harmonic-struggle.gcode?raw';
import hourMovementCode from '../gcodes/hour-movement.gcode?raw';

// F is reserved for feed rate. These names address arms 1 through 12 clockwise.
const AXES = 'ABCDEHIJKLUV'.split('');
const INITIAL_FEED = 900;

const presets = [
  { id: 'hour-movement', name: 'Hour movement', note: 'circular bloom then fold', code: hourMovementCode, glyph: 'H' },
  { id: 'circular-struggle', name: 'Circular struggle', note: 'clockwise travelling impulse', code: circularStruggleCode, glyph: 'O' },
  { id: 'random-struggle', name: 'Feral random', note: 'irregular repeatable spasms', code: randomStruggleCode, glyph: '?' },
  { id: 'harmonic-struggle', name: 'Harmonic struggle', note: 'phase-locked standing wave', code: harmonicStruggleCode, glyph: '~' },
  { id: 'curious', name: 'Curious creature', note: 'reach · recoil · inspect', code: curiousCode, glyph: '◒' },
  { id: 'breathe', name: 'Slow breathing', note: 'inhale · shiver · rest', code: breatheCode, glyph: '◌' },
  { id: 'ripple', name: 'Tidal ripple', note: 'clockwise pressure wave', code: rippleCode, glyph: '↻' },
  { id: 'scuttle', name: 'Night scuttle', note: 'push · pull · settle', code: scuttleCode, glyph: '⌁' },
];

document.querySelector('#app').innerHTML = `
  <header class="topbar">
    <a class="brand" href="#" aria-label="Dodeca Motion Lab home">
      <span class="brand-mark"><i></i><i></i><i></i><i></i><i></i><i></i></span>
      <span><strong>DODECA</strong><small>MOTION LAB / 01</small></span>
    </a>
    <div class="topbar-copy">
      <span>12 ARM KINETIC CLOCK</span>
      <span class="topbar-divider"></span>
      <span>G-CODE MOTION COMPOSER</span>
    </div>
    <div class="connection"><i></i> SIMULATOR READY</div>
  </header>

  <main class="workspace">
    <aside class="panel panel-left">
      <section>
        <div class="section-heading"><span>01</span><h2>Sequences</h2></div>
        <div class="preset-list">
          ${presets.map((preset, index) => `
            <button class="preset ${index === 0 ? 'active' : ''}" data-preset="${preset.id}">
              <span class="preset-glyph">${preset.glyph}</span>
              <span><strong>${preset.name}</strong><small>${preset.note}</small></span>
              <span class="preset-arrow">↗</span>
            </button>
          `).join('')}
        </div>
      </section>

      <section class="time-composer">
        <div class="section-heading"><span>02</span><h2>Time pose</h2></div>
        <p>One arm marks the hour; the longer arm marks five-minute steps.</p>
        <div class="time-row">
          <input id="time-input" type="time" aria-label="Clock time" />
          <button id="compose-time" class="square-button" title="Show selected time">↗</button>
        </div>
        <div class="clock-actions">
          <button id="live-clock" type="button">LIVE CLOCK</button>
          <button id="struggle" type="button">STRUGGLE <kbd>S</kbd></button>
        </div>
        <div class="clock-step-actions">
          <button id="next-five" type="button">NEXT 5 MIN <kbd>5</kbd></button>
          <button id="next-hour" type="button">NEXT HOUR <kbd>H</kbd></button>
        </div>
        <div class="clock-mode"><i></i><span id="clock-mode">LIVE · HOURLY MOVEMENT ARMED</span></div>
      </section>

      <section class="axis-monitor-section">
        <div class="section-heading"><span>03</span><h2>Actuator stroke</h2></div>
        <div class="axis-monitor" id="axis-monitor"></div>
        <div class="travel-key"><span>RETRACTED</span><span>100 MM</span></div>
      </section>
    </aside>

    <section class="viewport-shell">
      <div class="viewport-meta">
        <div><span class="eyebrow">MOTION VIEWPORT</span><strong id="sequence-name">CURIOUS CREATURE</strong></div>
        <div class="viewport-readout"><span id="active-axis">A01</span><small>LEAD ARM</small></div>
      </div>

      <div class="stage view-3d" id="stage">
        <div class="view-switch" role="group" aria-label="Simulator view">
          <button data-view="front">FRONT</button>
          <button data-view="3d" class="active">3D</button>
        </div>
        <div class="stage-corner top-left">X 0.000<br>Y 0.000</div>
        <div class="stage-corner top-right">LINEAR INPUT<br>0—100 MM</div>
        <canvas id="clock-3d" aria-label="Orbitable three-dimensional view of the twelve folding arm mechanisms"></canvas>
        <svg id="clock" viewBox="0 0 700 700" role="img" aria-label="Twelve arm kinetic clock simulator">
          <defs>
            <radialGradient id="hub-face" cx="38%" cy="30%" r="70%">
              <stop offset="0" stop-color="#fafafa" />
              <stop offset="0.2" stop-color="#9ea2a0" />
              <stop offset="0.48" stop-color="#303535" />
              <stop offset="0.72" stop-color="#b6b9b6" />
              <stop offset="1" stop-color="#222727" />
            </radialGradient>
            <linearGradient id="steel" x1="0" y1="0" x2="0" y2="1">
              <stop offset="0" stop-color="#ecf0ed" />
              <stop offset="0.26" stop-color="#737a79" />
              <stop offset="0.55" stop-color="#252b2c" />
              <stop offset="0.8" stop-color="#bbc0bd" />
              <stop offset="1" stop-color="#4a5050" />
            </linearGradient>
            <linearGradient id="rod" x1="0" y1="0" x2="1" y2="0">
              <stop offset="0" stop-color="#333a3a" />
              <stop offset="0.35" stop-color="#f0f2ed" />
              <stop offset="0.58" stop-color="#686e6d" />
              <stop offset="1" stop-color="#161b1c" />
            </linearGradient>
            <filter id="arm-shadow" x="-30%" y="-60%" width="180%" height="220%">
              <feDropShadow dx="4" dy="5" stdDeviation="3.5" flood-color="#1b201f" flood-opacity=".24" />
            </filter>
            <filter id="hub-shadow" x="-80%" y="-80%" width="260%" height="260%">
              <feDropShadow dx="7" dy="10" stdDeviation="9" flood-color="#1f2322" flood-opacity=".3" />
            </filter>
          </defs>
          <g class="dial-guides">
            <circle cx="350" cy="350" r="294" />
            <circle cx="350" cy="350" r="270" />
            ${Array.from({length: 60}, (_, i) => {
              const a = (i * 6 - 90) * Math.PI / 180;
              const major = i % 5 === 0;
              const r = major ? 283 : 289;
              const r2 = 294;
              return `<line x1="${350 + Math.cos(a) * r}" y1="${350 + Math.sin(a) * r}" x2="${350 + Math.cos(a) * r2}" y2="${350 + Math.sin(a) * r2}" class="${major ? 'major' : ''}" />`;
            }).join('')}
            <text x="350" y="38">12</text><text x="665" y="356">3</text>
            <text x="350" y="676">6</text><text x="35" y="356">9</text>
          </g>
          <g id="arms" filter="url(#arm-shadow)"></g>
          <g class="hub" filter="url(#hub-shadow)">
            <circle cx="350" cy="350" r="64" fill="#252a2a" />
            <circle cx="350" cy="350" r="58" fill="url(#hub-face)" stroke="#eef0ec" stroke-width="1" />
            <circle cx="350" cy="350" r="38" fill="#303535" stroke="#aeb4b1" stroke-width="6" />
            <path d="M350 323 L373 337 L373 363 L350 377 L327 363 L327 337 Z" fill="url(#steel)" stroke="#181d1d" stroke-width="2" />
            <circle cx="350" cy="350" r="13" fill="#d9dcd7" stroke="#4b5150" stroke-width="5" />
            <circle cx="350" cy="350" r="3" fill="#171b1b" />
          </g>
        </svg>
        <div class="side-view-card">
          <div class="side-view-heading"><span>SIDE VIEW / ONE ARM</span><span>LIVE ANGLE</span></div>
          <svg id="side-view" viewBox="0 0 280 165" role="img" aria-label="One selected arm shown from the side with three reference angles">
            <defs>
              <linearGradient id="side-steel" x1="0" y1="0" x2="0" y2="1">
                <stop offset="0" stop-color="#f0f2ef" />
                <stop offset="0.32" stop-color="#777e7b" />
                <stop offset="0.7" stop-color="#282e2e" />
                <stop offset="1" stop-color="#bfc3bf" />
              </linearGradient>
            </defs>
            <path class="side-wall" d="M252 20 L252 145 M35 136 L254 136" />
            <path class="side-rail" d="M76 142 L238 142" />
            <g class="angle-ghosts">
              <line x1="232" y1="126" x2="120" y2="126" />
              <line x1="232" y1="126" x2="139" y2="63" />
              <line x1="232" y1="126" x2="190" y2="22" />
              <text x="112" y="123">0°</text>
              <text x="133" y="57">34°</text>
              <text x="184" y="17">68°</text>
            </g>
            <g id="side-fingers"></g>
            <g class="side-hub">
              <circle cx="232" cy="126" r="13" />
              <circle cx="232" cy="126" r="4" />
            </g>
          </svg>
          <div class="side-view-key"><span>SELECTED ARM</span><span>3 REFERENCE ANGLES</span></div>
        </div>
        <div class="stage-scale"><span>Ø 620 MM</span><i></i><span>FRONT + SIDE PROJECTION</span></div>
      </div>

      <div class="transport">
        <div class="transport-buttons">
          <button id="stop" class="transport-icon" title="Stop and rewind" aria-label="Stop and rewind">■</button>
          <button id="play" class="play-button" aria-label="Play"><span class="play-symbol">▶</span><span class="pause-symbol">Ⅱ</span></button>
          <button id="step" class="transport-icon" title="Step one command" aria-label="Step one command">▶|</button>
        </div>
        <div class="timeline-wrap">
          <div class="timeline-labels"><span id="current-time">00:00.0</span><span id="total-time">00:00.0</span></div>
          <input id="timeline" type="range" min="0" max="1000" value="0" aria-label="Timeline" />
        </div>
        <label class="speed-control">SPEED <select id="speed"><option value="0.5">0.5×</option><option value="1" selected>1.0×</option><option value="1.5">1.5×</option><option value="2">2.0×</option></select></label>
        <label class="loop-control"><input id="loop" type="checkbox" checked><span></span>LOOP</label>
      </div>
    </section>

    <aside class="panel panel-right">
      <div class="editor-heading">
        <div class="section-heading"><span>04</span><h2>G-code source</h2></div>
        <span class="file-name" id="file-name">curious-creature.gcode</span>
      </div>
      <div class="editor-toolbar">
        <span><i></i><b id="parse-state">VALID PROGRAM</b></span>
        <div>
          <button id="copy-code" title="Copy G-code">COPY</button>
          <button id="download-code" title="Download G-code">SAVE</button>
        </div>
      </div>
      <div class="code-editor">
        <div class="line-numbers" id="line-numbers"></div>
        <textarea id="gcode" spellcheck="false" aria-label="G-code editor"></textarea>
        <div class="active-line-marker" id="active-line-marker"></div>
      </div>
      <div class="code-status">
        <div><span>COMMAND</span><strong id="command-count">—</strong></div>
        <div><span>LINE</span><strong id="line-number">—</strong></div>
        <div><span>FEED</span><strong id="feed-rate">—</strong></div>
      </div>
      <div class="dialect-note">
        <span>AXIS MAP</span>
        <p><b>A B C D E H I J K L U V</b> map clockwise from 12 o’clock. <b>F</b> is feed rate. Values are linear actuator stroke; <b>0</b> is folded, <b>100</b> is fully unfolded.</p>
        <p><b>G1</b> move · <b>G4 P</b> dwell · <b>G90/G91</b> absolute/relative · <b>G28</b> home</p>
      </div>
    </aside>
  </main>

  <footer>
    <span>DODECA SIMULATOR / BUILD 01</span>
    <span>SPACE — PLAY/PAUSE&nbsp;&nbsp;&nbsp; R — REWIND&nbsp;&nbsp;&nbsp; S — STRUGGLE</span>
    <span>OCTOPUS MOTION STUDY · MM / MIN</span>
  </footer>
`;

const els = {
  arms: document.querySelector('#arms'),
  monitor: document.querySelector('#axis-monitor'),
  editor: document.querySelector('#gcode'),
  lineNumbers: document.querySelector('#line-numbers'),
  activeLineMarker: document.querySelector('#active-line-marker'),
  play: document.querySelector('#play'),
  stop: document.querySelector('#stop'),
  step: document.querySelector('#step'),
  timeline: document.querySelector('#timeline'),
  currentTime: document.querySelector('#current-time'),
  totalTime: document.querySelector('#total-time'),
  speed: document.querySelector('#speed'),
  loop: document.querySelector('#loop'),
  parseState: document.querySelector('#parse-state'),
  commandCount: document.querySelector('#command-count'),
  lineNumber: document.querySelector('#line-number'),
  feedRate: document.querySelector('#feed-rate'),
  sequenceName: document.querySelector('#sequence-name'),
  fileName: document.querySelector('#file-name'),
  activeAxis: document.querySelector('#active-axis'),
  stage: document.querySelector('#stage'),
  canvas3d: document.querySelector('#clock-3d'),
  timeInput: document.querySelector('#time-input'),
  composeTime: document.querySelector('#compose-time'),
  liveClock: document.querySelector('#live-clock'),
  struggle: document.querySelector('#struggle'),
  nextFive: document.querySelector('#next-five'),
  nextHour: document.querySelector('#next-hour'),
  clockMode: document.querySelector('#clock-mode'),
};

function armMarkup(axis, index) {
  const rotation = index * 30 - 90;
  return `
    <g class="arm" data-axis="${axis}" transform="rotate(${rotation} 350 350)">
      <path class="finger-base" d="M380 342 L421 346 L421 354 L380 358" />
      <circle cx="389" cy="350" r="8" fill="url(#steel)" stroke="#242a2a" stroke-width="2" />
      <g class="finger-projection">
        <line class="finger-shadow" x1="414" y1="350" x2="485" y2="350" />
        <line class="finger-beam" x1="414" y1="350" x2="485" y2="350" />
        <line class="finger-glint" x1="416" y1="347" x2="482" y2="347" />
        <path class="finger-brace upper-brace" d="M405 343 L452 345 L482 350" />
        <path class="finger-brace lower-brace" d="M405 357 L452 355 L482 350" />
      </g>
      <circle class="finger-knuckle" cx="414" cy="350" r="8" />
      <g class="arm-tip" transform="translate(485 350)">
        <path d="M-14 -9 L5 -8 L18 -3 L26 0 L18 3 L5 8 L-14 9 L-7 0 Z" fill="url(#steel)" stroke="#252a2a" stroke-width="2" />
        <circle cx="25" cy="0" r="4" fill="#d9dcd8" stroke="#232828" stroke-width="2" />
      </g>
      <text class="arm-label" x="398" y="340" transform="rotate(90 398 340)">${axis}</text>
    </g>
  `;
}

els.arms.innerHTML = AXES.map(armMarkup).join('');
document.querySelector('#side-fingers').innerHTML = AXES.map((axis, index) => `
  <g class="side-finger" data-side-axis="${axis}">
    <line class="side-drive" x1="150" y1="142" x2="195" y2="110" />
    <rect class="side-slider" x="142" y="137" width="17" height="10" rx="3" />
    <line class="side-bone-shadow" x1="232" y1="126" x2="125" y2="126" />
    <line class="side-bone" x1="232" y1="126" x2="125" y2="126" />
    <circle class="side-drive-joint" cx="195" cy="110" r="4.5" />
    <circle class="side-tip" cx="125" cy="126" r="4" />
  </g>
`).join('');
els.monitor.innerHTML = AXES.map((axis, i) => `
  <div class="axis-row" data-axis-row="${axis}">
    <span><b>${String(i + 1).padStart(2, '0')}</b>${axis}</span>
    <div><i></i></div>
    <output>0</output>
  </div>
`).join('');

const armElements = AXES.map(axis => {
  const root = document.querySelector(`.arm[data-axis="${axis}"]`);
  const side = document.querySelector(`.side-finger[data-side-axis="${axis}"]`);
  return {
    root,
    beam: root.querySelector('.finger-beam'),
    shadow: root.querySelector('.finger-shadow'),
    glint: root.querySelector('.finger-glint'),
    upper: root.querySelector('.upper-brace'),
    lower: root.querySelector('.lower-brace'),
    tip: root.querySelector('.arm-tip'),
    side,
    sideBone: side.querySelector('.side-bone'),
    sideBoneShadow: side.querySelector('.side-bone-shadow'),
    sideDrive: side.querySelector('.side-drive'),
    sideSlider: side.querySelector('.side-slider'),
    sideDriveJoint: side.querySelector('.side-drive-joint'),
    sideTip: side.querySelector('.side-tip'),
    monitor: document.querySelector(`[data-axis-row="${axis}"]`),
  };
});

function createThreeMechanism() {
  const group = new THREE.Group();
  const linkMaterial = new THREE.MeshStandardMaterial({ color: 0x597d70, metalness: 0.72, roughness: 0.28 });
  const outputMaterial = new THREE.MeshStandardMaterial({ color: 0xaeb6b2, metalness: 0.92, roughness: 0.16 });
  const jointMaterial = new THREE.MeshStandardMaterial({ color: 0xd9ff59, metalness: 0.35, roughness: 0.28 });
  const links = LINK_TOPOLOGY.map(([from, to, name]) => {
    const geometry = new THREE.CylinderGeometry(name === 'L5' ? 4.6 : 2.8, name === 'L5' ? 4.6 : 2.8, 1, 10);
    const mesh = new THREE.Mesh(geometry, name === 'L5' ? outputMaterial : linkMaterial);
    mesh.castShadow = true;
    mesh.receiveShadow = true;
    group.add(mesh);
    return { from, to, name, mesh };
  });
  const joints = Object.fromEntries(Object.keys(cadPose(0)).map(name => {
    const mesh = new THREE.Mesh(new THREE.SphereGeometry(name === 'A' ? 5.2 : 3.8, 14, 10), jointMaterial);
    mesh.castShadow = true;
    group.add(mesh);
    return [name, mesh];
  }));
  return { group, links, joints, materials: { linkMaterial, outputMaterial, jointMaterial } };
}

function setCylinderBetween(mesh, start, end) {
  const direction = new THREE.Vector3().subVectors(end, start);
  const length = direction.length();
  mesh.position.copy(start).add(end).multiplyScalar(0.5);
  mesh.scale.set(1, length, 1);
  mesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), direction.normalize());
}

function mechanismPoint3D(point, stroke) {
  const mapped = toMechanism3D(point, stroke);
  return new THREE.Vector3(mapped.radial, 0, mapped.depth);
}

function initThreeView() {
  const renderer = new THREE.WebGLRenderer({ canvas: els.canvas3d, antialias: true, alpha: true });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  renderer.outputColorSpace = THREE.SRGBColorSpace;

  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(37, 1, 1, 1400);
  camera.position.set(0, 0, 650);
  camera.lookAt(0, 0, 0);

  const controls = new OrbitControls(camera, els.canvas3d);
  controls.enableDamping = true;
  controls.dampingFactor = 0.07;
  controls.minDistance = 260;
  controls.maxDistance = 850;
  controls.target.set(0, 0, 0);

  scene.add(new THREE.HemisphereLight(0xf7f7f1, 0x27302e, 2.1));
  const keyLight = new THREE.DirectionalLight(0xffffff, 4.5);
  keyLight.position.set(-180, -220, 380);
  keyLight.castShadow = true;
  keyLight.shadow.mapSize.set(2048, 2048);
  scene.add(keyLight);
  const rimLight = new THREE.DirectionalLight(0xbad8cf, 2.4);
  rimLight.position.set(300, 220, 90);
  scene.add(rimLight);

  const backPlateMaterial = new THREE.MeshStandardMaterial({ color: 0xe1e0da, metalness: 0.06, roughness: 0.82 });
  const backPlate = new THREE.Mesh(new THREE.CylinderGeometry(275, 275, 7, 96), backPlateMaterial);
  backPlate.rotation.x = Math.PI / 2;
  backPlate.position.z = -18;
  backPlate.receiveShadow = true;
  scene.add(backPlate);

  const ring = new THREE.Mesh(
    new THREE.TorusGeometry(269, 1.1, 6, 96),
    new THREE.MeshStandardMaterial({ color: 0x7d8681, metalness: 0.55, roughness: 0.4 }),
  );
  scene.add(ring);

  const hub = new THREE.Mesh(
    new THREE.CylinderGeometry(51, 51, 24, 48),
    new THREE.MeshStandardMaterial({ color: 0x3c4543, metalness: 0.92, roughness: 0.17 }),
  );
  hub.rotation.x = Math.PI / 2;
  hub.position.z = -1;
  hub.castShadow = true;
  scene.add(hub);

  const mechanisms = AXES.map((axis, index) => {
    const mechanism = createThreeMechanism();
    // Clock axes advance clockwise: A=12, D=3, G=6, J=9.
    mechanism.group.rotation.z = Math.PI / 2 - index * Math.PI / 6;
    mechanism.group.userData.axis = axis;
    scene.add(mechanism.group);
    return mechanism;
  });

  const resize = () => {
    const width = Math.max(1, els.stage.clientWidth);
    const height = Math.max(1, els.stage.clientHeight);
    renderer.setSize(width, height, false);
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
  };
  new ResizeObserver(resize).observe(els.stage);
  resize();

  const animate = () => {
    controls.update();
    renderer.render(scene, camera);
    requestAnimationFrame(animate);
  };
  animate();
  return { renderer, scene, camera, controls, mechanisms };
}

const threeView = initThreeView();

function renderThreeArms(values, leadIndex) {
  values.forEach((value, index) => {
    const mechanism = threeView.mechanisms[index];
    const pose = cadPose(value);
    Object.entries(pose).forEach(([name, point]) => mechanism.joints[name].position.copy(mechanismPoint3D(point, value)));
    mechanism.links.forEach(link => {
      setCylinderBetween(link.mesh, mechanismPoint3D(pose[link.from], value), mechanismPoint3D(pose[link.to], value));
    });
    const active = index === leadIndex;
    mechanism.materials.outputMaterial.color.setHex(active ? 0xff5f36 : 0xaeb6b2);
    mechanism.materials.jointMaterial.color.setHex(active ? 0xff8a54 : 0xd9ff59);
  });
}

document.querySelectorAll('.view-switch button').forEach(button => {
  button.addEventListener('click', () => {
    const view = button.dataset.view;
    els.stage.classList.toggle('view-3d', view === '3d');
    document.querySelectorAll('.view-switch button').forEach(item => item.classList.toggle('active', item === button));
  });
});

function stripComments(line) {
  return line.replace(/\([^)]*\)/g, '').replace(/;.*$/, '').trim();
}

function parseGcode(source, initialState = null) {
  const lines = source.split(/\r?\n/);
  const segments = [];
  const errors = [];
  let state = initialState ? [...initialState] : Array(12).fill(0);
  let absolute = true;
  let feed = INITIAL_FEED;
  let cursor = 0;

  lines.forEach((raw, lineIndex) => {
    const line = stripComments(raw).toUpperCase();
    if (!line) return;
    const commandMatch = line.match(/^([GMT])\s*(-?\d+(?:\.\d+)?)/);
    if (!commandMatch) {
      errors.push(`Line ${lineIndex + 1}: expected a G-code command`);
      return;
    }

    const family = commandMatch[1];
    const number = Number(commandMatch[2]);
    const command = `${family}${number}`;
    const params = {};
    let feedParam = null;
    const paramSource = line.slice(commandMatch[0].length);
    const paramEntries = [...paramSource.matchAll(/([A-Z])\s*(-?(?:\d+\.?\d*|\.\d+))/g)]
      .map(match => ({ letter: match[1], value: Number(match[2]) }));
    const fValues = [];
    for (const entry of paramEntries) {
      if (entry.letter === 'F') fValues.push(entry.value);
      else params[entry.letter] = entry.value;
    }

    if (command === 'G0' || command === 'G1') {
      if (fValues.length > 1) {
        errors.push(`Line ${lineIndex + 1}: F is feed rate and may appear only once`);
        return;
      }
      if (fValues.length === 1) feedParam = fValues[0];
      const invalidArm = Object.keys(params).find(letter => !AXES.includes(letter));
      if (invalidArm) {
        errors.push(`Line ${lineIndex + 1}: ${invalidArm} is not a Timiac arm name`);
        return;
      }
    } else if (fValues.length) {
      params.F = fValues.at(-1);
    }

    if (command === 'G90') { absolute = true; return; }
    if (command === 'G91') { absolute = false; return; }

    if (command === 'G4') {
      const duration = Math.max(0, params.P != null ? params.P / 1000 : (params.S || 0));
      const segment = {
        type: 'dwell', start: [...state], target: [...state], duration,
        startTime: cursor, endTime: cursor + duration, line: lineIndex + 1, raw, feed,
      };
      cursor += duration;
      segments.push(segment);
      return;
    }

    if (command === 'G28') {
      const target = Array(12).fill(0);
      const maxDelta = Math.max(...state.map((value, i) => Math.abs(target[i] - value)));
      const duration = maxDelta ? maxDelta / (feed / 60) : 0.08;
      segments.push({ type: 'move', start: [...state], target, duration, startTime: cursor, endTime: cursor + duration, line: lineIndex + 1, raw, feed });
      cursor += duration;
      state = target;
      return;
    }

    if (command === 'G0' || command === 'G1') {
      if (Number.isFinite(feedParam) && feedParam > 0) feed = feedParam;
      const target = [...state];
      let hasAxis = false;
      AXES.forEach((axis, axisIndex) => {
        if (params[axis] == null) return;
        hasAxis = true;
        const next = absolute ? params[axis] : state[axisIndex] + params[axis];
        target[axisIndex] = Math.max(0, Math.min(100, next));
      });
      if (!hasAxis) return;
      const maxDelta = Math.max(...state.map((value, i) => Math.abs(target[i] - value)));
      const duration = maxDelta ? Math.max(0.06, maxDelta / (feed / 60)) : 0.06;
      segments.push({ type: 'move', start: [...state], target, duration, startTime: cursor, endTime: cursor + duration, line: lineIndex + 1, raw, feed });
      cursor += duration;
      state = target;
      return;
    }

    if (!['M400'].includes(command)) errors.push(`Line ${lineIndex + 1}: ${command} is not supported`);
  });

  return { segments, errors, duration: cursor, lines };
}

const player = {
  parsed: null,
  position: 0,
  playing: false,
  raf: 0,
  lastFrame: 0,
  source: '',
  onComplete: null,
};

const clockState = {
  live: true,
  struggling: false,
  displayTime: '',
  lastHourToken: '',
};

function stateAt(position) {
  const { segments } = player.parsed;
  if (!segments.length) return { values: Array(12).fill(0), segment: null };
  if (position >= player.parsed.duration) {
    const segment = segments.at(-1);
    return { values: [...segment.target], segment };
  }
  const segmentIndex = Math.max(0, segments.findIndex(item => position >= item.startTime && position < item.endTime));
  const segment = segments[segmentIndex];
  if (!segment.duration || segment.type === 'dwell') return { values: [...segment.target], segment };
  const linear = Math.max(0, Math.min(1, (position - segment.startTime) / segment.duration));
  const previous = segments[segmentIndex - 1];
  const next = segments[segmentIndex + 1];
  const tangent = (left, right) => {
    if (!Number.isFinite(left) || !Number.isFinite(right) || left === 0 || right === 0 || Math.sign(left) !== Math.sign(right)) return 0;
    return 2 * left * right / (left + right);
  };
  const t2 = linear * linear;
  const t3 = t2 * linear;
  const h00 = 2 * t3 - 3 * t2 + 1;
  const h10 = t3 - 2 * t2 + linear;
  const h01 = -2 * t3 + 3 * t2;
  const h11 = t3 - t2;
  const values = segment.start.map((start, axis) => {
    const end = segment.target[axis];
    const currentSlope = (end - start) / segment.duration;
    const previousSlope = previous?.type === 'move' ? (previous.target[axis] - previous.start[axis]) / previous.duration : NaN;
    const nextSlope = next?.type === 'move' ? (next.target[axis] - next.start[axis]) / next.duration : NaN;
    const startVelocity = tangent(previousSlope, currentSlope);
    const endVelocity = tangent(currentSlope, nextSlope);
    const value = h00 * start + h10 * startVelocity * segment.duration + h01 * end + h11 * endVelocity * segment.duration;
    return Math.max(0, Math.min(100, value));
  });
  return { values, segment };
}

function actuatorKinematics(value) {
  // One whole rigid arm, seen from the side. A linear carriage and drive link
  // rotate it around its base knuckle. The twelve instances share this model.
  const hingeX = 232;
  const hingeY = 126;
  const attachRadius = 42;
  const railY = 142;
  const connectingRod = 72;
  const sliderForAngle = angle => {
    const radians = angle * Math.PI / 180;
    const attachX = hingeX - attachRadius * Math.cos(radians);
    const attachY = hingeY - attachRadius * Math.sin(radians);
    const vertical = railY - attachY;
    return attachX - Math.sqrt(Math.max(0, connectingRod ** 2 - vertical ** 2));
  };
  const foldedAngle = 68;
  const openSlider = sliderForAngle(0);
  const foldedSlider = sliderForAngle(foldedAngle);
  const targetSlider = foldedSlider + (value / 100) * (openSlider - foldedSlider);
  let low = 0;
  let high = foldedAngle;
  for (let iteration = 0; iteration < 24; iteration += 1) {
    const mid = (low + high) / 2;
    if (sliderForAngle(mid) < targetSlider) low = mid;
    else high = mid;
  }
  const angle = (low + high) / 2;
  return { angle, sliderX: targetSlider, hingeX, hingeY, attachRadius };
}

function renderArms(values) {
  let maxValue = -1;
  let maxIndex = 0;
  values.forEach((value, index) => {
    const pose = cadPose(value);
    const foldAngle = outputFoldAngle(pose);
    const openFactor = Math.pow(Math.max(0, Math.cos(foldAngle)), 0.72);
    const baseX = 414;
    const projectedLength = 42 + 136 * openFactor * (1 + 0.2 * openFactor);
    const endX = baseX + projectedLength;
    const braceKnee = baseX + projectedLength * 0.62;
    const { root, beam, shadow, glint, upper, lower, tip, side, sideBone, sideBoneShadow, sideDrive, sideSlider, sideDriveJoint, sideTip, monitor } = armElements[index];

    [beam, shadow].forEach(el => el.setAttribute('x2', endX - 8));
    glint.setAttribute('x2', endX - 11);
    upper.setAttribute('d', `M405 343 L${braceKnee} 345 L${endX - 7} 350`);
    lower.setAttribute('d', `M405 357 L${braceKnee} 355 L${endX - 7} 350`);
    tip.setAttribute('transform', `translate(${endX} 350)`);

    const sideHinge = pose.H;
    const sideTipPoint = pose.T;
    const sideScale = 0.72;
    const sideOrigin = [232, 126];
    const sideEndX = sideOrigin[0] + (sideTipPoint[0] - sideHinge[0]) * sideScale;
    const sideEndY = sideOrigin[1] + (sideTipPoint[1] - sideHinge[1]) * sideScale;
    [sideBone, sideBoneShadow].forEach(el => {
      el.setAttribute('x2', sideEndX);
      el.setAttribute('y2', sideEndY);
    });
    sideTip.setAttribute('cx', sideEndX);
    sideTip.setAttribute('cy', sideEndY);
    sideDrive.setAttribute('x1', 178 + value * -0.45);
    sideDrive.setAttribute('y1', 142);
    sideDrive.setAttribute('x2', 211 + (pose.Q[0] - pose.H[0]) * 0.25);
    sideDrive.setAttribute('y2', 112 + (pose.Q[1] - pose.H[1]) * 0.25);
    sideSlider.setAttribute('x', 170 + value * -0.45);
    sideDriveJoint.setAttribute('cx', 211 + (pose.Q[0] - pose.H[0]) * 0.25);
    sideDriveJoint.setAttribute('cy', 112 + (pose.Q[1] - pose.H[1]) * 0.25);
    side.style.setProperty('--side-depth', index);
    root.style.setProperty('--extension', value.toFixed(2));
    root.classList.toggle('reaching', value > 55);
    root.style.opacity = 0.7 + value * 0.003;
    monitor.querySelector('i').style.width = `${value}%`;
    monitor.querySelector('output').textContent = Math.round(value);
    if (value > maxValue) { maxValue = value; maxIndex = index; }
  });
  renderThreeArms(values, maxIndex);
  els.activeAxis.textContent = `A${String(maxIndex + 1).padStart(2, '0')}`;
  armElements.forEach((arm, index) => {
    const isLead = index === maxIndex && maxValue > 12;
    arm.root.classList.toggle('lead', isLead);
    arm.side.classList.toggle('lead', isLead);
    arm.side.classList.toggle('side-selected', index === maxIndex);
  });
}

function formatTime(seconds) {
  const minutes = Math.floor(seconds / 60);
  const secs = Math.floor(seconds % 60);
  const tenths = Math.floor((seconds % 1) * 10);
  return `${String(minutes).padStart(2, '0')}:${String(secs).padStart(2, '0')}.${tenths}`;
}

function updateActiveLine(line) {
  if (!line) {
    els.activeLineMarker.style.opacity = '0';
    return;
  }
  const lineHeight = 21;
  const top = 14 + (line - 1) * lineHeight - els.editor.scrollTop;
  els.activeLineMarker.style.opacity = top > -lineHeight && top < els.editor.clientHeight ? '1' : '0';
  els.activeLineMarker.style.transform = `translateY(${top}px)`;
}

function renderPosition() {
  const { values, segment } = stateAt(player.position);
  renderArms(values);
  const duration = player.parsed.duration || 0;
  els.timeline.value = duration ? Math.round(player.position / duration * 1000) : 0;
  els.currentTime.textContent = formatTime(player.position);
  els.lineNumber.textContent = segment ? String(segment.line).padStart(2, '0') : '—';
  els.feedRate.textContent = segment ? `${Math.round(segment.feed)}` : '—';
  updateActiveLine(segment?.line);
}

function setPlaying(next) {
  player.playing = next;
  document.body.classList.toggle('is-playing', next);
  els.play.setAttribute('aria-label', next ? 'Pause' : 'Play');
  if (next) {
    if (player.position >= player.parsed.duration) player.position = 0;
    player.lastFrame = performance.now();
    cancelAnimationFrame(player.raf);
    player.raf = requestAnimationFrame(tick);
  } else {
    cancelAnimationFrame(player.raf);
  }
}

function tick(now) {
  if (!player.playing) return;
  const delta = Math.min(0.08, (now - player.lastFrame) / 1000);
  player.lastFrame = now;
  player.position += delta * Number(els.speed.value);
  let completed = false;
  if (player.position >= player.parsed.duration) {
    if (els.loop.checked && player.parsed.duration > 0) {
      player.position %= player.parsed.duration;
    } else {
      player.position = player.parsed.duration;
      setPlaying(false);
      completed = true;
    }
  }
  renderPosition();
  if (completed && player.onComplete) {
    const onComplete = player.onComplete;
    player.onComplete = null;
    onComplete();
  } else if (player.playing) {
    player.raf = requestAnimationFrame(tick);
  }
}

function rebuildProgram({ reset = true, initialState = null } = {}) {
  const wasPlaying = player.playing;
  setPlaying(false);
  player.source = els.editor.value;
  player.parsed = parseGcode(player.source, initialState);
  if (reset) player.position = 0;
  player.position = Math.min(player.position, player.parsed.duration);
  els.parseState.textContent = player.parsed.errors.length ? player.parsed.errors[0] : 'VALID PROGRAM';
  els.parseState.parentElement.classList.toggle('has-error', player.parsed.errors.length > 0);
  els.commandCount.textContent = String(player.parsed.segments.length).padStart(2, '0');
  els.totalTime.textContent = formatTime(player.parsed.duration);
  updateLineNumbers();
  renderPosition();
  if (wasPlaying && player.parsed.segments.length) setPlaying(true);
}

function updateLineNumbers() {
  const count = els.editor.value.split(/\r?\n/).length;
  els.lineNumbers.innerHTML = Array.from({ length: count }, (_, i) => `<span>${String(i + 1).padStart(2, '0')}</span>`).join('');
}

function loadPreset(preset, autoplay = false) {
  setPlaying(false);
  player.onComplete = null;
  clockState.live = false;
  clockState.struggling = false;
  clockState.displayTime = '';
  document.body.classList.remove('is-struggling');
  document.querySelectorAll('.preset').forEach(button => button.classList.toggle('active', button.dataset.preset === preset.id));
  els.editor.value = preset.code.trim();
  els.sequenceName.textContent = preset.name.toUpperCase();
  els.fileName.textContent = `${preset.id === 'breathe' ? 'breathing' : preset.id === 'ripple' ? 'tidal-ripple' : preset.id}.gcode`;
  els.clockMode.textContent = 'SEQUENCE MODE · LIVE CLOCK PAUSED';
  els.loop.checked = true;
  rebuildProgram();
  if (autoplay) setPlaying(true);
}

function clockPoseValues(time) {
  const [hourText, minuteText] = time.split(':');
  const hour = Number(hourText) % 12;
  const minute = Number(minuteText);
  const hourIndex = hour;
  const minuteIndex = Math.floor(minute / 5) % 12;
  return AXES.map((axis, index) => index === minuteIndex ? 100 : index === hourIndex ? 50 : 0);
}

function generateTimePose(time, previousTime = '') {
  const [hourText, minuteText] = time.split(':');
  const hourIndex = Number(hourText) % 12;
  const minuteIndex = Math.floor(Number(minuteText) / 5) % 12;
  const previous = previousTime ? clockPoseValues(previousTime) : Array(12).fill(0);
  const target = clockPoseValues(time);
  const changed = AXES
    .map((axis, index) => previous[index] !== target[index] ? `${axis}${target[index]}` : null)
    .filter(Boolean);
  const overlap = hourIndex === minuteIndex ? `\n; Hour and minute hands overlap on axis ${AXES[hourIndex]}.` : '';
  const transition = changed.length ? `G1 ${changed.join(' ')} F780` : 'G4 P1';
  const from = previousTime ? `\n; Transition from ${previousTime}: unchanged arms remain physically still.` : '';
  return `; DODECA / TWO-ARM CLOCK ${time}\n; The 50 mm arm marks the hour; the 100 mm arm marks five-minute steps.\n; Axis A is 12 o'clock; axes continue clockwise.${from}${overlap}\nG90\n${transition}`;
}

function generateHourMovement(targetTime) {
  const lines = [
    `; DODECA / HOUR MOVEMENT TO ${targetTime}`,
    '; 1. Every arm opens together to maximum.',
    '; 2. A three-phase closed gap travels clockwise and reopens behind itself.',
    '; 3. All arms settle together: minute stays open, hour stops halfway.',
    'G90',
    `G1 ${AXES.map(axis => `${axis}100`).join(' ')} F2400`,
  ];

  // Build the leading half of a three-arm closing wave.
  for (let step = 0; step < 12; step += 1) {
    const words = [];
    if (step >= 2) words.push(`${AXES[step - 2]}0`);
    if (step >= 1) words.push(`${AXES[step - 1]}33`);
    words.push(`${AXES[step]}66`);
    lines.push(`G1 ${words.join(' ')} F3600`);
  }

  // Start opening at A before U and V have finished closing.
  lines.push('G1 A34 U0 V33 F3600');
  lines.push('G1 A67 B34 V0 F3600');
  for (let step = 2; step < 12; step += 1) {
    lines.push(`G1 ${AXES[step - 2]}100 ${AXES[step - 1]}67 ${AXES[step]}34 F3600`);
  }
  lines.push('G1 U100 V67 F3600');
  lines.push('G1 V100 F3600');

  const finalPose = clockPoseValues(targetTime);
  lines.push('; Simultaneous final settle into the new clock pose.');
  lines.push(`G1 ${AXES.map((axis, index) => `${axis}${finalPose[index]}`).join(' ')} F1800`);
  return lines.join('\n');
}

function formatClockTime(date = new Date()) {
  return `${String(date.getHours()).padStart(2, '0')}:${String(date.getMinutes()).padStart(2, '0')}`;
}

function getHourToken(date = new Date()) {
  return `${date.getFullYear()}-${date.getMonth()}-${date.getDate()}-${date.getHours()}`;
}

function showClockPose(time, { animate = false, live = true } = {}) {
  setPlaying(false);
  player.onComplete = null;
  const previousTime = clockState.displayTime;
  const initialState = previousTime ? clockPoseValues(previousTime) : Array(12).fill(0);
  clockState.live = live;
  clockState.struggling = false;
  clockState.displayTime = time;
  document.body.classList.remove('is-struggling');
  document.querySelectorAll('.preset').forEach(button => button.classList.remove('active'));
  els.timeInput.value = time;
  els.editor.value = generateTimePose(time, previousTime);
  els.sequenceName.textContent = `${live ? 'LIVE CLOCK' : 'TIME POSE'} / ${time}`;
  els.fileName.textContent = `clock-${time.replace(':', '')}.gcode`;
  els.clockMode.textContent = live ? 'LIVE · HOURLY MOVEMENT ARMED' : 'MANUAL TIME · LIVE CLOCK PAUSED';
  els.loop.checked = false;
  rebuildProgram({ initialState });
  if (animate) {
    setPlaying(true);
  } else {
    player.position = player.parsed.duration;
    renderPosition();
  }
}

function startStruggle({ automatic = false } = {}) {
  if (clockState.struggling) return;
  const time = formatClockTime();
  setPlaying(false);
  player.onComplete = null;
  clockState.live = true;
  clockState.struggling = true;
  clockState.displayTime = '';
  document.body.classList.add('is-struggling');
  document.querySelectorAll('.preset').forEach(button => button.classList.remove('active'));
  els.timeInput.value = time;
  els.editor.value = struggleCode.trim();
  els.sequenceName.textContent = `${automatic ? 'HOURLY ' : ''}SPIDER STRUGGLE`;
  els.fileName.textContent = 'struggle.gcode';
  els.clockMode.textContent = automatic ? 'HOUR CHANGED · ONE STRUGGLE' : 'MANUAL STRUGGLE · RETURNING TO CLOCK';
  els.loop.checked = false;
  rebuildProgram();
  player.onComplete = () => showClockPose(formatClockTime(), { animate: true, live: true });
  setPlaying(true);
}

function startHourMovement(targetTime, { automatic = false } = {}) {
  if (clockState.struggling) return;
  const previousTime = clockState.displayTime;
  const initialState = previousTime ? clockPoseValues(previousTime) : Array(12).fill(0);
  setPlaying(false);
  player.onComplete = null;
  clockState.live = automatic;
  clockState.struggling = true;
  clockState.displayTime = '';
  document.body.classList.add('is-struggling');
  document.querySelectorAll('.preset').forEach(button => button.classList.remove('active'));
  els.timeInput.value = targetTime;
  els.editor.value = generateHourMovement(targetTime);
  els.sequenceName.textContent = `HOUR MOVEMENT / ${targetTime}`;
  els.fileName.textContent = 'hour-movement.gcode';
  els.clockMode.textContent = automatic ? 'HOUR CHANGED · CIRCULAR BLOOM' : 'TESTING NEXT HOUR · CIRCULAR BLOOM';
  els.loop.checked = false;
  rebuildProgram({ initialState });
  player.onComplete = () => {
    // The hour movement already ends at the correct pose. Seed the clock with
    // that state so switching back to clock mode cannot jump or replay it.
    clockState.displayTime = targetTime;
    showClockPose(targetTime, { animate: false, live: automatic });
  };
  setPlaying(true);
}

function syncLiveClock({ initial = false } = {}) {
  const now = new Date();
  const time = formatClockTime(now);
  const hourToken = getHourToken(now);

  if (!clockState.lastHourToken) {
    clockState.lastHourToken = hourToken;
  } else if (hourToken !== clockState.lastHourToken) {
    clockState.lastHourToken = hourToken;
    startHourMovement(time, { automatic: true });
    return;
  }

  if (clockState.live && !clockState.struggling && time !== clockState.displayTime) {
    showClockPose(time, { animate: !initial, live: true });
  }
}

function stepClock({ nextFive = false, hours = 0, runHourMovement = false } = {}) {
  const base = clockState.displayTime || els.timeInput.value || formatClockTime();
  const [hourText, minuteText] = base.split(':');
  let totalMinutes = Number(hourText) * 60 + Number(minuteText);
  totalMinutes = nextFive ? (Math.floor(totalMinutes / 5) + 1) * 5 : totalMinutes + hours * 60;
  totalMinutes = ((totalMinutes % 1440) + 1440) % 1440;
  const time = `${String(Math.floor(totalMinutes / 60)).padStart(2, '0')}:${String(totalMinutes % 60).padStart(2, '0')}`;
  if (runHourMovement) startHourMovement(time);
  else showClockPose(time, { animate: true, live: false });
}

document.querySelectorAll('.preset').forEach(button => {
  button.addEventListener('click', () => loadPreset(presets.find(item => item.id === button.dataset.preset), true));
});

els.play.addEventListener('click', () => {
  if (els.editor.value !== player.source) rebuildProgram();
  if (player.parsed.segments.length) setPlaying(!player.playing);
});

els.stop.addEventListener('click', () => {
  if (clockState.struggling) {
    player.onComplete = null;
    showClockPose(formatClockTime(), { live: true });
    return;
  }
  setPlaying(false);
  player.position = 0;
  renderPosition();
});

els.step.addEventListener('click', () => {
  setPlaying(false);
  const next = player.parsed.segments.find(segment => segment.endTime > player.position + 0.001);
  player.position = next ? next.endTime : 0;
  renderPosition();
});

els.timeline.addEventListener('input', () => {
  player.position = Number(els.timeline.value) / 1000 * player.parsed.duration;
  renderPosition();
});

let editorTimer;
els.editor.addEventListener('input', () => {
  player.onComplete = null;
  clockState.live = false;
  clockState.struggling = false;
  clockState.displayTime = '';
  document.body.classList.remove('is-struggling');
  els.clockMode.textContent = 'CUSTOM G-CODE · LIVE CLOCK PAUSED';
  updateLineNumbers();
  clearTimeout(editorTimer);
  editorTimer = setTimeout(() => rebuildProgram({ reset: false }), 250);
});
els.editor.addEventListener('scroll', () => {
  els.lineNumbers.scrollTop = els.editor.scrollTop;
  const segment = stateAt(player.position).segment;
  updateActiveLine(segment?.line);
});
els.editor.addEventListener('keydown', event => {
  if (event.key === 'Tab') {
    event.preventDefault();
    const start = els.editor.selectionStart;
    els.editor.setRangeText('  ', start, els.editor.selectionEnd, 'end');
    els.editor.dispatchEvent(new Event('input'));
  }
});

els.composeTime.addEventListener('click', () => {
  const time = els.timeInput.value || formatClockTime();
  showClockPose(time, { animate: true, live: false });
});

els.liveClock.addEventListener('click', () => {
  clockState.lastHourToken = getHourToken();
  showClockPose(formatClockTime(), { animate: true, live: true });
});

els.struggle.addEventListener('click', () => startStruggle());
els.nextFive.addEventListener('click', () => stepClock({ nextFive: true }));
els.nextHour.addEventListener('click', () => stepClock({ hours: 1, runHourMovement: true }));

document.querySelector('#copy-code').addEventListener('click', async event => {
  await navigator.clipboard.writeText(els.editor.value);
  const button = event.currentTarget;
  button.textContent = 'COPIED';
  setTimeout(() => { button.textContent = 'COPY'; }, 1000);
});

document.querySelector('#download-code').addEventListener('click', () => {
  const blob = new Blob([els.editor.value], { type: 'text/plain' });
  const link = document.createElement('a');
  link.href = URL.createObjectURL(blob);
  link.download = els.fileName.textContent || 'dodeca-motion.gcode';
  link.click();
  URL.revokeObjectURL(link.href);
});

document.addEventListener('keydown', event => {
  if (event.target.matches('textarea, input, select')) return;
  if (event.code === 'Space') {
    event.preventDefault();
    els.play.click();
  }
  if (event.key.toLowerCase() === 'r') els.stop.click();
  if (event.key.toLowerCase() === 's') startStruggle();
  if (event.key === '5') els.nextFive.click();
  if (event.key.toLowerCase() === 'h') els.nextHour.click();
});

syncLiveClock({ initial: true });
setInterval(syncLiveClock, 1000);
