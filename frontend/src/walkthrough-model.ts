import type { Channel, Cycle, Decision, Phase, Run, State, Task } from './domain';

export const steps = ['Observe inputs', 'Forecast & risk', 'Safety Gate', 'Choose a mode', 'Account resources', 'Task state'] as const;
export const phaseColors: Record<Phase, string> = { PENDING: '#e8ece8', RUNNING: '#dbe8f0', PAUSED: '#f2e7cf', COMPLETE: '#dceee3', REJECTED: '#f4dddd' };

// Ratios are used only for drawing. Engine integers remain exact strings.
export function ratio(value: string | bigint, total: string | bigint): number {
  const denominator = BigInt(total);
  if (denominator <= 0n) return 0;
  return Number(BigInt(value) * 10000n / denominator) / 100;
}
export const bounded = (value: number) => Math.max(0, Math.min(100, value));
export function channelValue(channel: Channel): string {
  if (channel.name === 'BATTERY_SOC_MV_PCT') return `${Number(channel.value) / 10}%`;
  if (channel.name.startsWith('TEMP_MC')) return `${(Number(channel.value) / 1000).toFixed(1)} °C`;
  if (channel.name === 'CPU_AVAIL_PCT') return `${channel.value}%`;
  if (channel.name === 'FAULT_FLAGS' || channel.name === 'SENSOR_AVAIL_MASK') return `0x${BigInt(channel.value).toString(16)}`;
  if (channel.name === 'COMM_WINDOW_OPEN') return channel.value === '0' ? 'Closed' : 'Open';
  if (channel.name.endsWith('_KB')) return `${channel.value} KB`;
  return channel.value;
}
export function changedChannels(cycle: Cycle, previous?: Cycle): Channel[] {
  if (!previous) return cycle.telemetry;
  return cycle.telemetry.filter(channel => {
    const before = previous.telemetry.find(item => item.name === channel.name);
    return !before || channel.value !== before.value || channel.quality !== before.quality || channel.timestamp !== before.timestamp;
  });
}
export function progress(task: Task, state?: State): number {
  if (!state || state.phase === 'PENDING' || state.phase === 'REJECTED') return 0;
  if (state.phase === 'COMPLETE') return 100;
  return bounded(ratio(BigInt(task.duration) - BigInt(state.remaining), task.duration));
}
export function cycleTitle(cycle: Cycle): string {
  if (cycle.result !== 'OK') return 'Cycle discarded';
  if (cycle.emergency) return 'Emergency Safe Mode';
  if (cycle.decisions.some(d => d.restoration)) return 'Task restored';
  if (cycle.decisions.some(d => d.before !== 'COMPLETE' && d.after === 'COMPLETE')) return 'Task completed';
  if (cycle.decisions.some(d => d.before !== d.after && d.after === 'PAUSED')) return 'Execution paused';
  if (cycle.trigger === 'FAULT') return 'Fault-triggered decision';
  if (cycle.decisions.some(d => d.before !== d.after && d.after === 'RUNNING')) return 'Task admitted';
  if (cycle.decisions.some(d => d.before !== d.after && d.after === 'REJECTED')) return 'Task rejected';
  if (cycle.decisions.some(d => d.selected === 'DEFERRED')) return 'Waiting for a safe window';
  return 'Commitments checked';
}
export function decisionStory(decision: Decision | undefined, task: Task | undefined, final: boolean, overrun: boolean): string {
  if (final) return 'The engine settled execution at scenario end. This is a final state snapshot, not another admission cycle.';
  if (overrun) return 'The engine discarded this cycle. No candidate decisions or tentative allocations are presented as committed results.';
  if (!decision || !task) return 'No decision record was emitted for the selected task at this cycle.';
  if (decision.restoration) return `Task ${task.id} passed admission again and resumed in ${decision.selected} with ${decision.remaining} sim ns remaining. Earlier progress is preserved.`;
  if (decision.after === 'COMPLETE') return `Task ${task.id} finished at t=${decision.completed}, delivering ${decision.value} / ${task.value} mission value. It does not execute again.`;
  if (decision.after === 'PAUSED' && decision.invalidation?.cause === 'PREDICTED_RISK') return `A forecast warned of future ${decision.invalidation.quantity} risk. Task ${task.id} is paused with ${decision.remaining} sim ns preserved; the recorded risk horizon is t=${decision.invalidation.horizon}.`;
  if (decision.after === 'PAUSED' && decision.invalidation) return `An observed ${decision.invalidation.quantity.toLowerCase().replaceAll('_', ' ')} condition invalidated task ${task.id}. Its remaining ${decision.remaining} sim ns is preserved.`;
  if (decision.reason === 'EMERGENCY_SAFE_MODE') return 'Emergency Safe Mode blocks new dispatch. Inspect each task to see its captured state and allocations.';
  if (decision.reason === 'RESTORE_BLOCKED_BUDGET') return 'Restoration is waiting for its held energy reservation to clear through a fresh SOC-budget update.';
  if (decision.reason === 'RESTORE_BLOCKED_GATE') return 'Restoration was considered, but no executable commitment was admitted.';
  if (decision.selected === 'DEFERRED') return `Task ${task.id} cannot execute under the current admission checks. It remains pending because a later execution window is still possible.`;
  if (decision.after === 'REJECTED') return `Task ${task.id} was rejected rather than admitted into an unavailable execution window.`;
  if (['FULL', 'THROTTLED', 'DEGRADED'].includes(decision.selected)) return `Task ${task.id} was admitted in ${decision.selected}. The engine allocated its resources and recorded ${decision.remaining} sim ns remaining.`;
  return `Task ${task.id} remains ${decision.after.toLowerCase()} with ${decision.remaining} sim ns remaining. No new admission is recorded.`;
}
export function eventIndices(run: Run): number[] {
  return run.cycles.flatMap((cycle, index) => {
    const previous = run.cycles[index - 1];
    const stateChange = cycle.decisions.some(d => d.before !== d.after || d.restoration || (d.invalidation && (!previous?.decisions.find(p => p.task === d.task)?.invalidation || previous.decisions.find(p => p.task === d.task)?.invalidation?.cause !== d.invalidation.cause)));
    return index === 0 || stateChange || cycle.trigger === 'FAULT' || cycle.result !== 'OK' || cycle.emergency !== previous?.emergency || changedChannels(cycle, previous).length ? [index] : [];
  });
}
export interface Segment { start: string; end: string; phase: Phase; cycle: number }
export function executionSegments(run: Run, taskId: string): Segment[] {
  const segments: Segment[] = [];
  run.cycles.forEach((cycle, index) => {
    const state = cycle.states.find(s => s.task === taskId);
    const next = run.cycles[index + 1];
    const end = next?.time ?? run.final.time;
    if (!state || BigInt(end) <= BigInt(cycle.time)) return;
    // Completion may occur between observations, including the final partial tick.
    const nextState = (next?.states ?? run.final.states).find(s => s.task === taskId);
    const completion = nextState?.phase === 'COMPLETE' ? nextState.completed : null;
    if (state.phase === 'RUNNING' && completion != null && BigInt(completion) > BigInt(cycle.time) && BigInt(completion) < BigInt(end)) {
      segments.push({ start: cycle.time, end: completion, phase: 'RUNNING', cycle: index }, { start: completion, end, phase: 'COMPLETE', cycle: index });
    } else segments.push({ start: cycle.time, end, phase: state.phase, cycle: index });
  });
  return segments;
}
export function remainingPoints(run: Run, task: Task): { time: string; remaining: string; phase: Phase }[] {
  const points = run.cycles.flatMap(cycle => {
    const state = cycle.states.find(s => s.task === task.id);
    return state ? [{ time: cycle.time, remaining: state.phase === 'PENDING' ? task.duration : state.remaining, phase: state.phase }] : [];
  });
  const final = run.final.states.find(s => s.task === task.id);
  if (final) {
    if (final.phase === 'COMPLETE' && final.completed && !points.some(p => p.time === final.completed)) points.push({ time: final.completed, remaining: '0', phase: 'COMPLETE' });
    points.push({ time: run.final.time, remaining: final.phase === 'PENDING' ? task.duration : final.remaining, phase: final.phase });
  }
  return points.sort((a, b) => BigInt(a.time) < BigInt(b.time) ? -1 : BigInt(a.time) > BigInt(b.time) ? 1 : 0);
}
