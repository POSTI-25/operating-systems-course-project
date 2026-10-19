import { expect, test } from '@playwright/test';
import type { APIRequestContext, Page } from '@playwright/test';
import type { Run, Task } from '../src/domain';
import { steps } from '../src/walkthrough-model';
import { titles } from '../src/domain';
import { executionSegments, progress, ratio, remainingPoints } from '../src/walkthrough-model';

async function generate(page: Page, scenario: string, variant = 'full'): Promise<Run> {
  await page.goto('/');
  if (scenario !== 'low_soc_eclipse') await page.getByRole('button', { name: new RegExp(scenario) }).click();
  await page.locator(`input[name="variant"][value="${variant}"]`).check();
  const response = page.waitForResponse(response => response.url().endsWith('/api/runs') && response.request().method() === 'POST');
  await page.getByRole('button', { name: 'Generate reference run' }).click();
  const result = await response;
  expect(result.status()).toBe(201);
  await expect(page.getByRole('heading', { name: 'From conditions to execution' })).toBeVisible();
  return result.json();
}
async function capture(request: APIRequestContext, scenario: string, variant = 'full'): Promise<Run> {
  const response = await request.post('/api/runs', { data: { scenario, variant } });
  expect(response.status()).toBe(201);
  return response.json();
}
async function explanation(page: Page, name: string) {
  await page.getByRole('navigation', { name: 'Decision explanation steps' }).getByRole('button', { name: new RegExp(name) }).click();
}

test('low SOC visually eliminates unsafe modes before dispatch and accounts actual reservations', async ({ page }) => {
  await generate(page, 'low_soc_eclipse');
  await expect(page.locator('.input-tiles')).toContainText('20.5%');
  await explanation(page, 'Safety Gate');
  await expect(page.locator('.gate-card.verdict-eliminated')).toHaveCount(2);
  await expect(page.locator('.gate-card.verdict-pass')).toHaveCount(1);
  await expect(page.locator('.gate-card.verdict-pass')).toContainText('DEGRADED');
  await page.screenshot({ path: 'output/walkthrough-gate.png', fullPage: true });
  await explanation(page, 'Choose a mode');
  await expect(page.locator('.selection-result')).toContainText('DEGRADED');
  await expect(page.locator('.selection-result')).toContainText('449.099872');
  await explanation(page, 'Account resources');
  await expect(page.getByRole('img', { name: 'CPU after: capacity 100, reserve 0, committed 10, free 90', exact: true })).toBeVisible();
  await expect(page.getByRole('img', { name: 'ENERGY after: capacity 500, reserve 0, committed 400, free 100', exact: true })).toBeVisible();
});

test('restoration walkthrough preserves work and synchronizes chart, risk, resources and task lanes', async ({ page, request }) => {
  await generate(page, 'False-alarm restoration');
  await page.getByRole('button', { name: 'Predicted risk', exact: true }).click();
  await expect(page.locator('.cycle-story')).toContainText('10 sim ns preserved');
  await explanation(page, 'Forecast & risk');
  await expect(page.locator('.risk-outcome')).toContainText('horizon t=24');
  await expect(page.locator('.risk-outcome')).toContainText('predicted risk');
  await explanation(page, 'Task state');
  await expect(page.locator('.task-lane.phase-paused .task-token')).toContainText('10 ns remaining');
  const pausedProgress = await page.getByRole('progressbar', { name: 'Task 1 completed execution' }).getAttribute('aria-valuenow');
  await page.screenshot({ path: 'output/walkthrough-paused.png', fullPage: true });
  await page.getByRole('button', { name: 'Restoration', exact: true }).click();
  await expect(page.locator('.cycle-story')).toContainText('resumed in FULL with 10 sim ns remaining');
  await explanation(page, 'Task state');
  await expect(page.locator('.task-lane.phase-running .task-token')).toContainText('10 ns remaining');
  expect(await page.getByRole('progressbar', { name: 'Task 1 completed execution' }).getAttribute('aria-valuenow')).toBe(pausedProgress);
  await explanation(page, 'Account resources');
  await expect(page.getByRole('img', { name: 'CPU after: capacity 100, reserve 0, committed 30, free 70', exact: true })).toBeVisible();
  await page.screenshot({ path: 'output/walkthrough-restored.png', fullPage: true });
  await page.getByRole('button', { name: 'Completion', exact: true }).click();
  await explanation(page, 'Task state');
  await expect(page.locator('.task-lane.phase-complete .task-token')).toContainText('value 900');
  const run = await capture(request, 'false_alarm_restoration');
  const task = run.scenario.tasks[0];
  expect(progress(task, run.cycles.find(c => c.time === '10')!.states[0])).toBeCloseTo(16.66, 1);
  expect(progress(task, run.cycles.find(c => c.time === '24')!.states[0])).toBeCloseTo(16.66, 1);
  expect(executionSegments(run, task.id).filter(s => s.phase === 'PAUSED').map(s => [s.start, s.end])).toEqual([['10', '12'], ['12', '14'], ['14', '16'], ['16', '18'], ['18', '20'], ['20', '22'], ['22', '24']]);
});

test('competing tasks and fault-triggered cycles have selectable, accurate task views', async ({ page }) => {
  await generate(page, 'Competing payloads');
  await explanation(page, 'Task state');
  await expect(page.locator('.task-lane.phase-running .task-token')).toHaveCount(1);
  await expect(page.locator('.task-lane.phase-pending .task-token')).toHaveCount(1);
  await page.locator('.task-lane.phase-pending .task-token').click();
  await expect(page.getByRole('combobox', { name: 'Walkthrough task' })).toHaveValue('2');
  await explanation(page, 'Safety Gate');
  await expect(page.locator('.gate-card.verdict-eliminated')).toHaveCount(3);
  await page.getByRole('button', { name: /Configure/ }).first().click();
  await page.getByRole('button', { name: /Compound stress/ }).click();
  await page.getByRole('button', { name: 'Generate reference run' }).click();
  await page.getByRole('button', { name: 'Fault cycle', exact: true }).click();
  await expect(page.locator('.time-readout')).toContainText('3');
  await expect(page.locator('.engine-block')).toContainText('Fault cycle');
  await expect(page.locator('.execution-lane')).toHaveCount(3);
  await explanation(page, 'Task state');
  await expect(page.locator('.task-board')).toContainText('paused');
  await page.screenshot({ path: 'output/walkthrough-compound.png', fullPage: true });
});

test('automatic walkthrough explains a cycle before advancing and manual navigation pauses it', async ({ page }) => {
  await generate(page, 'low_soc_eclipse');
  await page.clock.install({ time: new Date('2026-10-15T12:00:00Z') });
  await page.clock.pauseAt(new Date('2026-10-15T12:00:01Z'));
  await page.getByRole('combobox', { name: 'Playback speed' }).selectOption('500');
  await page.getByRole('button', { name: 'Play walkthrough', exact: true }).click();
  await page.clock.runFor(500);
  await expect(page.locator('.active-step')).toContainText('Forecast & risk');
  await expect(page.locator('.cursor-slider')).toContainText('CYCLE 0');
  await page.getByRole('button', { name: 'Next cycle', exact: true }).click();
  await expect(page.getByRole('button', { name: 'Play walkthrough', exact: true })).toBeVisible();
  await expect(page.locator('.active-step')).toContainText('Observe inputs');
  await expect(page.locator('.cursor-slider')).toContainText('CYCLE 1');
  await page.clock.runFor(3000);
  await expect(page.locator('.cursor-slider')).toContainText('CYCLE 1');
});

test('guided inspector stays within mobile width through every explanation and final state', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await generate(page, 'False-alarm restoration');
  await page.getByRole('button', { name: 'Restoration', exact: true }).click();
  for (const name of ['Observe inputs', 'Forecast & risk', 'Safety Gate', 'Choose a mode', 'Account resources', 'Task state']) {
    await explanation(page, name);
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(true);
  }
  await page.screenshot({ path: 'output/walkthrough-mobile.png', fullPage: true });
  await page.getByRole('slider', { name: 'Inspection cursor' }).focus();
  await page.keyboard.press('End');
  await expect(page.locator('.cursor-slider')).toContainText('FINAL');
  await expect(page.locator('.task-lane.phase-complete .task-token')).toContainText('value 900');
  await explanation(page, 'Safety Gate');
  await expect(page.locator('.step-empty')).toContainText('Task execution has settled');
});

test('plot model preserves 64-bit times, equal-time cycles and completion during final partial tick', () => {
  expect(ratio('9223372036854775807', '18446744073709551614')).toBe(50);
  const task = { id: '1', duration: '3' } as Task;
  const running = { task: '1', phase: 'RUNNING', remaining: '3', completed: null, value: '0' };
  const complete = { task: '1', phase: 'COMPLETE', remaining: '0', completed: '3', value: '900' };
  const run = { scenario: { duration: '5', tasks: [task] }, cycles: [{ time: '0', states: [running] }, { time: '0', states: [running] }], final: { time: '5', states: [complete] } } as Run;
  expect(executionSegments(run, '1')).toEqual([{ start: '0', end: '3', phase: 'RUNNING', cycle: 1 }, { start: '3', end: '5', phase: 'COMPLETE', cycle: 1 }]);
  expect(remainingPoints(run, task).map(point => point.time)).toEqual(['0', '0', '3', '5']);
});

for (const variant of ['full', 'fixed', 'gomx', 'ranjbar']) {
  test(`walkthrough renders every checked-in scenario under ${variant}`, async ({ page, request }) => {
    test.setTimeout(120000);
    const response = await request.get('/api/scenarios');
    const scenarios = await response.json();
    for (const scenario of scenarios) {
      const errors: string[] = [];
      page.on('pageerror', error => errors.push(error.message));
      const run = await generate(page, titles[scenario.id], variant);
      for (const name of steps) {
        await explanation(page, name);
        await expect(page.locator('.step-content')).toBeVisible();
      }
      expect(errors).toEqual([]);
      for (const task of run.scenario.tasks) {
        for (const segment of executionSegments(run, task.id)) expect(BigInt(segment.end) > BigInt(segment.start)).toBe(true);
        for (const cycle of run.cycles) {
          const value = progress(task, cycle.states.find(state => state.task === task.id));
          expect(Number.isFinite(value)).toBe(true);
          expect(value).toBeGreaterThanOrEqual(0);
          expect(value).toBeLessThanOrEqual(100);
        }
      }
    }
  });
}
