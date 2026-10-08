import { test, expect } from '@playwright/test';

test('real C replay explains low-SOC admission and restoration without fabricated scores', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByRole('heading', { name: 'Decisions before dispatch.' })).toBeVisible();
  await page.getByRole('button', { name: 'Generate reference run' }).click();
  await expect(page.getByRole('heading', { name: 'Low-SOC eclipse' })).toBeVisible();
  await expect(page.locator('.candidates-table')).toContainText('soc floor');
  await expect(page.locator('.selected-mode')).toContainText('DEGRADED');
  await page.screenshot({ path: 'output/inspector-low-soc.png', fullPage: true });
  await page.getByRole('button', { name: 'Completion', exact: true }).click();
  await expect(page.locator('.state-details')).toContainText('360 / 900');
  await page.getByRole('button', { name: 'Configure', exact: false }).first().click();
  await page.getByRole('button', { name: /False-alarm restoration/ }).click();
  await page.getByRole('button', { name: 'Generate reference run' }).click();
  await expect(page.getByRole('heading', { name: 'False-alarm restoration' })).toBeVisible();
  await page.getByRole('button', { name: 'Predicted risk', exact: true }).click();
  await expect(page.locator('.time-readout')).toContainText('10');
  await expect(page.locator('.invalidation-detail')).toContainText('predicted risk');
  await page.getByRole('button', { name: 'Restoration', exact: true }).click();
  await expect(page.locator('.time-readout')).toContainText('24');
  await expect(page.locator('.selection-summary')).toContainText('10');
  await expect(page.locator('.selected-mode')).toContainText('FULL');
  await page.getByRole('button', { name: 'Completion', exact: true }).click();
  await expect(page.locator('.time-readout')).toContainText('34');
  await expect(page.locator('.state-details')).toContainText('900 / 900');
  await page.screenshot({ path: 'output/inspector-restoration.png', fullPage: true });
});

test('configuration, policy switching, evidence and mobile layout stay usable', async ({ page }) => {
  await page.goto('/');
  await page.getByRole('button', { name: /Compound stress/ }).click();
  await page.getByRole('radio', { name: /GomX style/ }).check();
  await expect(page.locator('.policy-description')).toContainText('Regular ticks only');
  await page.screenshot({ path: 'output/workstation-configure.png', fullPage: true });
  await page.getByRole('button', { name: /Compare & verify/ }).click();
  await expect(page.locator('.comparison-table tbody tr')).toHaveCount(4);
  await expect(page.locator('.full-row')).toContainText('0 / 3');
  await page.getByRole('combobox', { name: 'Scenario', exact: true }).selectOption('sudden_fault');
  await expect(page.locator('.comparison-table')).toContainText('≥ 1 ns');
  await expect(page.locator('.verification-list')).toContainText('Not run in this session');
  await page.setViewportSize({ width: 390, height: 844 });
  await page.getByRole('button', { name: /Configure/ }).click();
  await expect(page.getByRole('button', { name: 'Generate reference run' })).toBeVisible();
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth);
  await page.screenshot({ path: 'output/workstation-mobile.png', fullPage: true });
  expect(overflow).toBe(false);
});
