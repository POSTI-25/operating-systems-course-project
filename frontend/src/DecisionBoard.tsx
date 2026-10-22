import { useEffect, useState } from 'react';
import type { Cycle, Decision, Ledger, Profile, Run, State, Task } from './domain';
import { label, number, policy } from './domain';
import { bounded, channelValue, progress, ratio } from './walkthrough-model';
import './whiteboard.css';

interface Props {
  run: Run;
  cursor: number;
  taskId: string;
  select: (id: string) => void;
  move: (index: number) => void;
  step: number;
  playing: boolean;
}
const modes = ['FULL', 'THROTTLED', 'DEGRADED'];
const questions = [
  'What does the job need, and what do we have?',
  'Is it safe to keep going?',
  'Which ways of doing the job survive the checks?',
  'Which safe option did the scheduler choose?',
  'What gets reserved, and what is left for other jobs?',
  'Who works, who waits, and what happens to unfinished work?',
];
const modeNames: Record<string, string> = { FULL: 'Full output', THROTTLED: 'Reduced output', DEGRADED: 'Minimum useful output' };
const phaseNames: Record<string, string> = { PENDING: 'Waiting', RUNNING: 'Working', PAUSED: 'Paused', COMPLETE: 'Finished', REJECTED: 'Not admitted' };
const gateNames: Record<string, string> = {
  GATE_SOC_FLOOR: 'Battery safety limit',
  GATE_THERMAL_CEILING: 'Too much heat',
  GATE_PROTECTED_RESIDUAL: 'Insufficient resource headroom',
  GATE_DEADLINE: 'Cannot finish before the deadline',
  GATE_INVALID_INPUT: 'Invalid input',
};
const resourceNames: Record<string, string> = { CPU: 'CPU', ENERGY: 'Energy budget', MEMORY: 'Memory', STORAGE: 'Storage', COMM: 'Communication' };
const units: Record<string, string> = { CPU: '%', ENERGY: 'mJ', MEMORY: 'KB', STORAGE: 'KB', COMM: '%' };
const executing = (mode: string) => modes.includes(mode);
function entry(decision: Decision | undefined, dimension: string, after = false) {
  return (after ? decision?.ledgerAfter : decision?.ledgerBefore)?.find(e => e.dimension === dimension);
}
function valueOf(task: Task, profile: Profile) { return (BigInt(task.value) * BigInt(profile.fraction) / 1000n).toString(); }
function max(...values: (string | bigint)[]) { return values.map(BigInt).reduce((a, b) => a > b ? a : b, 1n); }
function width(value: string | bigint, scale: string | bigint) { return `${bounded(ratio(value, scale))}%`; }

// Preserve equal-time cycles. A fault at a tick is a distinct decision, not another elapsed interval.
export function jobMoments(run: Run, taskId: string) {
  return run.cycles.flatMap((cycle, index) => {
    const state = cycle.states.find(s => s.task === taskId);
    const previous = run.cycles[index - 1]?.states.find(s => s.task === taskId);
    const d = cycle.decisions.find(d => d.task === taskId);
    return index === 0 || state?.phase !== previous?.phase || d?.restoration || cycle.trigger === 'FAULT' || cycle.result !== 'OK' ? [index] : [];
  });
}

export function DecisionBoard({ run, cursor, taskId, select, move, step, playing }: Props) {
  const cycle = run.cycles[cursor];
  const task = run.scenario.tasks.find(t => t.id === taskId)!;
  const decision = cycle?.decisions.find(d => d.task === taskId);
  const state = (cycle?.states ?? run.final.states).find(s => s.task === taskId);
  return <section className="decision-whiteboard" aria-label="Scheduling whiteboard">
    <div className="wb-title"><span className="wb-marker">{step + 1}</span><div><span className="wb-small">FOLLOW ONE JOB: T{taskId} · {task.subsystem}</span><h3>{questions[step]}</h3></div></div>
    {cycle?.emergency && <div className="wb-emergency"><strong>Emergency Safe Mode</strong><span>Essential work could not keep a safe commitment. The governor blocks new dispatch.</span></div>}
    <div className="wb-drawing" key={`${run.id}-${cursor}-${taskId}-${step}`}>
      {!cycle ? <div className="wb-end"><strong>The scenario has ended.</strong><WorkStrip task={task} state={state} /><p>This is the final recorded state. There is no new sensor reading or allocation decision.</p></div> : cycle.result !== 'OK' ? <div className="wb-end"><strong>Draft plan discarded</strong><div className="wb-discard">Tentative changes <span>×</span> Published plan</div><p>The cycle exceeded its CPU budget. No draft allocation is shown as committed.</p></div> : <>
        {step === 0 && <Situation run={run} cycle={cycle} task={task} decision={decision} />}
        {step === 1 && <RiskDrawing run={run} cycle={cycle} task={task} decision={decision} state={state} />}
        {step === 2 && <GateDrawing task={task} decision={decision} cycle={cycle} />}
        {step === 3 && <ChoiceDrawing run={run} cursor={cursor} task={task} decision={decision} state={state} />}
        {step === 4 && <AllocationDrawing task={task} decision={decision} playing={playing} />}
        {step === 5 && <ScheduleDrawing run={run} cursor={cursor} task={task} decision={decision} state={state} select={select} />}
      </>}
    </div>
    <div className="wb-moments"><span className="wb-small">FOLLOW THIS JOB THROUGH TIME</span><div>{jobMoments(run, taskId).map(i => {
      const c = run.cycles[i];
      const s = c.states.find(s => s.task === taskId);
      const d = c.decisions.find(d => d.task === taskId);
      return <button key={c.sequence} onClick={() => move(i)} aria-pressed={cursor === i}><span>t={c.time}{c.trigger === 'FAULT' ? ' · fault' : ''}</span><strong>{d?.restoration ? 'Resumes' : s ? phaseNames[s.phase] : 'Check'}</strong></button>;
    })}<button aria-pressed={cursor === run.cycles.length} onClick={() => move(run.cycles.length)}><span>t={run.final.time}</span><strong>Final state</strong></button></div></div>
    <p className="wb-caption">A drawing of recorded C-engine decisions. Next explanation reveals another part of the same cycle; next cycle moves simulated time.</p>
  </section>;
}

function Situation({ run, cycle, task, decision }: { run: Run; cycle: Cycle; task: Task; decision?: Decision }) {
  const soc = cycle.telemetry.find(c => c.name === 'BATTERY_SOC_MV_PCT');
  const cpu = cycle.telemetry.find(c => c.name === 'CPU_AVAIL_PCT');
  const temp = cycle.telemetry.find(c => c.name === `TEMP_MC[${['BATTERY', 'BUS', 'PAYLOAD', 'ADCS'].indexOf(task.subsystem)}]`);
  const comm = cycle.telemetry.find(c => c.name === 'COMM_WINDOW_OPEN');
  const energy = entry(decision, 'ENERGY');
  const cpuBudget = entry(decision, 'CPU');
  const full = task.profiles.find(p => p.mode === 'FULL');
  const observedMargin = soc && soc.quality === 'OK' && BigInt(soc.value) > BigInt(run.scenario.socFloor) ? BigInt(soc.value) - BigInt(run.scenario.socFloor) : 0n;
  const observedHeadroom = BigInt(run.scenario.batteryCapacity) * observedMargin / 1000n;
  return <>
    <div className="wb-situation"><div className="wb-job"><span className="wb-small">A JOB TO DO</span><strong>T{task.id}</strong><span>{task.subsystem.toLowerCase()} work</span><p>Needs {task.duration} sim ns of work.<br />Must finish by t={task.deadline}.</p>{full && <div className="wb-job-request"><span>In FULL mode it asks for</span><b>{number(full.energy)} mJ</b><b>{full.cpu}% CPU</b></div>}</div><span className="wb-big-arrow" aria-hidden="true">→</span><div className="wb-supply"><span className="wb-small">THE SPACECRAFT’S LIMITS</span><div className="wb-supply-line"><span>Battery charge</span><strong>{soc ? channelValue(soc) : 'Not emitted'}</strong></div><div className="wb-battery" role="img" aria-label={`Observed battery ${soc ? channelValue(soc) : 'not emitted'}, safety floor ${Number(run.scenario.socFloor) / 10} percent`}><div className="wb-battery-fill" style={{ width: soc ? width(soc.value, '1000') : '0%' }} /><div className="wb-floor" style={{ left: width(run.scenario.socFloor, '1000') }} /><span>Keep at least {Number(run.scenario.socFloor) / 10}%</span></div><div className="wb-supply-line"><span>CPU currently available</span><strong>{cpu ? channelValue(cpu) : 'Not emitted'}</strong></div><div className="wb-seat-row" role="img" aria-label={`Observed available CPU ${cpu?.value ?? 'not emitted'} percent; each block represents 10 percent`}>{Array.from({ length: 10 }, (_, i) => <span key={i}><i style={{ width: cpu ? `${bounded((Number(cpu.value) - i * 10) * 10)}%` : '0%' }} /></span>)}</div><p className="wb-unit-note">10 blocks = 100% CPU capacity; these are capacity units, not 10 CPU cores.</p><div className="wb-other-observations"><span>{temp ? channelValue(temp) : '—'}<small>{task.subsystem.toLowerCase()} temperature</small></span><span>{comm ? channelValue(comm) : '—'}<small>communication window</small></span></div></div></div>
    {soc?.quality === 'OK' && energy && observedHeadroom === BigInt(energy.capacity) && observedMargin > 0n && <div className="wb-energy-explanation"><span>Why this energy budget?</span><strong>{Number(soc.value) / 10}% charge − {Number(run.scenario.socFloor) / 10}% kept safe = {Number(observedMargin) / 10}% above the floor</strong><p>{Number(observedMargin) / 10}% of {number(run.scenario.batteryCapacity)} mJ = <b>{number(observedHeadroom.toString())} mJ</b> above the safety floor.</p></div>}
    <div className="wb-budget-note"><span>Already-held resources matter too.</span><strong>{energy ? `${number(energy.free)} mJ free` : 'No energy Ledger emitted'}</strong><strong>{cpuBudget ? `${number(cpuBudget.free)}% CPU free` : 'No CPU Ledger emitted'}</strong><small>From the Ledger before this job’s decision; other jobs may already have reservations.</small></div>
    <p className="wb-takeaway">Scheduling means choosing which jobs can work, when they can work, and how much of each resource to reserve.</p>
  </>;
}

function ProfileBars({ task, decision, cycle, selected = false }: { task: Task; decision?: Decision; cycle?: Cycle; selected?: boolean }) {
  const profiles = task.profiles.filter(p => modes.includes(p.mode));
  const energyFree = entry(decision, 'ENERGY')?.free;
  const cpuFree = entry(decision, 'CPU')?.free;
  const commFree = entry(decision, 'COMM')?.free;
  const temperature = cycle?.telemetry.find(c => c.name === `TEMP_MC[${['BATTERY', 'BUS', 'PAYLOAD', 'ADCS'].indexOf(task.subsystem)}]`);
  const heat = !!temperature && decision?.candidates.some(c => c.predicate === 'GATE_THERMAL_CEILING');
  const cpu = !heat && cpuFree != null && profiles.some(p => BigInt(p.cpu) > BigInt(cpuFree));
  const comm = !heat && !cpu && commFree != null && profiles.some(p => BigInt(p.comm) > BigInt(commFree));
  const free = heat ? (100000n - BigInt(temperature!.value)).toString() : cpu ? cpuFree : comm ? commFree : energyFree;
  const request = (p: Profile) => heat ? p.thermal : cpu ? p.cpu : comm ? p.comm : p.energy;
  const unit = heat ? '°C' : cpu || comm ? '%' : 'mJ';
  const amount = (value: string) => heat ? String(Number(value) / 1000) : number(value);
  const name = heat ? 'Added heat' : cpu ? 'CPU request' : comm ? 'Communication request' : 'Energy request';
  const scale = max(...profiles.map(request), free ?? '0');
  return <><div className="wb-free-budget"><span>{heat ? 'Observed thermal margin' : cpu ? 'CPU still free' : comm ? 'Communication still free' : 'Energy still free'}</span><strong>{free == null ? 'Not emitted' : `${amount(free)} ${unit}`}</strong><small>{heat ? '100°C ceiling minus the observed temperature; C checks use their own effective forecast.' : 'Before this job’s decision. Every request below uses the same scale.'}</small></div><div className="wb-profiles">{profiles.map(profile => {
    const candidate = decision?.candidates.find(c => c.mode === profile.mode);
    const chosen = selected && decision?.selected === profile.mode;
    const needed = request(profile);
    const comparison = free == null ? '' : BigInt(needed) > BigInt(free) ? '>' : '≤';
    const chartLabel = heat ? `${profile.mode} adds ${amount(needed)} degrees Celsius; observed thermal margin ${free == null ? 'not emitted' : amount(free)} degrees` : cpu ? `${profile.mode} requests ${profile.cpu} percent CPU; captured free CPU ${cpuFree ?? 'not emitted'} percent` : comm ? `${profile.mode} requests ${profile.comm} percent communication; captured free communication ${commFree ?? 'not emitted'} percent` : `${profile.mode} requests ${profile.energy} mJ; captured free energy ${energyFree ?? 'not emitted'} mJ`;
    return <article key={profile.mode} className={`wb-profile ${chosen ? 'wb-chosen' : ''} ${candidate?.verdict === 'ELIMINATED' ? 'wb-failed' : ''}`}>
      <div className="wb-profile-heading"><strong>{profile.mode}</strong><span>{chosen ? '← chosen' : modeNames[profile.mode]}</span></div>
      <div className="wb-profile-compare"><span>{name}</span><strong>{amount(needed)} {unit}</strong></div>
      <div className="wb-request-track" role="img" aria-label={chartLabel}><span style={{ width: width(needed, scale) }} />{free != null && BigInt(free) >= 0n && <i style={{ left: width(free, scale) }} />}</div>
      {free != null && <p className="wb-compare-equation">{amount(needed)} <b>{comparison}</b> {amount(free)} {unit}<small>request compared with {heat ? 'observed margin' : 'free headroom'}</small></p>}
      <div className="wb-mode-extras"><span>{cpu || heat || comm ? `${number(profile.energy)} mJ energy · ` : ''}{profile.cpu}% CPU{cpu ? ' requested' : ''}</span></div>
      <div className="wb-profile-reward"><strong>{number(valueOf(task, profile))}</strong><span>value if completed</span></div>
      {candidate && <div className={`wb-verdict verdict-${candidate.verdict.toLowerCase()}`}><span>{candidate.verdict === 'PASS' ? '✓' : candidate.verdict === 'ELIMINATED' ? '×' : '—'}</span><strong>{candidate.verdict === 'PASS' ? 'Survives checks' : candidate.verdict === 'ELIMINATED' ? gateNames[candidate.predicate] ?? label(candidate.predicate) : 'Disabled by this policy'}</strong></div>}
    </article>;
  })}</div></>;
}

function GateDrawing({ task, decision, cycle }: { task: Task; decision?: Decision; cycle: Cycle }) {
  if (!decision?.candidates.length) return <NoNewDecision decision={decision} />;
  const thermalFailure = decision.candidates.some(c => c.predicate === 'GATE_THERMAL_CEILING');
  const temp = cycle.telemetry.find(c => c.name === `TEMP_MC[${['BATTERY', 'BUS', 'PAYLOAD', 'ADCS'].indexOf(task.subsystem)}]`);
  const comm = cycle.telemetry.find(c => c.name === 'COMM_WINDOW_OPEN');
  return <><div className="wb-check-heading"><span>Same job</span><b>→</b><span>3 resource profiles</span><b>→</b><span>Enabled safety checks</span></div>{(thermalFailure || comm?.value === '0') && <div className="wb-constraint-note">{thermalFailure && <span>Observed {task.subsystem.toLowerCase()} temperature: <strong>{temp ? channelValue(temp) : 'not emitted'}</strong>. The reference runner sets a <strong>100°C</strong> ceiling.</span>}{comm?.value === '0' && <span>The communication window is <strong>closed</strong>. Profiles can request different amounts of communication capacity.</span>}</div>}<ProfileBars task={task} decision={decision} cycle={cycle} /><div className="wb-line-key"><i /> Red line = the available headroom. Blue bar = the mode’s request.</div><p className="wb-takeaway">A smaller mode uses a different resource profile and delivers less value. That is what throttling means in this simulation.</p><details className="wb-accuracy-note"><summary>How to read these comparisons</summary><p>The drawings compare requests with observed or Ledger headroom. The C verdict remains authoritative: it also uses enabled forecasts, heat, other resources and deadlines. Only the first failure is exported. CPU demand need not decrease in every fixture. A mode change does not imply a shorter task duration or a hardware clock-frequency change.</p></details></>;
}

function ChoiceDrawing({ run, cursor, task, decision, state }: { run: Run; cursor: number; task: Task; decision?: Decision; state?: State }) {
  if (!decision?.candidates.length) return <NoNewDecision decision={decision} />;
  const survivors = decision.candidates.filter(c => c.verdict === 'PASS');
  const selected = decision.selected;
  const profile = task.profiles.find(p => p.mode === selected);
  if (!executing(selected)) {
    const lastMode = run.cycles.slice(0, cursor + 1).flatMap(c => c.decisions).filter(d => d.task === task.id && executing(d.selected)).at(-1)?.selected;
    return <><div className="wb-fallback"><div><span className="wb-small">EXECUTING OPTIONS THAT PASSED</span><strong>{survivors.length ? survivors.map(c => c.mode).join(' / ') : 'None'}</strong></div><span className="wb-big-arrow">→</span><div><span className="wb-small">RECORDED OUTCOME</span><strong>{state ? phaseNames[state.phase] : label(selected)}</strong><span>{state?.phase === 'PAUSED' ? 'Keep unfinished work. Recheck before resuming.' : selected === 'DEFERRED' ? 'Do not allocate a new execution mode yet.' : selected === 'REJECTED' ? 'No admissible future execution window.' : lastMode && state?.phase === 'RUNNING' ? `Existing ${lastMode} commitment continues.` : 'No new executing selection.'}</span></div></div><p className="wb-takeaway">{state?.phase === 'PAUSED' ? 'Passing a candidate check does not override a revalidation pause.' : 'Choosing to wait is also a scheduling decision.'}</p></>;
  }
  return <><ProfileBars task={task} decision={decision} cycle={run.cycles[cursor]} selected /><div className="wb-choice-result"><span className="wb-big-arrow">↓</span><div><span className="wb-small">ADMIT T{task.id} IN {selected}</span><strong>{profile ? `${number(profile.energy)} mJ + ${profile.cpu}% CPU` : selected}</strong><p>{profile ? `It can deliver ${number(valueOf(task, profile))} value on completion.` : ''}</p></div></div><p className="wb-takeaway">Safety filters first. Utility then ranks surviving modes for this job.</p><p className="wb-unit-note">{survivors.length === 1 ? 'Only one executing mode survived this evaluation.' : 'More than one mode may be safe; safe does not mean selected.'} Selected Utility = {decision.score ?? 'not emitted'}; it is a ranking score, not a percentage.</p></>;
}

function NoNewDecision({ decision }: { decision?: Decision }) {
  return <div className="wb-end"><strong>No new mode evaluation here.</strong><p>{decision?.after === 'COMPLETE' ? 'The job has finished. It does not need another mode or allocation.' : 'The capture did not emit a new candidate list for this job in this cycle. Existing work may continue without a new admission.'}</p>{decision && <div className="wb-state-change"><span>{phaseNames[decision.before]}</span><b>→</b><span>{phaseNames[decision.after]}</span></div>}</div>;
}

function RiskDrawing({ run, cycle, task, decision, state }: { run: Run; cycle: Cycle; task: Task; decision?: Decision; state?: State }) {
  const soc = cycle.telemetry.find(c => c.name === 'BATTERY_SOC_MV_PCT');
  const assumption = cycle.assumptions.find(a => a.task === task.id);
  const risk = decision?.invalidation;
  const futureRisk = risk?.cause === 'PREDICTED_RISK';
  return <><div className="wb-risk-path"><div><span className="wb-small">WHAT IS OBSERVED NOW</span><strong>{soc ? channelValue(soc) : 'Not emitted'}</strong><span>Battery must stay above {Number(run.scenario.socFloor) / 10}%</span></div><b className="wb-big-arrow">→</b><div className="wb-risk-question"><span className="wb-small">ASK ABOUT THE REMAINING WORK</span><strong>{futureRisk ? 'Future risk recorded' : decision?.restoration ? 'Admission passed again' : risk ? 'Observed condition invalidated the plan' : 'No invalidation recorded'}</strong><span>{run.variant === 'full' ? 'Use the trend, not just the latest reading.' : `${policy[run.variant].name}: ${policy[run.variant].prediction}`}</span></div><b className="wb-big-arrow">→</b><div className={`wb-risk-action ${state?.phase === 'PAUSED' ? 'wb-risk-pause' : ''}`}><span className="wb-small">RECORDED JOB STATE</span><strong>{state ? phaseNames[state.phase] : 'Not emitted'}</strong><span>{decision?.restoration ? 'Resume remaining work' : futureRisk ? 'A prediction is a warning, not an observed crash.' : risk ? label(risk.quantity) : 'Do not invent a risk that was not logged.'}</span></div></div>
    {risk && <div className="wb-risk-horizon"><span>Risk recorded at t={risk.time}</span><div><i /><span>{futureRisk ? 'Forecast horizon' : 'Recorded horizon'}</span><i /></div><strong>t={risk.horizon}</strong></div>}
    {state && <WorkStrip task={task} state={state} />}
    <p className="wb-takeaway">{decision?.restoration ? 'Resuming means continue the unfinished part, not restart the whole job.' : state?.phase === 'PAUSED' ? 'Pause the job, preserve progress, and reconsider when admission is safe again.' : 'A resource allocation is a promise that must be checked again as conditions change.'}</p>
    <p className="wb-unit-note">{assumption ? `Stored commitment forecast: ${Number(assumption.soc) / 10}% at horizon t=${assumption.horizon_ns}, committed at t=${assumption.commit_ns}. This can predate this cycle and differ from the invalidation horizon.` : 'No stored commitment forecast was emitted for this job here.'} Per-candidate prediction curves are not exported and are not drawn.</p>
  </>;
}

function AllocationDrawing({ task, decision, playing }: { task: Task; decision?: Decision; playing: boolean }) {
  const [after, setAfter] = useState(false);
  const [showAll, setShowAll] = useState(false);
  useEffect(() => {
    if (!playing) return;
    const timer = setTimeout(() => setAfter(true), 250);
    return () => clearTimeout(timer);
  }, [playing]);
  if (!decision) return <NoNewDecision />;
  const dimensions = showAll ? ['CPU', 'ENERGY', 'MEMORY', 'STORAGE', 'COMM'] : ['CPU', 'ENERGY'];
  const changed = decision.ledgerAfter.some(a => decision.ledgerBefore.find(b => b.dimension === a.dimension)?.committed !== a.committed);
  const released = decision.ledgerAfter.some(a => a.dimension !== 'SENSOR' && BigInt(a.committed) < BigInt(decision.ledgerBefore.find(b => b.dimension === a.dimension)?.committed ?? '0'));
  return <><div className="wb-allocation-toolbar"><div><span className="wb-small">SUPPLY ACCOUNTING FOR T{task.id} / ALL TASKS’ HOLDS INCLUDED</span><h4>{after ? 'After this decision' : 'Before this decision'}</h4></div><button className="primary" aria-pressed={after} onClick={() => setAfter(!after)}>{after ? 'Rewind to before' : released ? 'Draw the resource release →' : changed ? 'Draw the allocation →' : 'Show unchanged reservations →'}</button></div><div className="wb-allocation-legend"><span><i className="wb-reserved" />Protected reserve</span><span><i className="wb-held" />Committed to jobs</span><span><i className="wb-free" />Still free</span></div><div className="wb-allocation-rows">{dimensions.map(dimension => {
    const before = entry(decision, dimension);
    const target = entry(decision, dimension, true);
    if (!before || !target) return null;
    const e = after ? target : before;
    const scale = max(before.capacity, target.capacity, BigInt(before.reserve) + BigInt(before.committed), BigInt(target.reserve) + BigInt(target.committed));
    const delta = BigInt(target.committed) - BigInt(before.committed);
    return <div className="wb-allocation-row" key={dimension}>
      <div className="wb-allocation-label"><strong>{resourceNames[dimension]}</strong><span>{number(e.capacity)} {units[dimension]} capacity</span></div>
      <div className="wb-allocation-main"><div className="wb-allocated-track" role="img" aria-label={`${dimension} ${after ? 'after' : 'before'} drawing: capacity ${e.capacity}, reserve ${e.reserve}, committed ${e.committed}, free ${e.free}`}><span className="wb-reserved" style={{ width: width(e.reserve, scale) }} /><span className="wb-held" style={{ width: width(e.committed, scale) }} /><span className="wb-free" style={{ width: width(e.free, scale) }} /></div><div className="wb-allocation-numbers"><span>{number(e.committed)} {units[dimension]} committed</span><strong>{number(e.free)} {units[dimension]} free</strong></div>{after && BigInt(before.free) - delta === BigInt(target.free) && BigInt(before.free) >= 0n && BigInt(target.free) >= 0n && <p className="wb-resource-equation">{number(before.free)} {delta < 0n ? '+' : '−'} {number((delta < 0n ? -delta : delta).toString())} = <strong>{number(target.free)} {units[dimension]} free</strong></p>}</div>
      <div className={`wb-delta ${delta < 0n ? 'wb-release' : ''}`}><strong>{delta > 0n ? '+' : delta < 0n ? '−' : ''}{number((delta < 0n ? -delta : delta).toString())} {units[dimension]}</strong><span>{delta > 0n ? 'reserved by this decision' : delta < 0n ? 'released by this decision' : 'no change in hold'}</span></div>
    </div>;
  })}</div><button className="wb-other-resources" aria-expanded={showAll} onClick={() => setShowAll(!showAll)}>{showAll ? 'Focus on CPU and energy' : 'Show memory, storage, communication and sensors'}</button>{showAll && <SensorDrawing before={entry(decision, 'SENSOR')} after={entry(decision, 'SENSOR', true)} showAfter={after} />}<p className="wb-takeaway">{released ? 'Releasing reservations makes those resources available for other jobs.' : changed ? 'Allocation means reserving a share, so another job cannot claim that same capacity.' : 'No reservation change was recorded for this job. A check need not allocate resources again.'}</p><details className="wb-accuracy-note"><summary>About energy and resource holds</summary><p>Energy is a budget hold, not measured battery drain. A pause may release CPU, memory and other holds while energy remains held until a fresh SOC-budget update. Negative residuals are printed exactly; their drawn width is zero.</p></details></>;
}

function SensorDrawing({ before, after, showAfter }: { before?: Ledger; after?: Ledger; showAfter: boolean }) {
  if (!before || !after) return null;
  const e = showAfter ? after : before;
  const available = BigInt(e.capacity);
  const highest = Math.max(4, available.toString(2).length, BigInt(e.committed).toString(2).length);
  return <div className="wb-sensor-row"><strong>Sensors are individual tools</strong><div>{Array.from({ length: Math.min(32, highest) }, (_, i) => {
    const bit = 1n << BigInt(i);
    const held = (BigInt(e.committed) & bit) !== 0n;
    const present = (available & bit) !== 0n;
    return <span key={i} className={held ? 'sensor-assigned' : present ? 'sensor-ready' : 'sensor-absent'}>S{i}<small>{held ? 'held' : present ? 'free' : 'absent'}</small></span>;
  })}</div><small>Committed mask {e.committed}; free mask {e.free}. One bit identifies one sensor.</small></div>;
}

function WorkStrip({ task, state }: { task: Task; state?: State }) {
  const percent = progress(task, state);
  const done = !state || state.phase === 'PENDING' || state.phase === 'REJECTED' ? 0n : state.phase === 'COMPLETE' ? BigInt(task.duration) : BigInt(task.duration) - BigInt(state.remaining);
  return <div className="wb-work-strip"><div><span>Job T{task.id}: {task.duration} sim ns of work</span><strong>{done.toString()} done · {state?.phase === 'PENDING' ? `${task.duration} not started` : state?.phase === 'REJECTED' ? 'not admitted' : `${state?.remaining ?? task.duration} remaining`}</strong></div><div className="wb-work-track" role="progressbar" aria-label={`Whiteboard task ${task.id} completed work`} aria-valuenow={percent} aria-valuemin={0} aria-valuemax={100}><span style={{ width: `${percent}%` }} /></div><p>{state?.phase === 'PAUSED' ? 'PAUSED: this filled part stays. Waiting does not erase completed work.' : state?.phase === 'COMPLETE' ? `FINISHED: ${number(state.value)} value credited once.` : state?.phase === 'PENDING' ? 'WAITING: work has not started.' : state?.phase === 'REJECTED' ? 'NOT ADMITTED: no execution work is credited.' : 'WORKING: elapsed running time reduces the unfinished part.'}</p></div>;
}

function ScheduleDrawing({ run, cursor, task, decision, state, select }: { run: Run; cursor: number; task: Task; decision?: Decision; state?: State; select: (id: string) => void }) {
  const states = run.cycles[cursor]?.states ?? run.final.states;
  return <><div className="wb-schedule">{(['PENDING', 'RUNNING', 'PAUSED', 'COMPLETE', 'REJECTED'] as const).map(phase => <div className={`wb-schedule-lane lane-${phase.toLowerCase()}`} key={phase}><h4>{phaseNames[phase]}</h4>{states.filter(s => s.phase === phase).map(s => <button key={s.task} aria-pressed={s.task === task.id} onClick={() => select(s.task)}><strong>T{s.task}</strong><span>{run.scenario.tasks.find(t => t.id === s.task)?.subsystem}</span></button>)}{!states.some(s => s.phase === phase) && <span className="wb-empty-lane">—</span>}</div>)}</div>{decision && <div className="wb-state-change"><span>{phaseNames[decision.before]}</span><b>→</b><span>{phaseNames[decision.after]}</span><small>{decision.restoration ? 'Admitted again; resume the unfinished part.' : decision.before === decision.after ? 'No state transition in this cycle.' : decision.invalidation ? label(decision.invalidation.cause) : label(decision.reason)}</small></div>}<WorkStrip task={task} state={state} /><p className="wb-takeaway">{run.scenario.tasks.length > 1 ? 'Several jobs can work together if their combined reservations fit. Select a waiting job to inspect why it was not admitted.' : state?.phase === 'PAUSED' ? 'Pausing keeps the completed part and waits for a safe opportunity to resume.' : 'Starting a job, reserving resources and tracking remaining work are different parts of scheduling.'}</p><p className="wb-unit-note">These are simulated jobs, not Linux threads or CPU-core assignments. Resource reservations are shown in the preceding explanation.</p></>;
}
