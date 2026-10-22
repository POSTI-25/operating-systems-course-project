import type { Candidate, Cycle, Decision, Ledger, Run, State } from './domain';
import { channelValue, cycleTitle } from './walkthrough-model';

export const labStages = [
  { name: 'Read the mission', short: 'Inputs', title: 'A mission starts with jobs and limits.', intro: 'The scenario supplies sensor readings, timed changes, job requirements and resource profiles.', concept: 'Read it like a recipe: conditions describe the spacecraft; tasks describe the work it wants to do.' },
  { name: 'Observe conditions', short: 'Observe', title: 'What has changed on the spacecraft?', intro: 'Move through recorded sensor frames. Changed readings are highlighted.', concept: 'The scheduler needs current information before it can make a safe promise.' },
  { name: 'Build the options', short: 'Modes', title: 'One job. Several ways to do it.', intro: 'Reveal the configured FULL, THROTTLED and DEGRADED profiles one at a time.', concept: 'A lower-output mode can ask for fewer resources and earn less value. It does not necessarily take less time.' },
  { name: 'Check what fits', short: 'Safety', title: 'Unsafe options leave the queue.', intro: 'Step through the C engine’s candidate verdicts. Only surviving executing modes can reach ranking.', concept: 'The safety check is a door: a rejected option cannot win just because its output would be valuable.' },
  { name: 'Choose and allocate', short: 'Allocate', title: 'Choose an option. Reserve its resources.', intro: 'Reveal the selected outcome and the resource changes recorded for this decision.', concept: 'Allocation is putting a name on a share of the supplies, so another job cannot claim that same share.' },
  { name: 'Watch jobs execute', short: 'Execute', title: 'Follow the jobs, not just the report.', intro: 'Move through chronological decisions and see jobs change state and retain their completed work.', concept: 'Waiting has not started. Working makes progress. Paused keeps its place. Finished earns its completion value.' },
  { name: 'React and recover', short: 'Recheck', title: 'A promise can stop being safe.', intro: 'Follow recorded pauses, fault responses, recovery checks and restoration.', concept: 'If conditions change, reconsider the job. Resuming continues the unfinished part rather than starting over.' },
  { name: 'Read the result', short: 'Result', title: 'What did this mission accomplish?', intro: 'Inspect each job’s final recorded state and delivered mission value.', concept: 'The final state says what actually finished within this scenario, not what might finish if time were extended.' },
] as const;

export interface LabItem {
  id: string;
  title: string;
  text: string;
  cycle?: number;
  task?: string;
  line?: number;
  mode?: string;
  candidate?: Candidate;
  dimension?: string;
  kind: 'source' | 'frame' | 'profile' | 'candidate' | 'selection' | 'resource' | 'execution' | 'risk' | 'final';
}
export const phaseName: Record<string, string> = { PENDING: 'Waiting', RUNNING: 'Working', PAUSED: 'Paused', COMPLETE: 'Finished', REJECTED: 'Rejected' };
export const gateReason: Record<string, string> = { GATE_SOC_FLOOR: 'Battery safety check failed', GATE_THERMAL_CEILING: 'Thermal safety check failed', GATE_PROTECTED_RESIDUAL: 'Resource reservation does not fit', GATE_DEADLINE: 'Deadline check failed', GATE_INVALID_INPUT: 'Input is not valid' };
export const runsMode = (mode: string) => ['FULL', 'THROTTLED', 'DEGRADED'].includes(mode);
export function decisionAt(run: Run, item?: LabItem): Decision | undefined { return item?.cycle == null ? undefined : run.cycles[item.cycle]?.decisions.find(d => d.task === item.task); }
export function ledgerAt(decision: Decision | undefined, dimension: string, after = false): Ledger | undefined { return (after ? decision?.ledgerAfter : decision?.ledgerBefore)?.find(e => e.dimension === dimension); }

export function sourceLine(run: Run, pattern: string): number | undefined {
  const index = run.scenario.raw.split(/\r?\n/).findIndex(line => line === pattern || line.startsWith(pattern + ' '));
  return index < 0 ? undefined : index + 1;
}
function sourceCaption(line: string) {
  const [key, ...parts] = line.split(/\s+/);
  if (key === 'SCENARIO') return 'Name this experiment';
  if (key === 'DURATION_NS') return `Stop the scenario at t=${parts[0]}`;
  if (key === 'STEP_NS') return `Check every ${parts[0]} simulated ns`;
  if (key === 'BATTERY_CAPACITY_MJ') return `Define total battery capacity: ${parts[0]} mJ`;
  if (key === 'SOC_FLOOR_PERMILLE') return `Keep battery charge above ${Number(parts[0]) / 10}%`;
  if (key === 'TASK') return `Define job T${parts[0]}`;
  if (key === 'TASK_MODE') return `Define T${parts[0]}'s ${parts[1]} option`;
  if (key === 'UPDATE') return `Change an input at t=${parts[0]}`;
  if (key === 'FAULT') return `Change fault bits at t=${parts[0]}`;
  if (key === 'ORBIT') return 'Define the orbit and eclipse schedule';
  return `Set ${parts[0] === 'TEMP_MC' ? `temperature channel ${parts[1]}` : parts[0] ?? key}`;
}

export function buildLabItems(run: Run): LabItem[][] {
  const stages: LabItem[][] = Array.from({ length: labStages.length }, () => []);
  run.scenario.raw.split(/\r?\n/).forEach((line, index) => {
    if (!line.trim() || line.trim().startsWith('#')) return;
    stages[0].push({ id: `line-${index}`, kind: 'source', title: sourceCaption(line), text: line, line: index + 1 });
  });
  run.scenario.tasks.forEach(task => task.profiles.forEach(profile => {
    stages[2].push({ id: `profile-${task.id}-${profile.mode}`, kind: 'profile', task: task.id, mode: profile.mode, title: `T${task.id} · ${profile.mode}`, text: `${profile.energy} mJ energy, ${profile.cpu}% CPU; ${BigInt(task.value) * BigInt(profile.fraction) / 1000n} value if completed.`, line: sourceLine(run, `TASK_MODE ${task.id} ${profile.mode}`) });
  }));
  run.cycles.forEach((cycle, index) => {
    const changed = cycle.telemetry.filter(c => {
      const previous = run.cycles[index - 1]?.telemetry.find(p => p.name === c.name);
      return !previous || previous.value !== c.value || previous.timestamp !== c.timestamp || previous.quality !== c.quality;
    });
    stages[1].push({ id: `frame-${cycle.sequence}`, kind: 'frame', cycle: index, title: `t=${cycle.time} · ${cycle.trigger === 'FAULT' ? 'fault cycle' : 'sensor frame'}`, text: index === 0 ? `${cycle.telemetry.length} initial channels observed.` : changed.length ? changed.slice(0, 3).map(c => `${c.name}: ${channelValue(c)}`).join(' · ') : 'No input changed; the engine still checks the plan.' });
    cycle.decisions.forEach(d => {
      const base = { cycle: index, task: d.task };
      d.candidates.forEach(candidate => stages[3].push({ ...base, id: `gate-${cycle.sequence}-${d.task}-${candidate.mode}`, kind: 'candidate', candidate, mode: candidate.mode, title: `T${d.task} · ${candidate.mode}`, text: candidate.verdict === 'PASS' ? 'This option survives the enabled safety checks.' : candidate.verdict === 'ELIMINATED' ? gateReason[candidate.predicate] ?? candidate.predicate : candidate.verdict === 'FALLBACK_ONLY' ? 'Wait/reject fallback; this is not an executing mode.' : 'This option is disabled by the selected policy.', line: sourceLine(run, `TASK_MODE ${d.task} ${candidate.mode}`) ?? sourceLine(run, `TASK ${d.task}`) }));
      const changes = d.ledgerAfter.filter(a => {
        const before = d.ledgerBefore.find(b => b.dimension === a.dimension);
        return before && (before.committed !== a.committed || before.free !== a.free || before.reserve !== a.reserve || before.capacity !== a.capacity);
      });
      if (d.selected !== 'NONE' || changes.length || d.restoration) {
        stages[4].push({ ...base, id: `select-${cycle.sequence}-${d.task}`, kind: 'selection', mode: d.selected, title: runsMode(d.selected) ? `Choose ${d.selected} for T${d.task}` : d.selected === 'DEFERRED' ? `T${d.task} waits` : d.selected === 'REJECTED' ? `T${d.task} is not admitted` : `Update T${d.task}'s reservations`, text: runsMode(d.selected) ? 'The engine chose this executing mode. Next, reveal the recorded reservations.' : 'No new executing mode is chosen here.', line: sourceLine(run, `TASK_MODE ${d.task} ${d.selected}`) ?? sourceLine(run, `TASK ${d.task}`) });
        changes.forEach(change => stages[4].push({ ...base, id: `resource-${cycle.sequence}-${d.task}-${change.dimension}`, kind: 'resource', dimension: change.dimension, title: `Reveal ${change.dimension.toLowerCase()} accounting`, text: `Committed ${d.ledgerBefore.find(b => b.dimension === change.dimension)!.committed} → ${change.committed}; free ${d.ledgerBefore.find(b => b.dimension === change.dimension)!.free} → ${change.free}.`, line: sourceLine(run, `TASK_MODE ${d.task} ${d.selected}`) ?? sourceLine(run, `TASK ${d.task}`) }));
      }
      stages[5].push({ ...base, id: `execute-${cycle.sequence}-${d.task}`, kind: 'execution', title: `T${d.task}: ${phaseName[d.before]} → ${phaseName[d.after]}`, text: d.restoration ? `Resume with ${d.remaining} sim ns still to do. Earlier progress is kept.` : d.after === 'COMPLETE' ? `Completed at t=${d.completed}; ${d.value} mission value delivered.` : d.after === 'PAUSED' ? `${d.remaining} sim ns remain. Paused time does not advance this job.` : d.after === 'PENDING' ? 'The job has not started. It still needs an admissible execution window.' : `${d.remaining} sim ns remain. ${d.selected !== 'NONE' ? `Outcome: ${d.selected}.` : 'No new mode selected.'}`, line: sourceLine(run, `TASK ${d.task}`) });
      if (d.invalidation || d.restoration || cycle.emergency) stages[6].push({ ...base, id: `risk-${cycle.sequence}-${d.task}`, kind: 'risk', title: d.restoration ? `Restore T${d.task}` : cycle.emergency ? 'Emergency Safe Mode' : d.invalidation?.cause === 'PREDICTED_RISK' ? `T${d.task}: future risk` : `Recheck T${d.task}`, text: d.restoration ? `The job passed admission again and resumes its remaining ${d.remaining} sim ns.` : d.invalidation ? `${d.invalidation.cause}: ${d.invalidation.quantity}. Recorded horizon t=${d.invalidation.horizon}; state ${phaseName[d.after].toLowerCase()}.` : 'Essential work could not keep a safe commitment; new dispatch is blocked.', line: sourceLine(run, `TASK ${d.task}`) });
    });
  });
  run.final.states.forEach(state => stages[7].push({ id: `final-${state.task}`, task: state.task, kind: 'final', title: `T${state.task} · ${phaseName[state.phase]}`, text: `${state.value} value delivered; ${state.remaining} sim ns remain at scenario end t=${run.final.time}.`, line: sourceLine(run, `TASK ${state.task}`) }));
  return stages;
}

export function itemSourceLines(run: Run, item: LabItem | undefined): number[] {
  if (!item) return [];
  if (item.line != null) return [item.line];
  if (item.cycle != null) {
    const cycle = run.cycles[item.cycle];
    const lines = run.scenario.raw.split(/\r?\n/);
    if (item.kind === 'frame') return lines.flatMap((line, index) => /^(UPDATE|FAULT) /.test(line) && line.split(/\s+/)[1] === cycle.time || item.cycle === 0 && line.startsWith('INITIAL ') ? [index + 1] : []);
  }
  return [];
}

export function labStates(run: Run, item: LabItem | undefined, stage: number): State[] {
  if (stage === 7) return run.final.states;
  if (stage < 3) return run.scenario.tasks.map(t => ({ task: t.id, phase: 'PENDING', remaining: t.duration, completed: null, value: '0' }));
  if (item?.cycle == null) return [];
  const cycle = run.cycles[item.cycle];
  if (stage === 3 || stage === 4 && item.kind === 'selection') return run.cycles[item.cycle - 1]?.states ?? run.scenario.tasks.map(t => ({ task: t.id, phase: 'PENDING', remaining: t.duration, completed: null, value: '0' }));
  return cycle.states;
}
export function labCycleLabel(cycle: Cycle | undefined, stage: number, run: Run) { return stage === 7 ? `Final · t=${run.final.time}` : cycle ? `t=${cycle.time} · ${cycleTitle(cycle)}` : 'Scenario inputs · execution not shown yet'; }
