import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';
import type { Run } from '../src/domain';

async function runLesson(page: Page, shortcut = '1 · Fit a smaller mode'): Promise<Run> {
  await page.goto('/');
  await page.getByRole('button', { name: /Configure/ }).first().click();
  await page.getByRole('button', { name: shortcut, exact: true }).click();
  const result = page.waitForResponse(r => r.url().endsWith('/api/runs') && r.request().method() === 'POST');
  await page.getByRole('button', { name: 'Generate reference run' }).click();
  await page.getByRole('button', { name: 'Evidence & prior views', exact: true }).click();
  const response = await result;
  expect(response.status()).toBe(201);
  await expect(page.getByRole('region', { name: 'Scheduling whiteboard' })).toBeVisible();
  return response.json();
}
async function step(page: Page, name: string) {
  await page.getByRole('navigation', { name: 'Decision explanation steps' }).getByRole('button', { name: new RegExp(name) }).click();
}

test('whiteboard teaches 1000 versus 500, smaller modes, and exact allocation without rerunning C', async ({ page }) => {
  const run = await runLesson(page);
  const board = page.getByRole('region', { name: 'Scheduling whiteboard' });
  await expect(board.locator('.wb-job-request')).toContainText('1,000 mJ');
  await expect(board.locator('.wb-energy-explanation')).toContainText('0.5% above the floor');
  await expect(board.locator('.wb-budget-note')).toContainText('500 mJ free');
  await step(page, 'Safety Gate');
  await expect(board.locator('.wb-profile')).toHaveCount(3);
  await expect(board.locator('.wb-verdict.verdict-eliminated')).toHaveCount(2);
  await expect(board.locator('.wb-verdict.verdict-pass')).toHaveCount(1);
  await expect(board.getByRole('img', { name: 'DEGRADED requests 400 mJ; captured free energy 500 mJ', exact: true })).toBeVisible();
  await board.screenshot({ path: 'output/whiteboard-low-battery.png' });
  await step(page, 'Choose a mode');
  await expect(board.locator('.wb-chosen')).toContainText('DEGRADED');
  await expect(board.locator('.wb-choice-result')).toContainText('400 mJ + 10% CPU');
  await step(page, 'Account resources');
  let requests = 0;
  page.on('request', r => { if (r.url().endsWith('/api/runs')) requests++; });
  const decision = run.cycles[0].decisions[0];
  await board.getByRole('button', { name: 'Show memory, storage, communication and sensors' }).click();
  for (const dimension of ['CPU', 'ENERGY', 'MEMORY', 'STORAGE', 'COMM']) {
    const before = decision.ledgerBefore.find(e => e.dimension === dimension)!;
    await expect(board.getByRole('img', { name: `${dimension} before drawing: capacity ${before.capacity}, reserve ${before.reserve}, committed ${before.committed}, free ${before.free}`, exact: true })).toBeVisible();
  }
  await board.getByRole('button', { name: 'Draw the allocation' }).click();
  for (const e of decision.ledgerAfter.filter(e => e.dimension !== 'SENSOR')) {
    await expect(board.getByRole('img', { name: `${e.dimension} after drawing: capacity ${e.capacity}, reserve ${e.reserve}, committed ${e.committed}, free ${e.free}`, exact: true })).toBeVisible();
  }
  await expect(board).toContainText('100 mJ free');
  await board.getByRole('button', { name: 'Focus on CPU and energy' }).click();
  await board.screenshot({ path: 'output/whiteboard-allocation.png' });
  await board.getByRole('button', { name: 'Rewind to before' }).click();
  expect(requests).toBe(0);
  await page.clock.install({ time: new Date('2026-10-22T12:00:00Z') });
  await page.clock.pauseAt(new Date('2026-10-22T12:00:01Z'));
  await page.getByRole('button', { name: 'Play walkthrough', exact: true }).click();
  await page.clock.runFor(300);
  await expect(board.locator('.wb-allocation-toolbar')).toContainText('After this decision');
  await expect(page.locator('.cursor-slider')).toContainText('CYCLE 0');
  await page.getByRole('button', { name: 'Pause walkthrough', exact: true }).click();
  expect(requests).toBe(0);
});

test('two 60-percent jobs cannot claim the same 100-percent CPU capacity', async ({ page }) => {
  await runLesson(page, '2 · Share CPU between jobs');
  await step(page, 'Task state');
  const board = page.getByRole('region', { name: 'Scheduling whiteboard' });
  await expect(board.locator('.lane-running')).toContainText('T1');
  await expect(board.locator('.lane-pending')).toContainText('T2');
  await board.locator('.lane-pending button').click();
  await expect(page.getByRole('combobox', { name: 'Walkthrough task' })).toHaveValue('2');
  await expect(board.locator('.wb-budget-note')).toContainText('40% CPU free');
  await step(page, 'Safety Gate');
  await expect(board.getByRole('img', { name: 'FULL requests 60 percent CPU; captured free CPU 40 percent', exact: true })).toBeVisible();
  await expect(board.locator('.wb-verdict.verdict-eliminated')).toHaveCount(3);
  await board.screenshot({ path: 'output/whiteboard-cpu-sharing.png' });
  await step(page, 'Choose a mode');
  await expect(board.locator('.wb-fallback')).toContainText('Waiting');
});

test('pause releases recorded holds, keeps finished work, and restoration resumes it', async ({ page }) => {
  await runLesson(page, '3 · Pause and resume safely');
  const board = page.getByRole('region', { name: 'Scheduling whiteboard' });
  await expect(board.locator('.wb-seat-row')).toHaveAttribute('aria-label', /CPU 0 percent/);
  await page.getByRole('button', { name: 'Predicted risk', exact: true }).click();
  await step(page, 'Forecast & risk');
  await expect(board.locator('.wb-risk-path')).toContainText('Future risk recorded');
  await expect(board.locator('.wb-risk-path')).toContainText('82.1%');
  await expect(board.locator('.wb-risk-horizon')).toContainText('t=24');
  await expect(board.locator('.wb-work-strip')).toContainText('2 done · 10 remaining');
  const pausedProgress = await board.getByRole('progressbar').getAttribute('aria-valuenow');
  await board.screenshot({ path: 'output/whiteboard-pause.png' });
  await step(page, 'Account resources');
  await board.getByRole('button', { name: 'Draw the resource release' }).click();
  await expect(board.getByRole('img', { name: /^CPU after drawing:/ })).toHaveAttribute('aria-label', /committed 0, free 100$/);
  await page.getByRole('button', { name: 'Restoration', exact: true }).click();
  await step(page, 'Task state');
  await expect(board.locator('.wb-work-strip')).toContainText('2 done · 10 remaining');
  await expect(board.getByRole('progressbar')).toHaveAttribute('aria-valuenow', pausedProgress!);
  await expect(board.locator('.wb-state-change')).toContainText('Paused');
  await expect(board.locator('.wb-state-change')).toContainText('Working');
  await board.screenshot({ path: 'output/whiteboard-resume.png' });
  await page.getByRole('button', { name: 'Completion', exact: true }).click();
  await step(page, 'Task state');
  await expect(board.locator('.wb-work-strip')).toContainText('900 value credited once');
  await board.getByRole('button', { name: /Final state/ }).click();
  await expect(board).toContainText('no new sensor reading or allocation decision');
});

test('thermal constraints and narrow layouts remain legible through every explanation', async ({ page }) => {
  await page.setViewportSize({ width: 320, height: 800 });
  await page.goto('/');
  await page.getByRole('button', { name: /Configure/ }).first().click();
  await page.getByRole('button', { name: /High thermal load/ }).click();
  await page.getByRole('button', { name: 'Generate reference run' }).click();
  await page.getByRole('button', { name: 'Evidence & prior views', exact: true }).click();
  const board = page.getByRole('region', { name: 'Scheduling whiteboard' });
  for (const name of ['Observe inputs', 'Forecast & risk', 'Safety Gate', 'Choose a mode', 'Account resources', 'Task state']) {
    await step(page, name);
    await expect(board).toBeVisible();
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
    if (name === 'Safety Gate') {
      await expect(board.locator('.wb-constraint-note')).toContainText('100°C');
      await expect(board.locator('.wb-verdict.verdict-eliminated')).toContainText('Too much heat');
      await board.screenshot({ path: 'output/whiteboard-thermal-mobile.png' });
    }
  }
});
