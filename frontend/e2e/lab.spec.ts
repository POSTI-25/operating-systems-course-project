import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';
import type { Run } from '../src/domain';
import { buildLabItems, labStages } from '../src/lab-model';

async function start(page: Page, scenario = 'low_soc_eclipse'): Promise<Run> {
  await page.goto('/');
  await page.getByRole('combobox', { name: 'Lab scenario', exact: true }).selectOption(scenario);
  const result = page.waitForResponse(r => r.url().endsWith('/api/runs') && r.request().method() === 'POST');
  await page.getByRole('button', { name: 'Run & explore' }).first().click();
  const response = await result;
  expect(response.status()).toBe(201);
  await expect(page.getByRole('region', { name: 'Interactive scheduling lab', exact: true })).toBeVisible();
  return response.json();
}
async function chapter(page: Page, name: string) {
  await page.getByRole('navigation', { name: 'Scheduling journey' }).getByRole('button', { name: new RegExp(name) }).click();
}

test('compiler-style journey reveals candidates and allocations one item at a time with source links', async ({ page }) => {
  const run = await start(page);
  let reruns = 0;
  page.on('request', r => { if (r.url().endsWith('/api/runs')) reruns++; });
  await expect(page.locator('.sl-source-active')).toContainText('SCENARIO');
  await page.screenshot({ path: 'output/scheduler-explorer-desktop.png' });
  await chapter(page, 'Build the options');
  await expect(page.locator('.sl-mode-stream .sl-revealed')).toHaveCount(1);
  await expect(page.locator('.sl-source-active')).toContainText('TASK_MODE 1 FULL');
  await page.getByRole('button', { name: 'Next scheduling step' }).click();
  await expect(page.locator('.sl-mode-stream .sl-revealed')).toHaveCount(2);
  await expect(page.locator('.sl-source-active')).toContainText('TASK_MODE 1 THROTTLED');
  await chapter(page, 'Check what fits');
  await expect(page.locator('.sl-mode-eliminated')).toHaveCount(1);
  await expect(page.locator('.sl-gate-result')).toContainText('Battery safety check failed');
  await page.getByRole('button', { name: 'Next scheduling step' }).click();
  await expect(page.locator('.sl-mode-eliminated')).toHaveCount(2);
  await page.getByRole('button', { name: 'Next scheduling step' }).click();
  await expect(page.locator('.sl-mode-pass')).toHaveCount(1);
  await expect(page.locator('.sl-source-active')).toContainText('TASK_MODE 1 DEGRADED');
  await expect(page.locator('.sl-source-active')).toBeInViewport();
  await page.locator('.sl-layout').screenshot({ path: 'output/scheduler-lab-safety.png' });
  await chapter(page, 'Choose and allocate');
  await expect(page.locator('.sl-selection-result')).toContainText('DEGRADED');
  await expect(page.getByRole('img', { name: 'Lab CPU Before: capacity 100, reserved 0, committed 0, free 100', exact: true })).toBeVisible();
  const allocationItems = buildLabItems(run)[4];
  const cpu = allocationItems.findIndex(i => i.dimension === 'CPU');
  await page.getByRole('slider', { name: 'Step within scheduling stage' }).fill(String(cpu));
  await expect(page.locator('.sl-resource-transition')).toContainText('CPU');
  await expect(page.getByRole('img', { name: 'Lab CPU After: capacity 100, reserved 0, committed 10, free 90', exact: true })).toHaveCount(2);
  const energy = allocationItems.findIndex(i => i.dimension === 'ENERGY');
  await page.getByRole('slider', { name: 'Step within scheduling stage' }).fill(String(energy));
  await expect(page.getByRole('img', { name: 'Lab ENERGY After: capacity 500, reserved 0, committed 400, free 100', exact: true })).toHaveCount(2);
  await page.locator('.sl-layout').screenshot({ path: 'output/scheduler-lab-allocation.png' });
  expect(reruns).toBe(0);
});

test('execution replay moves the same job between lanes and preserves work across restoration', async ({ page }) => {
  await start(page, 'false_alarm_restoration');
  await page.getByRole('button', { name: 'First pause', exact: false }).click();
  await expect(page.locator('.sl-transition')).toContainText('Paused');
  const token = page.locator('.sl-process-row button[data-task="1"]');
  await expect(token).toHaveAttribute('data-phase', 'PAUSED');
  await expect(page.locator('.sl-risk-record')).toContainText('horizon t=24');
  const progress = await page.getByRole('progressbar', { name: 'Lab job 1 completed work' }).getAttribute('aria-valuenow');
  await page.locator('.sl-machine').screenshot({ path: 'output/scheduler-lab-paused.png' });
  await page.getByRole('button', { name: 'First restoration', exact: false }).click();
  await expect(token).toHaveAttribute('data-phase', 'RUNNING');
  await expect(page.getByRole('progressbar', { name: 'Lab job 1 completed work' })).toHaveAttribute('aria-valuenow', progress!);
  await expect(page.locator('.sl-detail')).toContainText('remaining 10 sim ns');
  await page.getByRole('button', { name: 'First completion', exact: false }).click();
  await expect(token).toHaveAttribute('data-phase', 'COMPLETE');
  await expect(page.locator('.sl-detail')).toContainText('900 mission value');
  await chapter(page, 'Read the result');
  await expect(page.locator('.sl-results')).toContainText('900 value');
  await expect(page.locator('.sl-resource-placeholder')).toContainText('not a new resource-allocation snapshot');
});

test('play, back, stage scrubber and artifact clicks operate on local recorded evidence', async ({ page }) => {
  await start(page);
  await chapter(page, 'Build the options');
  await page.clock.install({ time: new Date('2026-10-22T12:00:00Z') });
  await page.clock.pauseAt(new Date('2026-10-22T12:00:01Z'));
  await page.getByRole('combobox', { name: 'Scheduling playback speed' }).selectOption('500');
  await page.locator('.sl-play').click();
  await page.clock.runFor(500);
  await expect(page.locator('.sl-source-active')).toContainText('TASK_MODE 1 THROTTLED');
  await page.getByRole('button', { name: 'Previous scheduling step' }).click();
  await expect(page.locator('.sl-play')).toHaveText('Play');
  await page.clock.runFor(2000);
  await expect(page.locator('.sl-source-active')).toContainText('TASK_MODE 1 FULL');
  await page.locator('.sl-mode-stream button').filter({ hasText: 'DEGRADED' }).click();
  await expect(page.locator('.sl-source-active')).toContainText('TASK_MODE 1 DEGRADED');
  await expect(page.getByRole('slider', { name: 'Step within scheduling stage' })).toHaveValue('2');
});

test('one Play traverses every populated phase and stops after the final result without rerunning C', async ({ page }) => {
  const run = await start(page);
  const items = buildLabItems(run);
  expect(items[6]).toHaveLength(0);
  expect(items[7]).toHaveLength(1);
  let reruns = 0;
  page.on('request', r => { if (r.url().endsWith('/api/runs')) reruns++; });
  await page.clock.install({ time: new Date('2026-10-22T12:00:00Z') });
  await page.clock.pauseAt(new Date('2026-10-22T12:00:01Z'));
  await page.getByRole('combobox', { name: 'Scheduling playback speed' }).selectOption('500');
  await page.locator('.sl-play').click();
  for (let stage = 0; stage < items.length; stage++) {
    if (!items[stage].length) continue;
    await expect(page.locator('.sl-current-stage')).toContainText(labStages[stage].name);
    await expect(page.locator('.sl-play')).toHaveText('Pause');
    for (let step = 1; step < items[stage].length; step++) {
      await page.clock.runFor(500);
      await expect(page.getByRole('slider', { name: 'Step within scheduling stage' })).toHaveValue(String(step));
    }
    await page.clock.runFor(500);
  }
  await expect(page.locator('.sl-current-stage')).toContainText('Read the result');
  await expect(page.locator('.sl-results')).toContainText('360 value');
  await expect(page.locator('.sl-play')).toHaveText('Play');
  await page.clock.runFor(2000);
  await expect(page.locator('.sl-current-stage')).toContainText('Read the result');
  expect(reruns).toBe(0);
  await page.locator('.sl-play').click();
  await expect(page.locator('.sl-current-stage')).toContainText('Read the mission');
  await expect(page.locator('.sl-play')).toHaveText('Pause');
  await page.locator('.sl-play').click();
  await page.clock.runFor(1000);
  await expect(page.getByRole('slider', { name: 'Step within scheduling stage' })).toHaveValue('0');
});

test('phase boundaries and an empty phase support Play and manual navigation', async ({ page }) => {
  const run = await start(page);
  await page.clock.install({ time: new Date('2026-10-22T12:00:00Z') });
  await page.clock.pauseAt(new Date('2026-10-22T12:00:01Z'));
  await page.getByRole('combobox', { name: 'Scheduling playback speed' }).selectOption('500');
  await chapter(page, 'Build the options');
  await page.getByRole('slider', { name: 'Step within scheduling stage' }).fill(String(buildLabItems(run)[2].length - 1));
  await page.locator('.sl-play').click();
  await page.clock.runFor(500);
  await expect(page.locator('.sl-current-stage')).toContainText('Check what fits');
  await page.getByRole('button', { name: 'Previous scheduling step' }).click();
  await expect(page.locator('.sl-current-stage')).toContainText('Build the options');
  await expect(page.locator('.sl-play')).toHaveText('Play');
  await page.clock.runFor(1000);
  await expect(page.locator('.sl-current-stage')).toContainText('Build the options');
  await page.getByRole('button', { name: 'Next scheduling step' }).click();
  await expect(page.locator('.sl-current-stage')).toContainText('Check what fits');
  await page.locator('.sl-play').click();
  await chapter(page, 'React and recover');
  await expect(page.locator('.sl-empty')).toBeVisible();
  await expect(page.locator('.sl-play')).toHaveText('Play');
  await page.clock.runFor(1000);
  await expect(page.locator('.sl-current-stage')).toContainText('React and recover');
  await page.locator('.sl-play').click();
  await page.clock.runFor(500);
  await expect(page.locator('.sl-current-stage')).toContainText('Read the result');
  await expect(page.locator('.sl-play')).toHaveText('Pause');
  await page.clock.runFor(500);
  await expect(page.locator('.sl-play')).toHaveText('Play');
});

test('small-screen lab and changing the scenario regenerate evidence without overflow', async ({ page }) => {
  await page.setViewportSize({ width: 320, height: 800 });
  await start(page);
  for (const s of labStages) {
    await chapter(page, s.name);
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
  }
  const response = page.waitForResponse(r => r.url().endsWith('/api/runs') && r.request().method() === 'POST');
  await page.getByRole('combobox', { name: 'Lab scenario' }).selectOption('competing_payload_tasks');
  expect((await response).status()).toBe(201);
  await chapter(page, 'Watch jobs execute');
  await expect(page.locator('.sl-process-row button[data-task="1"]')).toHaveAttribute('data-phase', 'RUNNING');
  await expect(page.locator('.sl-process-row button[data-task="2"]')).toHaveAttribute('data-phase', 'PENDING');
  await page.screenshot({ path: 'output/scheduler-explorer-mobile.png' });
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
});
