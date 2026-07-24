import { test, expect } from '@playwright/test';

const APP_URL = process.env.APP_URL || 'http://127.0.0.1:4000';
test.setTimeout(90_000);

test.use({
  viewport: { width: 1440, height: 900 },
  launchOptions: { args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] },
});

test('Timiac 3D workspace and manual actuator controls', async ({ page }) => {
  const consoleErrors = [];
  page.on('console', message => {
    if (message.type() === 'error') consoleErrors.push(message.text());
  });
  page.on('pageerror', error => consoleErrors.push(error.message));

  await page.goto(APP_URL, { waitUntil: 'networkidle' });
  await expect(page).toHaveTitle('Timiac · Kinetic Clock Studio');
  await expect(page.locator('.topbar')).toBeHidden();
  await expect(page.locator('.viewport-meta')).toBeHidden();
  await expect(page.locator('.mobile-dock')).toHaveCount(1);
  await expect(page.locator('.mobile-dock')).toBeHidden();
  await expect(page.locator('.mobile-sheet').first()).toBeVisible();
  await expect(page.locator('#clock-3d')).toBeVisible();
  await expect(page.getByRole('button', { name: 'FRONT' })).toHaveCount(0);
  await expect(page.locator('.axis-slider')).toHaveCount(12);
  await expect(page.locator('.axis-number')).toHaveCount(12);
  await expect(page.locator('.footer-studio')).toHaveAttribute('href', 'https://andreamancuso.com/');

  await page.locator('#credits-open').click();
  await expect(page.locator('#credits-dialog')).toBeVisible();
  await expect(page.locator('#credits-dialog')).toContainText('TIMIAC is a robotic clock designed by Andrea Mancuso');
  await expect(page.locator('#credits-dialog')).toContainText('designed and developed by Davoud Nikkhouy');
  await expect(page.locator('#credits-dialog .credit-links a')).toHaveCount(3);
  await expect(page.locator('#credits-dialog a[href="https://andreamancuso.com/about"]')).toBeVisible();
  await expect(page.locator('#credits-dialog a[href="https://www.instagram.com/andreamancuso_studio/"]')).toBeVisible();
  await page.screenshot({ path: 'artifacts/timiac-credits.png', fullPage: true });
  await page.locator('#credits-close').click();
  await expect(page.locator('#credits-dialog')).toBeHidden();

  const canvasBox = await page.locator('#clock-3d').boundingBox();
  const leftBox = await page.locator('.panel-left').boundingBox();
  const rightBox = await page.locator('.panel-right').boundingBox();
  expect(canvasBox.y).toBe(0);
  expect(canvasBox.width).toBeGreaterThan(800);
  expect(canvasBox.width).toBeGreaterThan((leftBox.width + rightBox.width) * 1.5);

  await page.locator('.axis-slider').first().evaluate(element => {
    element.value = '73';
    element.dispatchEvent(new Event('input', { bubbles: true }));
  });
  await expect(page.locator('.axis-number').first()).toHaveValue('73');
  await expect(page.locator('#sequence-name')).toContainText('MANUAL POSE / ARM A');
  await expect(page.locator('#gcode')).toHaveValue(/A73/);

  await page.locator('#half-all').click();
  for (const input of await page.locator('.axis-number').all()) await expect(input).toHaveValue('50');
  await expect(page.locator('#gcode')).toHaveValue(/A50 B50 C50 D50 E50 F50 G50 H50 I50 J50 K50 L50/);

  await page.locator('#extend-all').click();
  for (const input of await page.locator('.axis-number').all()) await expect(input).toHaveValue('100');

  await page.locator('#fold-all').click();
  for (const input of await page.locator('.axis-number').all()) await expect(input).toHaveValue('0');

  await page.screenshot({ path: 'artifacts/timiac-redesign.png', fullPage: true });
  expect(consoleErrors).toEqual([]);
});
