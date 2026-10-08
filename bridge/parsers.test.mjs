import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { parseDecision, parseCapture, parseMetrics, parseScenario } from './parsers.mjs';
const root = new URL('../', import.meta.url);
test('real goldens preserve candidate elimination and pause/restore/completion', async () => {
  const raw = await readFile(new URL('golden/false_alarm_restoration.decisions.expected', root), 'utf8');
  const records = raw.trim().split('\n').map(parseDecision);
  assert.equal(records.find(r => r.invalidation?.cause === 'PREDICTED_RISK').time, '10');
  assert.equal(records.find(r => r.restoration).remaining, '10');
  assert.equal(records.find(r => r.restoration).time, '24');
  assert.equal(records.find(r => r.after === 'COMPLETE').completed, '34');
});
test('64-bit timestamps and negative Ledger residuals remain exact', async () => {
  const raw = (await readFile(new URL('golden/low_soc_eclipse.decisions.expected', root), 'utf8')).split('\n')[0];
  const record = parseDecision(raw.replace('sim_ns=0', 'sim_ns=18446744073709551615').replace('CPU:100:0:0:100', 'CPU:10:0:30:-20'));
  assert.equal(record.time, '18446744073709551615');
  assert.equal(record.ledgerBefore[0].free, '-20');
  assert.equal(record.candidates[0].predicate, 'GATE_SOC_FLOOR');
  assert.equal(record.selected, 'DEGRADED');
});
test('incomplete cycles and discarded decisions fail rather than fabricate state', () => {
  assert.throws(() => parseCapture('CYCLE sequence=0 sim_ns=0 trigger=TICK result=OK emergency=0'));
  assert.throws(() => parseDecision('DECISION task=1 state_before=MADE_UP state_after=RUNNING'));
});
test('checked-in metrics preserve NA and censored recovery correctly', async () => {
  const rows = parseMetrics(await readFile(new URL('docs/phase5_metrics.csv', root), 'utf8'));
  assert.equal(rows.length, 32);
  assert.equal(rows[0].critical_completion_ratio, null);
  const fault = rows.find(r => r.scenario === 'sudden_fault' && r.variant === 'GOMX_STYLE');
  assert.equal(fault.recovery_latency_ns, '1');
  assert.equal(fault.recovery_latency_censored, '1');
});
test('scenario preview retains original units and all mode profiles', async () => {
  const scenario = parseScenario('compound_stress', await readFile(new URL('scenarios/compound_stress.scn', root), 'utf8'));
  assert.equal(scenario.tasks.length, 3);
  assert.equal(scenario.tasks[0].criticality, '0');
  assert.equal(scenario.tasks[0].profiles.length, 3);
  assert.equal(scenario.initial.length, 15);
});
