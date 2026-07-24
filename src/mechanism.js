// Digitized from close.jpeg, middle.jpeg and open.jpeg.
// Coordinates describe one mechanism in its radial/side plane. A quadratic
// path passes through all three supplied CAD poses exactly.
export const CAD_POSES = {
  close: {
    A: [240, 28], B: [246, 48], C: [255, 73], U: [183, 42],
    V: [172, 55], Q: [174, 108], H: [162, 117], T: [241, 115],
  },
  middle: {
    A: [240, 28], B: [237, 48], C: [234, 72], U: [178, 40],
    V: [157, 54], Q: [126, 94], H: [119, 82], T: [115, 158],
  },
  open: {
    A: [240, 28], B: [227, 43], C: [212, 63], U: [168, 48],
    V: [130, 58], Q: [118, 50], H: [95, 47], T: [16, 46],
  },
};

export const LINK_TOPOLOGY = [
  ['A', 'B', 'L3'], ['B', 'C', 'L3'],
  ['U', 'B', 'L1'], ['V', 'C', 'L2'],
  ['U', 'Q', 'L6'], ['Q', 'H', 'L7'],
  ['V', 'H', 'CPL'], ['H', 'T', 'L5'],
];

function interpolatePoint(name, stroke) {
  const t = Math.max(0, Math.min(1, stroke / 100));
  const p0 = CAD_POSES.close[name];
  const pm = CAD_POSES.middle[name];
  const p1 = CAD_POSES.open[name];
  const control = [
    2 * pm[0] - (p0[0] + p1[0]) / 2,
    2 * pm[1] - (p0[1] + p1[1]) / 2,
  ];
  const inverse = 1 - t;
  return [
    inverse * inverse * p0[0] + 2 * inverse * t * control[0] + t * t * p1[0],
    inverse * inverse * p0[1] + 2 * inverse * t * control[1] + t * t * p1[1],
  ];
}

export function cadPose(stroke) {
  return Object.fromEntries(
    Object.keys(CAD_POSES.close).map(name => [name, interpolatePoint(name, stroke)]),
  );
}

export function outputFoldAngle(pose) {
  const dx = pose.T[0] - pose.H[0];
  const dy = pose.T[1] - pose.H[1];
  return Math.atan2(Math.abs(dy), -dx);
}

export function toMechanism3D(point, stroke = 0) {
  // Side-view X becomes radial distance; side-view Y becomes depth away from
  // the clock face. The fixed C1 pivot remains close to the central hub.
  // The final half of the actuator stroke adds up to 20% radial opening while
  // keeping the supplied closed and middle calibration poses unchanged.
  const upperStroke = Math.max(0, Math.min(1, (stroke - 50) / 50));
  const openGain = 1 + 0.2 * upperStroke * upperStroke;
  return {
    radial: (240 - point[0]) * 0.86 * openGain + 34,
    depth: (point[1] - 46) * 0.86,
  };
}
