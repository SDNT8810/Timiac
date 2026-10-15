import { test, expect } from '@playwright/test';
import { AXES, generateHourMovement } from '../src/clock-motion.js';

const watchErrors = page => {
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
  return errors;
};

const showTime = async (page, value = '03:30') => {
  await page.locator('#time-input').fill(value);
  await page.locator('#compose-time').click();
};

test('old desktop appearance retains the latest axes, hourly G-code and seven Struggles', async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 900 });
  const errors = watchErrors(page);
  await page.goto('/', { waitUntil: 'networkidle' });
  await expect(page).toHaveTitle('Timiac · Kinetic Clock Studio');
  await expect(page.locator('.topbar')).toBeHidden();
  await expect(page.locator('.viewport-meta')).toBeHidden();
  await expect(page.locator('.mobile-dock')).toBeHidden();
  await expect(page.getByRole('button', { name: 'FRONT', exact: true })).toHaveCount(0);
  await expect(page.locator('#mobile-motion')).toBeHidden();
  await expect(page.locator('#mobile-time')).toBeVisible();
  await expect(page.locator('.axis-slider')).toHaveCount(12);
  await expect(page.locator('.axis-number')).toHaveCount(12);
  await expect(page.locator('#speed')).toHaveValue('1');
  const canvas = await page.locator('#clock-3d').boundingBox();
  expect(canvas.y).toBe(0);
  expect(canvas.width).toBeGreaterThan(800);

  await page.locator('.axis-slider').first().evaluate(input => {
    input.value = '73'; input.dispatchEvent(new Event('input', { bubbles: true }));
  });
  await expect(page.locator('.axis-number').first()).toHaveValue('73');
  await expect(page.locator('#gcode')).toHaveValue(/A73/);
  await page.locator('#half-all').click();
  for (const input of await page.locator('.axis-number').all()) await expect(input).toHaveValue('50');
  const halfCode = await page.locator('#gcode').inputValue();
  for (const axis of AXES) expect(halfCode).toMatch(new RegExp(`(?:^|\\s)${axis}50(?:\\s|$)`));
  expect(halfCode).not.toMatch(/(?:^|\s)[FG]50(?:\s|$)/);

  await showTime(page);
  await page.locator('#next-hour').click();
  const target = await page.locator('#time-input').inputValue();
  expect(await page.locator('#gcode').inputValue()).toBe(generateHourMovement(target));
  await expect(page.locator('#parse-state')).not.toContainText(/unknown|invalid|error/i);
  await page.locator('#stop').click();

  await expect(page.locator('#struggle-pattern option')).toHaveCount(7);
  const codes = new Set();
  for (let variant = 0; variant < 7; variant++) {
    await page.locator('#struggle-pattern').selectOption(String(variant));
    await page.locator('#preview-struggle').click();
    await expect(page.locator('#sequence-name')).toContainText(`STRUGGLE ${variant + 1}/7`);
    await expect(page.locator('#parse-state')).not.toContainText(/unknown|invalid|error/i);
    codes.add(await page.locator('#gcode').inputValue());
  }
  expect(codes.size).toBe(7);
  await page.locator('#stop').click();

  await page.locator('#credits-open').click();
  await expect(page.locator('#credits-dialog')).toBeVisible();
  await expect(page.locator('#credits-dialog')).toContainText('Andrea Mancuso');
  await expect(page.locator('#credits-dialog')).toContainText('Davoud Nikkhouy');
  await page.locator('#credits-close').click();
  await expect(page.locator('#credits-dialog')).toBeHidden();
  await page.screenshot({ path: process.env.ARTIFACT_DIR ? `${process.env.ARTIFACT_DIR}/hybrid-desktop.png` : 'test-results/hybrid-desktop.png', fullPage: true });
  expect(errors).toEqual([]);
});

test('feed readout labels the new G-code feed units truthfully', async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 900 });
  await page.goto('/', { waitUntil: 'networkidle' });
  await expect(page.locator('#feed-rate').locator('..').locator('span')).toHaveText('FEED %/MIN');
  await page.locator('#gcode').fill('G90\nG1 A0 F900\nG1 A100 F900');
  await page.locator('#play').click();
  await expect(page.locator('#feed-rate')).toHaveText('900');
});

test('manual number inputs normalize out-of-range and cleared entries when committed', async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 900 });
  await page.goto('/', { waitUntil: 'networkidle' });
  const input = page.locator('.axis-number').first();
  const slider = page.locator('.axis-slider').first();
  await input.fill('120'); await input.blur();
  await expect(input).toHaveValue('100'); await expect(slider).toHaveValue('100');
  await expect(page.locator('#gcode')).toHaveValue(/A100/);
  await input.fill(''); await input.blur();
  await expect(input).toHaveValue('100'); await expect(slider).toHaveValue('100');
  await input.fill('-12'); await input.blur();
  await expect(input).toHaveValue('0'); await expect(slider).toHaveValue('0');
  await expect(page.locator('#gcode')).toHaveValue(/A0/);
});

test.describe('clock-first mobile appearance', () => {
  test.use({ viewport: { width: 390, height: 844 }, isMobile: true, hasTouch: true });

  test('opens on the clock and preserves functional modal controls', async ({ page }) => {
    const errors = watchErrors(page);
    await page.goto('/', { waitUntil: 'networkidle' });
    await expect(page.locator('.mobile-dock')).toBeVisible();
    await expect(page.locator('.mobile-dock > button:visible')).toHaveCount(4);
    for (const name of ['motion', 'time', 'arms', 'code']) await expect(page.locator(`#mobile-${name}`)).toBeHidden();
    const geometry = await page.evaluate(() => ({ width: innerWidth, scroll: document.documentElement.scrollWidth, height: innerHeight, scrollHeight: document.documentElement.scrollHeight, canvas: document.querySelector('#clock-3d').getBoundingClientRect().toJSON() }));
    expect(geometry.width).toBe(390); expect(geometry.scroll).toBeLessThanOrEqual(391);
    expect(geometry.scrollHeight).toBeLessThanOrEqual(geometry.height + 1); expect(geometry.canvas.width).toBeGreaterThanOrEqual(389);
    await page.screenshot({ path: process.env.ARTIFACT_DIR ? `${process.env.ARTIFACT_DIR}/hybrid-mobile.png` : 'test-results/hybrid-mobile.png' });

    await page.locator('#quick-next-five').click();
    await expect(page.locator('#sequence-name')).toContainText('TIME POSE');
    await page.getByRole('button', { name: 'Time', exact: true }).click();
    await expect(page.locator('#mobile-time')).toBeVisible();
    await expect(page.locator('#mobile-time')).toHaveAttribute('aria-modal', 'true');
    await expect(page.locator('#mobile-time')).toHaveAccessibleName('Clock pose');
    await expect(page.locator('.viewport-shell')).toHaveAttribute('inert', '');
    await page.locator('#struggle-pattern').selectOption('6');
    await page.locator('#preview-struggle').click();
    await expect(page.locator('#sequence-name')).toContainText('STRUGGLE 7/7');
    await page.keyboard.press('Escape');
    await expect(page.locator('#mobile-time')).toBeHidden();
    await expect(page.locator('.viewport-shell')).not.toHaveAttribute('inert', '');
    await page.locator('#stop').click();

    await page.getByRole('button', { name: 'Arms', exact: true }).click();
    await expect(page.locator('#mobile-arms')).toHaveAccessibleName('Arm positions');
    await page.locator('#half-all').click();
    for (const input of await page.locator('.axis-number').all()) await expect(input).toHaveValue('50');
    await page.locator('.axis-number').last().scrollIntoViewIfNeeded();
    await expect(page.locator('.axis-number').last()).toBeVisible();
    await page.locator('#mobile-arms .mobile-sheet-close').click();

    await page.getByRole('button', { name: 'G-code', exact: true }).click();
    await expect(page.locator('#mobile-code')).toHaveAccessibleName('Motion program');
    const source = await page.locator('#gcode').inputValue();
    await page.locator('#gcode').focus(); await page.keyboard.press('Tab'); await page.keyboard.press('Shift+Tab');
    await expect(page.locator('#gcode')).toHaveValue(source);
    expect(await page.locator('#mobile-code').evaluate(e => e.contains(document.activeElement))).toBe(true);
    await page.keyboard.press('Escape');
    await expect(page.locator('#mobile-code')).toBeHidden();

    await page.getByRole('button', { name: 'About', exact: true }).click();
    await expect(page.locator('#credits-dialog')).toBeVisible();
    await page.locator('#credits-close').click();
    expect(errors).toEqual([]);
  });

  test('refits the camera when portrait width changes without a reload', async ({ page }) => {
    await page.goto('/', { waitUntil: 'networkidle' });
    const distance = async () => Number(await page.locator('#clock-3d').getAttribute('data-camera-distance'));
    await expect(page.locator('#clock-3d')).toHaveAttribute('data-camera-distance', /[0-9]/);
    const phoneDistance = await distance();
    await page.setViewportSize({ width: 768, height: 1024 });
    await expect.poll(distance).toBeLessThan(phoneDistance - 100);
    await page.setViewportSize({ width: 390, height: 844 });
    await expect.poll(distance).toBeCloseTo(phoneDistance, 0);
  });

  test('fits small phones, portrait tablets and landscape without clipped desktop columns', async ({ page }) => {
    for (const viewport of [{ width: 320, height: 568 }, { width: 768, height: 1024 }, { width: 844, height: 390 }]) {
      await page.setViewportSize(viewport); await page.goto('/', { waitUntil: 'networkidle' });
      const geometry = await page.evaluate(() => ({ width: innerWidth, scroll: document.documentElement.scrollWidth, height: innerHeight, scrollHeight: document.documentElement.scrollHeight }));
      expect(geometry.width).toBe(viewport.width); expect(geometry.scroll).toBeLessThanOrEqual(viewport.width + 1);
      expect(geometry.scrollHeight).toBeLessThanOrEqual(viewport.height + 1);
      await expect(page.locator('.mobile-dock')).toBeVisible();
      await expect(page.locator('#mobile-time')).toBeHidden();
    }
  });
});
