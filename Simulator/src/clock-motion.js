// Simulator positions are extension percentages; physical home/open is 0 mm.
import { STRUGGLE_PATTERNS } from './struggle-patterns.generated.js';
export { STRUGGLE_PATTERNS };
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

export function chooseStruggleVariant(previous = -1, random = Math.random()) {
  if (!(random >= 0 && random < 1)) throw new RangeError('Random value must be in [0, 1)');
  const avoid = Number.isInteger(previous) && previous >= 0 && previous < STRUGGLE_PATTERNS.length;
  const choice = Math.floor(random * (STRUGGLE_PATTERNS.length - (avoid ? 1 : 0)));
  return avoid && choice >= previous ? choice + 1 : choice;
}

export function generateStruggle(initialState, time = '01:00', variant = 0) {
  const pattern = STRUGGLE_PATTERNS[variant];
  if (!pattern) throw new RangeError('Unknown Struggle pattern');
  let previous = [...initialState];
  const lines = [`; STRUGGLE ${variant + 1}/7: ${pattern.name}. Opening 0-20 mm, then directly to ${time}.`, 'G90'];
  const poses = [...pattern.frames, { positions: clockPoseValues(time), feed: STRUGGLE_FEED }];
  for (const { positions: target, feed } of poses) {
    const delta = target.map((value, index) => value - previous[index]);
    const span = Math.max(...delta.map(Math.abs));
    if (span > 0) {
      // Match both boards' XYZ-first feed distances instead of making the
      // simulator's twelve simultaneous arms faster than the real mechanism.
      const distance = Math.max(...[[0, 11, 10, 9, 8, 7], [6, 5, 4, 3, 2, 1]].map(board => {
        const primary = Math.hypot(...board.slice(0, 3).map(index => delta[index]));
        return primary || Math.hypot(...board.slice(3).map(index => delta[index]));
      }));
      lines.push(`G1 ${words(target)} F${(Math.min(feed, STRUGGLE_FEED) * span / Math.max(distance, span)).toFixed(3)}`);
    }
    previous = target;
  }
  return lines.join('\n');
}
