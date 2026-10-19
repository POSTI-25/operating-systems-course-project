import { useEffect, useRef } from 'react';
import { label, number, policy } from './domain';
import type { Cycle, Decision, Ledger, Phase, Run, State, Task } from './domain';
import { bounded, changedChannels, channelValue, cycleTitle, decisionStory, eventIndices, progress, ratio, steps } from './walkthrough-model';
import { ExecutionMap, HistoryPlots } from './TracePlots';
import './walkthrough.css';

interface Props { run: Run; cursor: number; move: (index: number) => void; taskId: string; select: (id: string) => void; step: number; setStep: (step: number) => void }
const channelNames: Record<string, string> = { BATTERY_SOC_MV_PCT: 'Battery charge', CPU_AVAIL_PCT: 'CPU available', 'TEMP_MC[2]': 'Payload temperature', FAULT_FLAGS: 'Subsystem faults', COMM_WINDOW_OPEN: 'Communication', MEM_AVAIL_KB: 'Memory available' };
const phases: Phase[] = ['PENDING', 'RUNNING', 'PAUSED', 'COMPLETE', 'REJECTED'];

export function Walkthrough({ run, cursor, move, taskId, select, step, setStep }: Props) {
  const cycle = run.cycles[cursor];
  const final = !cycle;
  const task = run.scenario.tasks.find(t => t.id === taskId);
  const decision = cycle?.decisions.find(d => d.task === taskId);
  const state = (cycle?.states ?? run.final.states).find(s => s.task === taskId);
  const previous = run.cycles[cursor - 1];
  const events = eventIndices(run);
  const story = decisionStory(decision, task, final, cycle?.result === 'OVERRUN');
  const eventRail = useRef<HTMLDivElement>(null);
  useEffect(() => {
    const rail = eventRail.current;
    const active = rail?.querySelector<HTMLButtonElement>('.active-event');
    if (rail && active) rail.scrollLeft += active.getBoundingClientRect().left - rail.getBoundingClientRect().left - (rail.clientWidth - active.clientWidth) / 2;
  }, [cursor, run.id]);
  return <div className="walkthrough">
    <section className="walkthrough-panel" aria-label="Guided decision walkthrough">
      <div className="walkthrough-intro"><div><span className="eyebrow">FOLLOW THE ENGINE’S DECISION</span><h2>From conditions to execution</h2><p>Step through the evidence behind this cycle, then move forward in simulation time.</p></div><label className="walkthrough-task">Inspect task<select aria-label="Walkthrough task" value={taskId} onChange={e => select(e.target.value)}>{run.scenario.tasks.map(t => <option key={t.id} value={t.id}>T{t.id} · {t.subsystem} · class {t.criticality}</option>)}</select></label></div>
      <div className="cycle-story"><span className={`story-symbol ${state?.phase.toLowerCase()}`}>{final ? '✓' : cycle?.trigger === 'FAULT' ? '!' : state?.phase === 'PAUSED' ? 'Ⅱ' : '→'}</span><div><strong>{final ? 'Final execution settlement' : cycleTitle(cycle)}</strong><p>{story}</p></div><span className="story-time">t = {cycle?.time ?? run.final.time}<small>sim ns</small></span></div>
      <nav className="process-steps" aria-label="Decision explanation steps">{steps.map((name, index) => <button key={name} className={`${index === step ? 'active-step' : ''} ${index < step ? 'read-step' : ''}`} aria-current={index === step ? 'step' : undefined} onClick={() => setStep(index)}><span>{String(index + 1).padStart(2, '0')}</span><strong>{name}</strong><small>{['What did it observe?', 'Why reconsider?', 'What survives?', 'What is admitted?', 'What changes?', 'Where is the task?'][index]}</small></button>)}</nav>
      <div className="step-content" key={`${run.id}-${cursor}-${taskId}-${step}`}>
        {step === 0 && <InputStep cycle={cycle} previous={previous} task={task} />}
        {step === 1 && <RiskStep cycle={cycle} decision={decision} taskId={taskId} variant={run.variant} />}
        {step === 2 && <GateStep decision={decision} task={task} final={final} overrun={cycle?.result === 'OVERRUN'} />}
        {step === 3 && <SelectionStep decision={decision} final={final} />}
        {step === 4 && <ResourceStep decision={decision} />}
        {step === 5 && <TaskBoard run={run} cycle={cycle} taskId={taskId} select={select} />}
      </div>
      <div className="step-footer"><span>Explanation {step + 1} / {steps.length} · captured evidence, not processor timing</span><div><button className="secondary" disabled={step === 0} onClick={() => setStep(step - 1)}>← Previous explanation</button><button className="primary" disabled={final && step === steps.length - 1} onClick={() => step < steps.length - 1 ? setStep(step + 1) : move(Math.min(run.cycles.length, cursor + 1))}>{step < steps.length - 1 ? 'Next explanation →' : 'Next cycle →'}</button></div></div>
    </section>
    <section className="event-story" aria-label="Run event navigator"><div className="visual-heading"><div><span className="eyebrow">CAUSE & EFFECT ACROSS THE CAPTURE</span><h3>Jump to an input change or decision</h3></div><span className="muted">Select a moment to explain it</span></div><div className="event-strip" ref={eventRail}>{events.map(index => {
      const c = run.cycles[index];
      const changed = changedChannels(c, run.cycles[index - 1]);
      return <button key={c.sequence} className={cursor === index ? 'active-event' : ''} aria-current={cursor === index ? 'true' : undefined} onClick={() => move(index)}><span className="mono">t={c.time}<small>cycle {c.sequence}</small></span><strong>{cycleTitle(c)}</strong><small>{index === 0 ? 'Initial observations' : changed.length ? changed.slice(0, 2).map(channel => `${channelNames[channel.name] ?? channel.name}: ${channelValue(channel)}`).join(' · ') : c.decisions.filter(d => d.before !== d.after || d.restoration).map(d => `T${d.task}: ${label(d.before)} → ${label(d.after)}`).join(' · ') || `${c.trigger} / ${c.result}`}</small></button>;
    })}<button className={final ? 'active-event' : ''} onClick={() => move(run.cycles.length)}><span className="mono">t={run.final.time}<small>final snapshot</small></span><strong>Execution settled</strong><small>No additional admission</small></button></div></section>
    <HistoryPlots run={run} cursor={cursor} task={task} />
    <ExecutionMap run={run} cursor={cursor} taskId={taskId} select={select} />
  </div>;
}

function InputStep({ cycle, previous, task }: { cycle?: Cycle; previous?: Cycle; task?: Task }) {
  if (!cycle) return <StepEmpty title="No new sensor observation">The scenario has ended. Final settlement advances execution; it does not create another telemetry frame.</StepEmpty>;
  const changed = changedChannels(cycle, previous);
  const temperature = ['BATTERY', 'BUS', 'PAYLOAD', 'ADCS'].indexOf(task?.subsystem ?? 'PAYLOAD');
  const inputs = { ...channelNames };
  delete inputs['TEMP_MC[2]'];
  inputs[`TEMP_MC[${temperature}]`] = `${task?.subsystem ?? 'Payload'} temperature`;
  return <><div className="step-heading"><h3>Conditions supplied to this cycle</h3><p>{!previous ? 'The experiment begins with these observations.' : changed.length ? `${changed.length} channel${changed.length === 1 ? '' : 's'} changed value, quality or source timestamp since the preceding cycle.` : 'No observation changed since the preceding cycle. A regular tick still checks execution and commitments.'}</p></div><div className="input-flow"><div className="input-tiles">{Object.entries(inputs).map(([name, title]) => {
    const channel = cycle.telemetry.find(c => c.name === name);
    const before = previous?.telemetry.find(c => c.name === name);
    if (!channel) return null;
    const updated = !!previous && changed.some(c => c.name === name);
    return <div key={name} className={`input-tile ${updated ? 'updated-input' : ''}`}><span>{title}{updated && <small>updated</small>}</span><strong>{before && before.value !== channel.value && <del>{channelValue(before)} → </del>}{channelValue(channel)}</strong><small>source t={channel.timestamp} · {channel.quality}</small></div>;
  })}</div><div className="flow-arrow" aria-hidden="true">→</div><div className="engine-block"><span className="eyebrow">C GOVERNOR</span><strong>{cycle.trigger === 'FAULT' ? 'Fault cycle' : 'Regular tick'}</strong><span>Observe → evaluate → update plan</span><small>{cycle.telemetry.length} channels in this captured frame</small></div></div>
    {!!changed.filter(c => !Object.hasOwn(inputs, c.name)).length && <p className="other-inputs">Other observed changes: {changed.filter(c => !Object.hasOwn(inputs, c.name)).map(c => `${c.name} ${channelValue(c)} (${c.quality})`).join(' · ')}</p>}
    <p className="step-note">These are scripted observations, not operating-system measurements. Channel timestamps and quality remain exactly as captured.</p></>;
}

function RiskStep({ cycle, decision, taskId, variant }: { cycle?: Cycle; decision?: Decision; taskId: string; variant: Run['variant'] }) {
  if (!cycle) return <StepEmpty title="No new forecast at final settlement">The capture contains a final execution state, not an additional prediction or decision cycle.</StepEmpty>;
  const assumption = cycle.assumptions.find(a => a.task === taskId);
  const soc = cycle.telemetry.find(c => c.name === 'BATTERY_SOC_MV_PCT');
  const risk = decision?.invalidation;
  return <><div className="step-heading"><h3>{decision?.restoration ? 'Reconsider the paused commitment' : risk ? 'Why the commitment is being reconsidered' : 'Forecasts support a revisable plan'}</h3><p>{variant === 'full' ? 'Holt level-and-trend prediction supports admission and revalidation. The stored assumption and any emitted risk record are shown below.' : `${policy[variant].name}: ${policy[variant].prediction}. Do not interpret this policy as using the full Holt prediction path.`}</p></div>
    <div className="risk-flow"><div className="risk-reading"><span className="eyebrow">OBSERVED SOC</span><strong>{soc ? channelValue(soc) : 'Not emitted'}</strong><small>source t={soc?.timestamp ?? '—'} · {soc?.quality ?? '—'}</small></div><div className="flow-arrow" aria-hidden="true">→</div><div className="risk-reading"><span className="eyebrow">STORED COMMITMENT FORECAST</span><strong>{assumption ? `${Number(assumption.soc) / 10}%` : 'No stored commitment'}</strong><small>{assumption ? `committed at t=${assumption.commit_ns} · horizon t=${assumption.horizon_ns}` : 'No assumption was captured for this task.'}</small></div><div className="flow-arrow" aria-hidden="true">→</div><div className={`risk-outcome ${risk && !decision?.restoration ? 'risk-warning' : ''}`}><span className="eyebrow">REVALIDATION EVIDENCE</span><strong>{decision?.restoration ? 'Restore after admission checks' : risk ? label(risk.cause) : 'No invalidation emitted'}</strong><p>{risk ? risk.cause === 'PREDICTED_RISK' ? `Risk recorded at t=${risk.time}; horizon t=${risk.horizon}. ${decision?.restoration ? 'The engine admitted a new commitment and resumed remaining work.' : 'The task keeps its remaining work while paused. A predicted-risk record is not an observed physical failure.'}` : `Recorded trigger: ${label(risk.quantity)} at t=${risk.time}.${risk.upgraded ? ' An earlier predicted cause upgraded to an observed cause.' : ''}` : 'This record contains no invalidation event. That does not imply a forecast of zero risk.'}</p></div></div>
    <p className="step-note">The stored forecast can be older than this cycle. Full per-candidate forecasts and filter internals are not exported; this view does not reconstruct or invent them.</p></>;
}

function GateStep({ decision, task, final, overrun }: { decision?: Decision; task?: Task; final: boolean; overrun: boolean }) {
  if (!decision?.candidates.length) return <StepEmpty title={overrun ? 'Discarded cycle: no published Gate result' : final ? 'Task execution has settled' : 'No new candidate evaluation'}>{final ? 'The final snapshot does not add an admission cycle.' : 'The engine did not emit candidate evaluations for this task here. A retained commitment does not require a new candidate list in every record.'}</StepEmpty>;
  return <><div className="step-heading"><h3>Filter unsafe modes before ranking</h3><p>Each executing mode must survive the enabled safety checks. The log reports the first failure, not a separate result for every predicate.</p></div><div className="gate-cards">{decision.candidates.map(candidate => {
    const profile = task?.profiles.find(p => p.mode === candidate.mode);
    const fallback = candidate.verdict === 'FALLBACK_ONLY';
    return <article key={candidate.mode} className={`gate-card verdict-${candidate.verdict.toLowerCase()}`}><div className="gate-card-title"><strong>{candidate.mode}</strong><span>{candidate.verdict === 'PASS' ? '✓' : candidate.verdict === 'ELIMINATED' ? '×' : '—'}</span></div><p>{candidate.verdict === 'PASS' ? 'Survives enabled checks' : candidate.verdict === 'ELIMINATED' ? `Stopped: ${label(candidate.predicate.replace('GATE_', ''))}` : fallback ? 'Fallback, not an executing candidate' : 'Disabled by this policy'}</p><small>{profile && task ? `${number(profile.energy)} mJ · ${BigInt(task.value) * BigInt(profile.fraction) / 1000n} value` : 'No execution allocation'}</small>{candidate.skipMask !== '0x00' && <small>Skipped checks mask {candidate.skipMask}</small>}<div className="gate-exit">{candidate.verdict === 'PASS' ? '↓ eligible for ranking' : candidate.verdict === 'ELIMINATED' ? '⊣ does not reach ranking' : 'outside survivor set'}</div></article>;
  })}</div>{decision.after === 'PAUSED' && <p className="step-note attention">Passing candidates do not override a revalidation pause. This record publishes no executable selection for the paused task.</p>}</>;
}

function SelectionStep({ decision, final }: { decision?: Decision; final: boolean }) {
  if (!decision) return <StepEmpty title={final ? 'No new admission at scenario end' : 'No selection record'}>Only choices emitted by the engine are shown.</StepEmpty>;
  const survivors = decision.candidates.filter(c => c.verdict === 'PASS');
  const executing = ['FULL', 'THROTTLED', 'DEGRADED'].includes(decision.selected);
  return <><div className="step-heading"><h3>{executing ? 'Choose among safe executing modes' : 'An evaluation need not produce a dispatch'}</h3><p>Utility ranks survivors. The capture exports the selected score only; it does not include the other candidates’ scores.</p></div><div className="selection-flow"><div className="survivor-set"><span className="eyebrow">GATE SURVIVORS</span>{survivors.length ? survivors.map(c => <span className="survivor-token" key={c.mode}>✓ {c.mode}</span>) : <span className="muted">{decision.candidates.length ? 'No executing survivors' : 'No new survivor set emitted'}</span>}</div><div className="flow-arrow" aria-hidden="true">→</div><div className="selection-engine"><span className="eyebrow">RANK / FALLBACK</span><strong>{executing ? 'Utility' : decision.selected === 'DEFERRED' ? 'Wait for another window' : decision.selected === 'REJECTED' ? 'Reject admission' : 'No new dispatch'}</strong><small>{decision.after === 'PAUSED' ? 'Revalidation has paused the commitment.' : label(decision.reason)}</small></div><div className="flow-arrow" aria-hidden="true">→</div><div className={`selection-result ${executing ? 'has-selection' : ''}`}><span className="eyebrow">EMITTED SELECTION</span><strong>{decision.selected === 'NONE' ? 'None' : decision.selected}</strong><small>{decision.score != null ? `selected utility ${decision.score}` : 'No utility score emitted'}</small></div></div><p className="step-note">The selected score is a ranking number, not a percentage or delivered mission value. Existing running work can continue without a new mode selection.</p></>;
}

const units: Record<string, string> = { CPU: '%', ENERGY: 'mJ', MEMORY: 'KB', STORAGE: 'KB', COMM: '%' };
function ResourceBar({ entry, scale, name }: { entry: Ledger; scale: bigint; name: string }) {
  return <div className="resource-reading"><div className="resource-numbers"><span>{number(entry.committed)} committed</span><span>{number(entry.free)} free</span></div><div className="allocation-bar" role="img" aria-label={`${name}: capacity ${entry.capacity}, reserve ${entry.reserve}, committed ${entry.committed}, free ${entry.free}`}><span className="reserved-part" style={{ width: `${bounded(ratio(entry.reserve, scale))}%` }} /><span className="committed-part" style={{ width: `${bounded(ratio(entry.committed, scale))}%` }} /><span className="free-part" style={{ width: `${bounded(ratio(entry.free, scale))}%` }} /></div><small>capacity {number(entry.capacity)} · protected reserve {number(entry.reserve)}</small></div>;
}
function ResourceStep({ decision }: { decision?: Decision }) {
  if (!decision) return <StepEmpty title="No new decision Ledger snapshot">Final settlement publishes task states without an artificial resource-allocation decision.</StepEmpty>;
  return <><div className="step-heading"><h3>Resource reservations before → after this decision</h3><p>These bars show the captured Ledger for the selected task’s decision, including existing reservations from other work.</p></div><div className="resource-flow"><div className="resource-flow-head"><span>Resource</span><span>Before decision</span><span>After decision</span></div>{decision.ledgerAfter.filter(entry => entry.dimension !== 'SENSOR').map(after => {
    const before = decision.ledgerBefore.find(entry => entry.dimension === after.dimension)!;
    const scale = [BigInt(before.capacity), BigInt(after.capacity), BigInt(before.committed) + BigInt(before.reserve), BigInt(after.committed) + BigInt(after.reserve), 1n].reduce((a, b) => a > b ? a : b);
    const delta = BigInt(after.committed) - BigInt(before.committed);
    return <div className="resource-flow-row" key={after.dimension}><div className="resource-name"><strong>{after.dimension}</strong><small>{units[after.dimension]}</small><span className={delta > 0n ? 'allocation-added' : delta < 0n ? 'allocation-released' : ''}>{delta > 0n ? `+${number(delta.toString())} allocated` : delta < 0n ? `${number((-delta).toString())} released` : 'unchanged hold'}</span></div><ResourceBar entry={before} scale={scale} name={`${after.dimension} before`} /><ResourceBar entry={after} scale={scale} name={`${after.dimension} after`} /></div>;
  })}</div><div className="allocation-legend"><span><i className="reserved-part" />Protected reserve</span><span><i className="committed-part" />Committed</span><span><i className="free-part" />Free</span></div><p className="step-note">Energy bars represent budget holds, not battery consumption. Negative residuals are printed exactly; their bar width is zero. Bar scales follow the displayed capacities and holds.</p><div className="sensor-bits"><span className="eyebrow">SENSOR ALLOCATION · EACH SQUARE IS ONE BIT</span>{(() => {
    const after = decision.ledgerAfter.find(entry => entry.dimension === 'SENSOR')!;
    const before = decision.ledgerBefore.find(entry => entry.dimension === 'SENSOR')!;
    return <><div className="sensor-bit-strip" role="img" aria-label={`Sensor committed mask before ${before.committed}, after ${after.committed}`}>{Array.from({ length: 32 }, (_, index) => {
      const mask = 1n << BigInt(index);
      const available = (BigInt(after.capacity) & mask) !== 0n;
      const held = (BigInt(after.committed) & mask) !== 0n;
      return <span key={index} className={held ? 'sensor-held' : available ? 'sensor-free' : 'sensor-unavailable'} title={`Sensor bit ${index}: ${held ? 'committed' : available ? 'available' : 'unavailable'}`}>{index}</span>;
    })}</div><small>Committed mask {before.committed} → {after.committed}; free mask {after.free}. Bit 0 is on the left. Dark = committed; pale = free; hatched = unavailable.</small></>;
  })()}</div></>;
}

function TaskBoard({ run, cycle, taskId, select }: { run: Run; cycle?: Cycle; taskId: string; select: (id: string) => void }) {
  const states: State[] = cycle?.states ?? run.final.states;
  return <><div className="step-heading"><h3>Task states after {cycle ? `cycle ${cycle.sequence}` : 'final settlement'}</h3><p>The engine can keep several tasks running when resources allow. These lanes show simulated task states, not CPU-thread assignments.</p></div><div className="task-board">{phases.map(phase => <section key={phase} className={`task-lane phase-${phase.toLowerCase()}`}><h4>{label(phase)}<span>{states.filter(s => s.phase === phase).length}</span></h4>{states.filter(s => s.phase === phase).map(state => {
    const task = run.scenario.tasks.find(t => t.id === state.task)!;
    const percent = progress(task, state);
    const d = cycle?.decisions.find(item => item.task === state.task);
    return <button key={state.task} className={`task-token ${taskId === state.task ? 'focused-task' : ''}`} onClick={() => select(state.task)} aria-pressed={taskId === state.task}><strong>T{state.task} <span>{task.subsystem}</span></strong><small>class {task.criticality} · deadline {task.deadline}</small><div className="work-meter" role="progressbar" aria-label={`Task ${state.task} completed execution`} aria-valuemin={0} aria-valuemax={100} aria-valuenow={percent}><span style={{ width: `${percent}%` }} /></div><span>{phase === 'PENDING' ? `${task.duration} ns required · not started` : phase === 'REJECTED' ? 'Not admitted' : `${state.remaining} ns remaining`}</span><small>{phase === 'COMPLETE' ? `completed at ${state.completed} · value ${state.value}` : `delivered value ${state.value}`}</small>{d && d.before !== d.after && <small>{label(d.before)} → {label(d.after)}</small>}</button>;
  })}{!states.some(s => s.phase === phase) && <span className="empty-lane">No tasks</span>}</section>)}</div><p className="step-note">Execution progress is retained during a pause. Mission value is credited on completion, not continuously. Select a task to follow its decision and resource record.</p></>;
}

function StepEmpty({ title, children }: { title: string; children: React.ReactNode }) { return <div className="step-empty"><span aria-hidden="true">[ — ]</span><h3>{title}</h3><p>{children}</p></div>; }
