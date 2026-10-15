// Simulator positions are extension percentages; physical home/open is 0 mm.
export const AXES = 'ABCDEHIJKLUV'.split('');
export const HOUR_FEED = 700;
export const STRUGGLE_FEED = 800;
const words = values => AXES.map((axis, index) => `${axis}${values[index]}`).join(' ');

export function clockPoseValues(time) {
  const [hour, minute] = time.split(':').map(Number);
  return AXES.map((_, index) => index === Math.floor(minute / 5) % 12 ? 100 : index === hour % 12 ? 50 : 0);
}

export function generateHourMovement(time) {
  return [
    `; HOUR MOVEMENT TO ${time}: all open, then directly to time; 30% slower.`,
    'G90',
    `G1 ${words(Array(12).fill(100))} F${HOUR_FEED}`,
    `G1 ${words(clockPoseValues(time))} F${HOUR_FEED}`,
  ].join('\n');
}

export function generateStruggle(initialState) {
  let previous = [...initialState];
  const edge = initialState.map(value => value >= 50 ? 90 : 10);
  const lines = ['; STRUGGLE: two continuous 40 mm out-and-back cycles; 5-45 mm on hardware.', 'G90'];
  for (let frame = 0; frame < 5; frame++) {
    const target = frame % 2 ? edge.map(value => 100 - value) : [...edge];
    const delta = target.map((value, index) => value - previous[index]);
    const span = Math.max(...delta.map(Math.abs));
    if (span > 0) {
      // Match both boards' XYZ-first feed distances instead of making the
      // simulator's twelve simultaneous arms faster than the real mechanism.
      const distance = Math.max(...[[0, 11, 10, 9, 8, 7], [6, 5, 4, 3, 2, 1]].map(board => {
        const primary = Math.hypot(...board.slice(0, 3).map(index => delta[index]));
        return primary || Math.hypot(...board.slice(3).map(index => delta[index]));
      }));
      lines.push(`G1 ${words(target)} F${(STRUGGLE_FEED * span / Math.max(distance, span)).toFixed(3)}`);
    }
    previous = target;
  }
  return lines.join('\n');
}
