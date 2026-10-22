import { expect, test } from '@playwright/test';

test('mission map distinguishes the browser and concurrent paths and explains partial reads', async ({ page }) => {
  const errors: string[] = [];
  page.on('pageerror', e => errors.push(e.message));
  await page.goto('/');
  await page.getByRole('button', { name: /Configure/ }).first().click();
  await page.getByRole('button', { name: 'Explore the mission map' }).click();
  await page.locator('.architecture-lesson > summary').click();
  await expect(page.getByRole('heading', { name: 'The mission control map' })).toBeVisible();
  await page.screenshot({ path: 'output/mission-hero-desktop.png' });
  const chapters = page.getByRole('navigation', { name: 'Workflow chapters' });
  await chapters.getByRole('button', { name: 'Read the sensors' }).click();
  await expect(page.locator('.tour-selected')).toContainText('one C process');
  await expect(page.locator('.tour-selected')).toContainText('does not fork');
  await expect(page.locator('.station-active')).toContainText('Spacecraft reporter');
  await page.getByRole('button', { name: 'Concurrent CLI simulator' }).click();
  await chapters.getByRole('button', { name: 'Make two crews' }).click();
  await expect(page.locator('.tour-selected')).toContainText('fork()');
  await expect(page.locator('.tour-selected')).toContainText('separate memory');
  await page.screenshot({ path: 'output/mission-map-desktop.png' });
  await page.locator('.mission-tour').screenshot({ path: 'output/mission-tour-desktop.png' });
  const lab = page.getByRole('region', { name: 'Fixed-size message framing demonstration' });
  const assembled = lab.getByRole('progressbar');
  let extraRunRequests = 0;
  page.on('request', r => { if (r.url().endsWith('/api/runs')) extraRunRequests++; });
  for (const count of [1, 2]) {
    await lab.getByRole('button', { name: 'Read next fragment' }).click();
    await expect(assembled).toHaveAttribute('aria-valuenow', String(count));
    await expect(lab).toContainText('do not dispatch yet');
  }
  await lab.getByRole('button', { name: 'Read next fragment' }).click();
  await expect(lab).toContainText('Complete message → dispatch by type');
  await expect(lab.getByRole('button', { name: 'Read next fragment' })).toBeDisabled();
  await lab.getByRole('button', { name: 'Empty example buffer' }).click();
  await expect(assembled).toHaveAttribute('aria-valuenow', '0');
  expect(extraRunRequests).toBe(0);
  expect(errors).toEqual([]);
});

test('recommended mission links real inputs to the paused, restored and completed space story', async ({ page }) => {
  await page.goto('/');
  await page.getByRole('button', { name: /Configure/ }).first().click();
  await page.getByRole('button', { name: 'Choose the recommended demo' }).click();
  await expect(page.getByRole('region', { name: 'Mission briefing' })).toContainText('85%');
  await expect(page.getByRole('region', { name: 'Mission briefing' })).toContainText('0%');
  await expect(page.locator('.input-events')).toContainText('Available CPU → 100%');
  await page.getByRole('button', { name: 'Generate reference run' }).click();
  await page.getByRole('button', { name: 'Evidence & prior views', exact: true }).click();
  await page.getByRole('button', { name: 'Space analogy view', exact: true }).click();
  const scene = page.getByRole('region', { name: 'Space story for the selected decision' });
  await expect(scene).toContainText('pending');
  await page.getByRole('button', { name: 'Predicted risk', exact: true }).click();
  await expect(scene).toContainText('paused');
  await expect(scene).toContainText('10 sim ns remain');
  await expect(scene).toContainText('future risk');
  const progress = await scene.getByRole('img', { name: /Recorded job progress/ }).getAttribute('aria-label');
  await scene.screenshot({ path: 'output/mission-inspect-risk.png' });
  await page.getByRole('button', { name: 'Restoration', exact: true }).click();
  await expect(scene).toContainText('running');
  await expect(scene).toContainText('earlier work was kept');
  await expect(scene.getByRole('img', { name: /Recorded job progress/ })).toHaveAttribute('aria-label', progress!);
  await scene.getByRole('button', { name: 'Systems detail', exact: true }).click();
  await expect(scene.locator('.story-explanation')).toHaveCount(0);
  await expect(scene.locator('.systems-explanation')).toBeVisible();
  await page.getByRole('button', { name: 'Completion', exact: true }).click();
  await expect(scene).toContainText('complete');
  await expect(scene).toContainText('900 mission value delivered');
  await page.getByRole('slider', { name: 'Inspection cursor' }).focus();
  await page.keyboard.press('End');
  await expect(scene).toContainText('No new frame');
  await expect(scene).toContainText('No new telemetry');
});

test('mission chapters, story lenses and inspector remain readable on small screens', async ({ page }) => {
  await page.setViewportSize({ width: 320, height: 800 });
  await page.goto('/');
  await page.getByRole('button', { name: /Configure/ }).first().click();
  await page.getByRole('button', { name: 'Explore the mission map' }).click();
  await page.locator('.architecture-lesson > summary').click();
  for (const lens of ['Space story', 'Systems detail', 'Story + systems']) {
    await page.getByRole('button', { name: lens, exact: true }).click();
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
  }
  await page.getByRole('button', { name: 'Concurrent CLI simulator' }).click();
  await expect(page.getByRole('heading', { name: /A pipe carries bytes/ })).toBeVisible();
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
  await page.screenshot({ path: 'output/mission-map-mobile.png' });
  await page.getByRole('button', { name: 'Set up the guided demo' }).first().click();
  await page.getByRole('button', { name: 'Generate reference run' }).click();
  await page.getByRole('button', { name: 'Evidence & prior views', exact: true }).click();
  await page.getByRole('button', { name: 'Space analogy view', exact: true }).click();
  await page.getByRole('button', { name: 'Restoration', exact: true }).click();
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
  await page.locator('.live-mission').screenshot({ path: 'output/mission-inspect-mobile.png' });
});
