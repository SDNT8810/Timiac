import { test, expect } from '@playwright/test';

const APP_URL = process.env.APP_URL || 'http://127.0.0.1:4000';
test.setTimeout(120_000);

test.use({
  viewport: { width: 390, height: 844 },
  isMobile: true,
  hasTouch: true,
  deviceScaleFactor: 2,
  launchOptions: { args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] },
});

const sheet = (page, name) => page.locator(`.mobile-sheet[data-mobile-sheet="${name}"]`);
const toMinutes = time => {
  const [hours, minutes] = time.split(':').map(Number);
  return hours * 60 + minutes;
};
const fromMinutes = minutes => {
  const wrapped = ((minutes % 1440) + 1440) % 1440;
  return `${String(Math.floor(wrapped / 60)).padStart(2, '0')}:${String(wrapped % 60).padStart(2, '0')}`;
};

test('main page exposes usable clock pose quick actions', async ({ page }) => {
  await page.goto(APP_URL, { waitUntil: 'networkidle' });

  const quickActions = page.getByRole('group', { name: 'Clock pose quick actions' });
  await expect(quickActions).toBeVisible();
  await expect(quickActions.getByRole('button')).toHaveCount(3);

  const actualTime = quickActions.getByRole('button', { name: 'Actual time', exact: true });
  const nextHour = quickActions.getByRole('button', { name: '+1 hr', exact: true });
  const nextFive = quickActions.getByRole('button', { name: '+5 min', exact: true });
  for (const button of [actualTime, nextHour, nextFive]) {
    const box = await button.boundingBox();
    expect(box.height).toBeGreaterThanOrEqual(44);
  }
  const stageBox = await page.locator('#stage').boundingBox();
  const quickActionsBox = await quickActions.boundingBox();
  expect(quickActionsBox.y).toBeLessThanOrEqual(stageBox.y + 51);

  const startingTime = await page.locator('#time-input').inputValue();
  const expectedFive = fromMinutes((Math.floor(toMinutes(startingTime) / 5) + 1) * 5);
  await nextFive.click();
  await expect(page.locator('#time-input')).toHaveValue(expectedFive);
  await expect(page.locator('#sequence-name')).toHaveText(`TIME POSE / ${expectedFive}`);

  await actualTime.click();
  await expect(page.locator('#clock-mode')).toContainText('LIVE');
  const actual = await page.locator('#time-input').inputValue();

  await nextHour.click();
  const hourTarget = await page.locator('#time-input').inputValue();
  const hourDelta = (toMinutes(hourTarget) - toMinutes(actual) + 1440) % 1440;
  expect([60, 61]).toContain(hourDelta);
  await expect(page.locator('#clock-mode')).toContainText('TESTING NEXT HOUR');
});

test('phone opens on the clock and exposes controls as popup sheets', async ({ page }) => {
  const consoleErrors = [];
  page.on('console', message => {
    if (message.type() === 'error') consoleErrors.push(message.text());
  });
  page.on('pageerror', error => consoleErrors.push(error.message));

  await page.goto(APP_URL, { waitUntil: 'networkidle' });
  await expect(page.locator('meta[name="viewport"]')).toHaveAttribute('content', /viewport-fit=cover/);
  await expect(page.locator('.mobile-dock')).toBeVisible();
  await expect(page.locator('.mobile-dock > button')).toHaveCount(5);
  await expect(page.locator('.mobile-sheet')).toHaveCount(4);
  await expect(page.locator('footer')).toBeHidden();

  for (const name of ['motion', 'time', 'arms', 'code']) await expect(sheet(page, name)).toBeHidden();

  const metrics = await page.evaluate(() => {
    const box = selector => document.querySelector(selector).getBoundingClientRect().toJSON();
    return {
      innerWidth: window.innerWidth,
      innerHeight: window.innerHeight,
      scrollWidth: document.documentElement.scrollWidth,
      scrollHeight: document.documentElement.scrollHeight,
      canvas: box('#clock-3d'),
      viewport: box('.viewport-shell'),
      transport: box('.transport'),
      dock: box('.mobile-dock'),
    };
  });

  expect(metrics.innerWidth).toBe(390);
  expect(metrics.scrollWidth).toBeLessThanOrEqual(391);
  expect(metrics.scrollHeight).toBeLessThanOrEqual(metrics.innerHeight + 1);
  expect(metrics.canvas.width).toBeGreaterThanOrEqual(389);
  expect(metrics.canvas.height).toBeGreaterThanOrEqual(300);
  expect(metrics.transport.width).toBeGreaterThanOrEqual(389);
  expect(metrics.viewport.height + metrics.dock.height).toBeLessThanOrEqual(metrics.innerHeight + 1);

  await page.screenshot({ path: 'artifacts/timiac-mobile-clock-only.png' });

  const dockButtons = {
    motion: page.getByRole('button', { name: 'Motion', exact: true }),
    time: page.getByRole('button', { name: 'Time', exact: true }),
    arms: page.getByRole('button', { name: 'Arms', exact: true }),
    code: page.getByRole('button', { name: 'G-code', exact: true }),
  };

  for (const [name, button] of Object.entries(dockButtons)) {
    await button.click();
    await expect(sheet(page, name)).toBeVisible();
    await expect(button).toHaveAttribute('aria-expanded', 'true');
    await expect(page.locator('.topbar')).toHaveAttribute('inert', '');
    await expect(page.locator('.viewport-shell')).toHaveAttribute('inert', '');
    await expect(page.locator('.mobile-dock')).toHaveAttribute('inert', '');
    const sheetBox = await sheet(page, name).boundingBox();
    const dockBox = await page.locator('.mobile-dock').boundingBox();
    expect(sheetBox.width, name).toBeLessThanOrEqual(390);
    expect(sheetBox.y + sheetBox.height, name).toBeLessThanOrEqual(dockBox.y + 1);

    if (name === 'motion') {
      for (let index = 0; index < 14; index += 1) {
        await page.keyboard.press('Tab');
        expect(await sheet(page, name).evaluate(item => item.contains(document.activeElement))).toBe(true);
      }
      await page.screenshot({ path: 'artifacts/timiac-mobile-motion-sheet.png' });
    }
    if (name === 'arms') {
      await page.locator('#half-all').click();
      for (const input of await page.locator('.axis-number').all()) await expect(input).toHaveValue('50');
      await page.screenshot({ path: 'artifacts/timiac-mobile-arms-sheet.png' });
      await page.locator('.axis-number').last().scrollIntoViewIfNeeded();
      await expect(page.locator('.axis-number').last()).toBeVisible();
    }
    if (name === 'code') {
      await expect(page.locator('#gcode')).toBeVisible();
      const sourceBeforeTab = await page.locator('#gcode').inputValue();
      await page.locator('#gcode').focus();
      await page.keyboard.press('Tab');
      await expect(page.locator('#gcode')).toHaveValue(sourceBeforeTab);
      expect(await sheet(page, name).evaluate(item => item.contains(document.activeElement))).toBe(true);
      await page.keyboard.press('Shift+Tab');
      await expect(page.locator('#gcode')).toHaveValue(sourceBeforeTab);
    }

    await sheet(page, name).getByRole('button', { name: new RegExp(`Close ${name}`, 'i') }).click();
    await expect(sheet(page, name)).toBeHidden();
    await expect(button).toHaveAttribute('aria-expanded', 'false');
    await expect(page.locator('.topbar')).not.toHaveAttribute('inert', '');
    await expect(page.locator('.viewport-shell')).not.toHaveAttribute('inert', '');
    await expect(page.locator('.mobile-dock')).not.toHaveAttribute('inert', '');
    await expect(button).toBeFocused();
  }

  await dockButtons.arms.click();
  await expect(sheet(page, 'arms')).toBeVisible();
  await page.locator('.mobile-sheet-backdrop').click({ position: { x: 10, y: 10 } });
  await expect(sheet(page, 'arms')).toBeHidden();

  await dockButtons.time.click();
  await expect(sheet(page, 'time')).toHaveAttribute('aria-modal', 'true');
  await page.keyboard.press('Escape');
  await expect(sheet(page, 'time')).toBeHidden();

  const aboutButton = page.getByRole('button', { name: 'About', exact: true });
  await aboutButton.click();
  await expect(aboutButton).toHaveAttribute('aria-expanded', 'true');
  await expect(page.locator('#credits-dialog')).toBeVisible();
  const dialogBox = await page.locator('#credits-dialog').boundingBox();
  expect(dialogBox.width).toBeLessThanOrEqual(374);
  await page.locator('#credits-close').click();
  await expect(aboutButton).toHaveAttribute('aria-expanded', 'false');

  expect(consoleErrors).toEqual([]);
});

test('camera resets to each responsive default across a mobile desktop round trip', async ({ page }) => {
  await page.goto(APP_URL, { waitUntil: 'networkidle' });
  const camera = page.locator('#clock-3d');
  const distance = async () => Number(await camera.getAttribute('data-camera-distance'));

  await expect(camera).toHaveAttribute('data-camera-distance', /\d/);
  const initialMobileDistance = await distance();
  expect(initialMobileDistance).toBeGreaterThan(720);

  await page.setViewportSize({ width: 844, height: 390 });
  await expect.poll(distance).toBeLessThan(initialMobileDistance - 100);
  const landscapeMobileDistance = await distance();

  await page.setViewportSize({ width: 390, height: 844 });
  await expect.poll(distance).toBeCloseTo(initialMobileDistance, 0);
  expect(await distance()).toBeGreaterThan(landscapeMobileDistance + 100);

  await page.setViewportSize({ width: 1200, height: 800 });
  await expect.poll(distance).toBeCloseTo(720, 0);

  await page.setViewportSize({ width: 390, height: 844 });
  await expect.poll(distance).toBeCloseTo(initialMobileDistance, 0);
});

test('clock-only shell fits small phones, large phones, and mobile landscape', async ({ browser }) => {
  const viewports = [
    { name: 'small-portrait', width: 320, height: 568 },
    { name: 'android-portrait', width: 360, height: 800 },
    { name: 'compact-iphone', width: 375, height: 667 },
    { name: 'large-portrait', width: 412, height: 915 },
    { name: 'tablet-portrait', width: 768, height: 1024 },
    { name: 'phone-landscape', width: 844, height: 390 },
  ];

  for (const viewport of viewports) {
    const context = await browser.newContext({
      viewport: { width: viewport.width, height: viewport.height },
      isMobile: true,
      hasTouch: true,
      deviceScaleFactor: 2,
    });
    const page = await context.newPage();
    const errors = [];
    page.on('console', message => {
      if (message.type() === 'error') errors.push(message.text());
    });
    page.on('pageerror', error => errors.push(error.message));

    await page.goto(APP_URL, { waitUntil: 'networkidle' });
    const metrics = await page.evaluate(() => {
      const box = selector => document.querySelector(selector).getBoundingClientRect();
      const canvas = box('#clock-3d');
      const viewportShell = box('.viewport-shell');
      const topbar = box('.topbar');
      const dock = box('.mobile-dock');
      return {
        innerWidth: window.innerWidth,
        innerHeight: window.innerHeight,
        scrollWidth: document.documentElement.scrollWidth,
        scrollHeight: document.documentElement.scrollHeight,
        canvasWidth: canvas.width,
        canvasHeight: canvas.height,
        viewportHeight: viewportShell.height,
        topbarHeight: topbar.height,
        dockHeight: dock.height,
        dockButtonHeight: box('.mobile-dock > button').height,
        quickActionsTop: box('.clock-quick-actions').top,
        stageTop: box('#stage').top,
        stageCornerVisible: getComputedStyle(document.querySelector('.stage-corner.top-left')).display !== 'none',
        visibleSheets: [...document.querySelectorAll('.mobile-sheet')].filter(item => getComputedStyle(item).display !== 'none').length,
      };
    });

    expect(metrics.innerWidth, viewport.name).toBe(viewport.width);
    expect(metrics.scrollWidth, viewport.name).toBeLessThanOrEqual(viewport.width + 1);
    expect(metrics.scrollHeight, viewport.name).toBeLessThanOrEqual(viewport.height + 1);
    expect(metrics.canvasWidth, viewport.name).toBeGreaterThanOrEqual(viewport.width - 1);
    expect(metrics.canvasHeight, viewport.name).toBeGreaterThanOrEqual(viewport.width > viewport.height ? 150 : 250);
    expect(metrics.viewportHeight + metrics.topbarHeight + metrics.dockHeight, viewport.name).toBeLessThanOrEqual(viewport.height + 1);
    expect(metrics.dockButtonHeight, viewport.name).toBeGreaterThanOrEqual(44);
    expect(metrics.visibleSheets, viewport.name).toBe(0);
    if (viewport.width <= 360 && viewport.height > viewport.width) {
      expect(metrics.quickActionsTop - metrics.stageTop, viewport.name).toBeLessThanOrEqual(3);
      expect(metrics.stageCornerVisible, viewport.name).toBe(false);
    }
    expect(errors, viewport.name).toEqual([]);

    if (['small-portrait', 'large-portrait', 'tablet-portrait', 'phone-landscape'].includes(viewport.name)) {
      await page.screenshot({ path: `artifacts/timiac-clock-only-${viewport.name}.png` });
    }
    await context.close();
  }
});
