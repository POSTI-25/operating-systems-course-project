export type Page = 'lab' | 'mission' | 'configure' | 'inspect' | 'compare';
export type Variant = 'full' | 'fixed' | 'gomx' | 'ranjbar';
export type Phase = 'PENDING' | 'RUNNING' | 'PAUSED' | 'COMPLETE' | 'REJECTED';
export interface Profile { mode: string; energy: string; thermal: string; fraction: string; cpu: string; memory: string; storage: string; sensor: string; comm: string }
export interface Task { id: string; period: string; deadline: string; duration: string; criticality: string; value: string; subsystem: string; profiles: Profile[] }
export interface Scenario { id: string; name: string; duration: string; step: string; batteryCapacity: string; socFloor: string; tasks: Task[]; events: { type: string; time: string; detail: string }[]; initial: { channel: string; value: string; timestamp: string; quality: string }[]; raw: string }
export interface Channel { name: string; value: string; timestamp: string; quality: string }
export interface State { task: string; phase: Phase; remaining: string; completed: string | null; value: string }
export interface Ledger { dimension: string; capacity: string; reserve: string; committed: string; free: string }
export interface Candidate { mode: string; verdict: string; predicate: string; skipMask: string }
export interface Invalidation { task: string; cause: string; quantity: string; time: string; originalMode: string; horizon: string; clearCount: string; requiredClear: string; upgraded: boolean }
export interface Decision { task: string; tick: string; time: string; trigger: string; before: Phase; after: Phase; candidates: Candidate[]; selected: string; score: string | null; reason: string; ledgerBefore: Ledger[]; ledgerAfter: Ledger[]; invalidation: Invalidation | null; restoration: boolean; remaining: string; completed: string | null; value: string; raw: string }
export interface Cycle { sequence: string; time: string; trigger: string; result: string; emergency: boolean; telemetry: Channel[]; decisions: Decision[]; states: State[]; assumptions: { task: string; commit_ns: string; soc: string; horizon_ns: string; draw: string }[] }
export interface Run { id: string; createdAt: string; source: string; variant: Variant; scenario: Scenario; cycles: Cycle[]; final: { time: string; states: State[] }; provenance: { scenarioHash: string; executableHash: string; selfCost: string; cap: string; clock: string; protectedReserves: string }; raw: string }
export type Metric = Record<string, string | null>;
export const policy: Record<Variant, { name: string; description: string; modes: string; gate: string; prediction: string; fault: string }> = {
  full: { name: 'SAGE-RT full', description: 'All safety checks, graduated modes, Holt prediction and fault cycles.', modes: 'FULL · THROTTLED · DEGRADED · DEFERRED · REJECTED', gate: 'SOC · Thermal · Resources · Deadline', prediction: 'Holt α 0.3 / β 0.1', fault: 'Immediate fault cycle' },
  fixed: { name: 'Fixed / EDF', description: 'Class, deadline and ID ordering with FULL mode and unchecked admission.', modes: 'FULL', gate: 'Disabled', prediction: 'Current telemetry', fault: 'Regular ticks only' },
  gomx: { name: 'GomX style', description: 'Battery-oriented ablation with binary modes and ENERGY admission.', modes: 'FULL · DEFERRED', gate: 'SOC · Deadline', prediction: 'Current telemetry / solar harvest credit', fault: 'Regular ticks only' },
  ranjbar: { name: 'Ranjbar style', description: 'Fault and thermal ablation with non-energy resource admission.', modes: 'FULL · THROTTLED · DEGRADED · DEFERRED · REJECTED', gate: 'Thermal · Resources · Deadline', prediction: 'Current telemetry', fault: 'Immediate fault cycle' },
};
export const titles: Record<string, string> = { normal_orbit: 'Normal orbit', low_soc_eclipse: 'Low-SOC eclipse', high_thermal_load: 'High thermal load', sudden_fault: 'Sudden fault', communication_blackout: 'Communication blackout', competing_payload_tasks: 'Competing payloads', compound_stress: 'Compound stress', false_alarm_restoration: 'False-alarm restoration', prediction_trend: 'Prediction trend' };
export const descriptions: Record<string, string> = { normal_orbit: 'A stable reference for admission and task completion.', low_soc_eclipse: 'Eliminate unsafe modes and complete useful work at lower fidelity.', high_thermal_load: 'Inspect thermal gating and the surviving throttled mode.', sudden_fault: 'Observe a fault-triggered cycle between regular ticks.', communication_blackout: 'Inspect communication feasibility independently of battery safety.', competing_payload_tasks: 'Trace competing resource demands and sequential admission.', compound_stress: 'Inspect critical-task invalidation, capacity shedding and emergency mode.', false_alarm_restoration: 'Follow a predicted-risk pause, Gate-approved restoration and completion.', prediction_trend: 'Additional trend fixture used by the prediction A/B.' };
export const number = (value: string | null | undefined) => value == null ? '—' : /^-?\d+$/.test(value) ? BigInt(value).toLocaleString('en-IN') : value;
export const label = (value: string) => value.replaceAll('_', ' ').toLowerCase();
export function significant(cycle: Cycle) { return cycle.emergency || cycle.trigger === 'FAULT' || cycle.decisions.some(d => d.before !== d.after || d.restoration || d.invalidation); }
export function download(name: string, value: string, type = 'application/json') {
  const url = URL.createObjectURL(new Blob([value], { type }));
  const a = document.createElement('a'); a.href = url; a.download = name; a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
