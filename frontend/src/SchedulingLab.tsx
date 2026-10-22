import { useEffect, useMemo, useRef, useState } from 'react';
import type { CSSProperties } from 'react';
import type { Ledger, Run, State } from './domain';
import { number, policy } from './domain';
import { bounded, channelValue, progress, ratio } from './walkthrough-model';
import { buildLabItems, decisionAt, gateReason, itemSourceLines, labCycleLabel, labStages, labStates, ledgerAt, phaseName } from './lab-model';
import type { LabItem } from './lab-model';
import './scheduler-lab.css';

interface Props { run: Run; taskId: string; move: (index: number) => void; select: (id: string) => void }
const phaseOrder = ['PENDING', 'RUNNING', 'PAUSED', 'COMPLETE', 'REJECTED'];
const unit: Record<string, string> = { CPU: '%', ENERGY: 'mJ', MEMORY: 'KB', STORAGE: 'KB', COMM: '%', SENSOR: 'mask' };
const resourceLabel: Record<string, string> = { CPU: 'CPU', ENERGY: 'Energy', MEMORY: 'Memory', STORAGE: 'Storage', COMM: 'Communication', SENSOR: 'Sensors' };

export function SchedulingLab({ run, taskId, move, select }: Props) {
  const items = useMemo(() => buildLabItems(run), [run]);
  const [stage, setStage] = useState(0);
  const [step, setStep] = useState(0);
  const [tab, setTab] = useState<'visual' | 'raw'>('visual');
  const [playing, setPlaying] = useState(false);
  const [speed, setSpeed] = useState(1800);
  const sourceRef = useRef<HTMLDivElement>(null);
  const item = items[stage][step];
  const task = run.scenario.tasks.find(t => t.id === (item?.task ?? taskId)) ?? run.scenario.tasks[0];
  const cycle = item?.cycle == null ? undefined : run.cycles[item.cycle];
  const decision = decisionAt(run, item);
  const lines = itemSourceLines(run, item);
  const states = labStates(run, item, stage);
  function adjacent(direction: 1 | -1): [number, number] | undefined {
    for (let chapter = stage; chapter >= 0 && chapter < items.length; chapter += direction) {
      const position = chapter === stage ? step + direction : direction === 1 ? 0 : items[chapter].length - 1;
      if (position >= 0 && position < items[chapter].length) return [chapter, position];
    }
  }
  const nextPosition = adjacent(1);
  const previousPosition = adjacent(-1);
  function go(nextStage: number, nextStep = 0, keepPlaying = false) {
    const position = Math.max(0, Math.min(Math.max(0, items[nextStage].length - 1), nextStep));
    setStage(nextStage); setStep(position); setPlaying(keepPlaying);
    const next = items[nextStage][position];
    if (next?.task) select(next.task);
    move(nextStage === 7 ? run.cycles.length : next?.cycle ?? 0);
  }
  useEffect(() => {
    if (!playing) return;
    const timer = setTimeout(() => {
      const next = adjacent(1);
      if (next) go(next[0], next[1], true);
      else setPlaying(false);
    }, speed);
    return () => clearTimeout(timer);
  }, [playing, stage, step, speed, items]);
  useEffect(() => {
    const box = sourceRef.current;
    const active = box?.querySelector<HTMLElement>('.sl-source-active');
    if (box && active) box.scrollTop = Math.max(0, active.offsetTop - box.clientHeight / 3);
  }, [stage, step, run.id]);
  function chooseTask(id: string) {
    const same = items[stage].findIndex(i => i.task === id && i.cycle === item?.cycle);
    if (same >= 0) go(stage, same);
    else go(5, Math.max(0, items[5].findIndex(i => i.task === id && (item?.cycle == null || i.cycle === item.cycle))));
  }
  function jump(kind: 'pause' | 'restore' | 'complete') {
    const targetStage = kind === 'complete' ? 5 : 6;
    const target = items[targetStage].findIndex(i => {
      const d = decisionAt(run, i);
      return kind === 'pause' ? d?.before !== 'PAUSED' && d?.after === 'PAUSED' : kind === 'restore' ? d?.restoration : d?.before !== 'COMPLETE' && d?.after === 'COMPLETE';
    });
    if (target >= 0) go(targetStage, target);
  }
  const status = item?.candidate?.verdict ?? (cycle?.result === 'OVERRUN' ? 'DISCARDED' : stage === 7 ? 'FINAL' : 'CAPTURED');
  return <section className="scheduler-lab" aria-label="Interactive scheduling lab">
    <div className="sl-heading"><div><span className="eyebrow">THE INTERACTIVE LAB</span><h2>Open the scheduler.</h2><p>Follow the inputs, inspect the choices, then watch the jobs execute.</p></div><span className="sl-policy">{policy[run.variant].name}</span></div>
    <div className="sl-overview" role="group" aria-label="Scheduling pipeline overview">{labStages.map((s, i) => <button key={s.name} aria-pressed={i === stage} onClick={() => go(i)}><span>0{i + 1}</span><strong>{s.short}</strong></button>)}</div>
    <div className="sl-layout">
      <aside className="sl-journey"><span className="eyebrow">FOLLOW THE JOURNEY</span><nav aria-label="Scheduling journey">{labStages.map((s, i) => <button key={s.name} className={stage === i ? 'sl-current-stage' : ''} aria-current={stage === i ? 'step' : undefined} onClick={() => go(i)}><span>0{i + 1}</span><div><strong>{s.name}</strong><small>{items[i].length} {i === 0 ? 'source lines' : 'recorded items'}</small></div><b>{i < stage ? '✓' : '→'}</b></button>)}</nav><p>Click Play once to continue through all phases. Phases without events are skipped. Select any stage to pause and inspect it.</p></aside>
      <section className="sl-workspace" aria-label="Scheduler stage visualization"><div className="sl-chapter"><span>0{stage + 1} / {labStages.length}</span><span className={`sl-status status-${status.toLowerCase()}`}>{status.replaceAll('_', ' ')}</span></div><h3>{labStages[stage].title}</h3><p className="sl-intro">{labStages[stage].intro}</p><p className="sl-concept">{labStages[stage].concept}</p><div className="sl-artifact-bar"><div role="group" aria-label="Scheduler stage view"><button aria-pressed={tab === 'visual'} onClick={() => { setPlaying(false); setTab('visual'); }}>Visual steps</button><button aria-pressed={tab === 'raw'} onClick={() => { setPlaying(false); setTab('raw'); }}>Raw evidence</button></div><span>{items[stage].length ? `${step + 1} / ${items[stage].length}` : 'No events'}</span></div>
        <div className="sl-visual" aria-label="Selected scheduling evidence">{!item ? <div className="sl-empty"><strong>No such event in this capture.</strong><p>{stage === 6 ? 'This policy and scenario recorded no invalidation, restoration or emergency event. Try False-alarm restoration with SAGE-RT full to follow recovery.' : 'The engine emitted no records for this teaching stage.'}</p></div> : tab === 'raw' ? <pre className="sl-json">{JSON.stringify(stage === 0 ? item.text : stage === 1 ? cycle?.telemetry : stage === 2 ? task.profiles.find(p => p.mode === item.mode) : stage === 3 ? item.candidate : stage === 7 ? run.final.states.find(s => s.task === item.task) : decision, null, 2)}</pre> : <StageVisual run={run} item={item} stage={stage} step={step} items={items[stage]} choose={position => go(stage, position)} />}</div>
        <div className="sl-detail" aria-live="polite"><span>{item?.cycle == null ? stage === 7 ? `t=${run.final.time}` : 'INPUT' : `t=${cycle?.time}`}</span><div><strong>{item?.title ?? 'No recorded item'}</strong><p>{item?.text ?? 'Nothing is invented to fill an empty stage.'}</p></div></div>
        <div className="sl-transport"><div><button aria-label="Restart scheduling stage" onClick={() => go(stage)}>↺</button><button aria-label="Previous scheduling step" disabled={!previousPosition} onClick={() => previousPosition && go(...previousPosition)}>←</button><button className="sl-play" onClick={() => { if (playing) setPlaying(false); else if (nextPosition) setPlaying(true); else go(items.findIndex(chapter => chapter.length > 0), 0, true); }}>{playing ? 'Pause' : 'Play'}</button><button aria-label="Next scheduling step" disabled={!nextPosition} onClick={() => nextPosition && go(...nextPosition)}>→</button></div><label>Step {item ? step + 1 : 0} of {items[stage].length}<input type="range" aria-label="Step within scheduling stage" min={0} max={Math.max(0, items[stage].length - 1)} value={step} disabled={!item} onChange={e => go(stage, Number(e.target.value))} /></label><select aria-label="Scheduling playback speed" value={speed} onChange={e => setSpeed(Number(e.target.value))}><option value={3000}>Slow</option><option value={1800}>Normal</option><option value={500}>Fast</option></select></div>
        <div className="sl-chapter-nav"><button disabled={stage === 0} onClick={() => go(stage - 1)}>← Previous stage</button><button disabled={stage === labStages.length - 1} onClick={() => go(stage + 1)}>Next stage →</button></div>
      </section>
      <aside className="sl-source-panel"><div><span className="eyebrow">YOUR MISSION</span><h3>Scenario source</h3><small>{run.scenario.id}.scn · read-only</small></div><div className="sl-source" ref={sourceRef} aria-label="Scenario source with highlighted evidence">{run.scenario.raw.split(/\r?\n/).map((line, index) => <button key={index} className={lines.includes(index + 1) ? 'sl-source-active' : ''} aria-label={`Source line ${index + 1}: ${line}`} onClick={() => go(0, Math.max(0, items[0].findIndex(i => i.line === index + 1)))}><span>{index + 1}</span><code><b>{line.split(' ')[0]}</b>{line.includes(' ') ? line.slice(line.indexOf(' ')) : ''}</code></button>)}</div><div className="sl-source-help"><strong>Connected to the evidence.</strong><p>Select an option or a trace event to highlight its input. Click a source line to explore what it defines.</p><strong>What is actually running?</strong><p>The C engine already produced this capture. Playback reveals recorded results; it does not execute C again or trace individual function-call timing.</p></div></aside>
    </div>
    <section className="sl-machine" aria-label="Animated spacecraft scheduler state"><div className="sl-machine-heading"><div><span className="eyebrow">FROM A DECISION TO A VISIBLE RESULT</span><h3>The jobs on the spacecraft</h3></div><span>{labCycleLabel(cycle, stage, run)}</span></div><div className="sl-jump-events">{(['pause', 'restore', 'complete'] as const).map(kind => <button key={kind} disabled={!items[kind === 'complete' ? 5 : 6].some(i => { const d = decisionAt(run, i); return kind === 'pause' ? d?.before !== 'PAUSED' && d?.after === 'PAUSED' : kind === 'restore' ? d?.restoration : d?.before !== 'COMPLETE' && d?.after === 'COMPLETE'; })} onClick={() => jump(kind)}>{kind === 'pause' ? 'First pause' : kind === 'restore' ? 'First restoration' : 'First completion'} ↗</button>)}</div><StateMachine run={run} states={states} taskId={task.id} choose={chooseTask} /><ResourceMachine run={run} item={item} stage={stage} items={items[stage]} step={step} /><p className="sl-evidence-note">{stage < 3 ? 'Preview of configured inputs; no execution is shown yet.' : stage === 3 ? 'Last published task states before this cycle; candidate checks are revealed in emitted order, not measured as C call timings.' : stage === 4 ? 'Task states are the recorded before/after endpoints. Resource fields reveal snapshots, not live intermediate plans.' : 'Published simulation states. Task movement illustrates a state change; it is not a Linux thread or CPU-core assignment.'}</p></section>
  </section>;
}

function StageVisual({ run, item, stage, step, items, choose }: { run: Run; item: LabItem; stage: number; step: number; items: LabItem[]; choose: (index: number) => void }) {
  const eventsRef = useRef<HTMLDivElement>(null);
  useEffect(() => {
    const box = eventsRef.current;
    const active = box?.querySelector<HTMLElement>('.sl-event-current');
    if (box && active) box.scrollTop = Math.max(0, active.offsetTop - box.clientHeight / 3);
  }, [item.id]);
  const task = run.scenario.tasks.find(t => t.id === item.task) ?? run.scenario.tasks[0];
  const cycle = item.cycle == null ? undefined : run.cycles[item.cycle];
  const decision = decisionAt(run, item);
  if (stage === 0) return <div className="sl-source-artifact"><span className="sl-small">SOURCE LINE {item.line}</span><pre>{item.text}</pre><div className="sl-read-arrow">↓</div><strong>{item.title}</strong><div className="sl-source-tokens">{item.text.split(/\s+/).map((word, index) => <span key={index} className={index === 0 ? 'sl-keyword' : /^\d/.test(word) ? 'sl-number' : ''}>{word}</span>)}</div><p>These are scenario fields, not instructions in a flight-control language.</p></div>;
  if (stage === 1) {
    const previous = item.cycle == null ? undefined : run.cycles[item.cycle - 1];
    return <div className="sl-observations">{cycle?.telemetry.map(channel => {
      const before = previous?.telemetry.find(c => c.name === channel.name);
      const changed = before && (before.value !== channel.value || before.timestamp !== channel.timestamp || before.quality !== channel.quality);
      return <div key={channel.name} className={changed ? 'sl-channel-updated' : ''}><span>{channel.name.replace('BATTERY_SOC_MV_PCT', 'BATTERY_SOC')}</span><strong>{before && before.value !== channel.value && <del>{channelValue(before)} → </del>}{channelValue(channel)}</strong><small>source t={channel.timestamp} · {channel.quality}{changed ? ' · updated' : ''}</small></div>;
    })}</div>;
  }
  if (stage === 2 || stage === 3) {
    const profiles = task.profiles;
    const isGate = stage === 3;
    const position = isGate ? decision?.candidates.findIndex(c => c.mode === item.mode) ?? -1 : profiles.findIndex(p => p.mode === item.mode);
    const cards = isGate ? decision?.candidates.map(c => ({ mode: c.mode, candidate: c })) ?? [] : profiles.map(p => ({ mode: p.mode, candidate: undefined }));
    return <><div className="sl-flow-caption"><span>JOB T{task.id}</span><b>→</b><span>{isGate ? 'SAFETY CHECK' : 'CONFIGURED EXECUTION PROFILES'}</span><b>→</b><span>{isGate ? 'SURVIVE / ELIMINATE' : 'DIFFERENT RESOURCE REQUESTS'}</span></div><div className="sl-mode-stream">{cards.map(({ mode, candidate }, index) => {
      const profile = profiles.find(p => p.mode === mode);
      const revealed = index <= position;
      const active = item.mode === mode;
      const corresponding = items.findIndex(i => i.mode === mode && i.task === task.id && (isGate ? i.cycle === item.cycle : true));
      return <button key={mode} className={`${active ? 'sl-active-artifact' : ''} ${revealed ? 'sl-revealed' : 'sl-unrevealed'} ${candidate && revealed ? `sl-mode-${candidate.verdict.toLowerCase()}` : ''}`} aria-pressed={active} onClick={() => choose(corresponding)}><span className="sl-mode-token">T{task.id}</span><strong>{mode}</strong>{profile ? <><span>{number(profile.energy)} mJ · {profile.cpu}% CPU</span><small>{BigInt(task.value) * BigInt(profile.fraction) / 1000n} value on completion</small></> : <span>Non-executing fallback</span>}<b>{!revealed ? '?' : !candidate ? 'profile' : candidate.verdict === 'PASS' ? '✓ pass' : candidate.verdict === 'ELIMINATED' ? '× eliminated' : candidate.verdict === 'FALLBACK_ONLY' ? 'fallback' : 'disabled'}</b></button>;
    })}</div>{item.candidate && <div className={`sl-gate-result ${item.candidate.verdict === 'ELIMINATED' ? 'sl-negative' : ''}`}><strong>{item.mode}: {item.candidate.verdict === 'ELIMINATED' ? gateReason[item.candidate.predicate] ?? item.candidate.predicate : item.text}</strong><p>{item.candidate.verdict === 'PASS' ? 'This option can reach ranking; passing does not automatically select it.' : item.candidate.verdict === 'ELIMINATED' ? 'It leaves the executing survivor set. The log reports the first failed check only.' : item.candidate.verdict === 'FALLBACK_ONLY' ? 'Deferred and rejected are outcomes, not resource-consuming execution profiles.' : 'The selected policy does not consider this mode.'}</p><small>Skipped-check mask {item.candidate.skipMask}</small></div>}</>;
  }
  if (stage === 4 && decision) {
    if (item.kind === 'selection') return <div className="sl-selection"><div><span className="sl-small">SURVIVING EXECUTING OPTIONS</span><div className="sl-survivors">{decision.candidates.filter(c => c.verdict === 'PASS').map(c => <span key={c.mode}>{c.mode}</span>)}{!decision.candidates.some(c => c.verdict === 'PASS') && <span>No new survivor set</span>}</div></div><b>→</b><div className="sl-selection-result"><span className="sl-small">C ENGINE OUTCOME</span><strong>{decision.selected === 'NONE' ? phaseName[decision.after] : decision.selected}</strong><small>{decision.score == null ? 'No selected Utility score emitted' : `Selected Utility: ${decision.score}`}</small></div></div>;
    const before = ledgerAt(decision, item.dimension!);
    const after = ledgerAt(decision, item.dimension!, true);
    if (!before || !after) return null;
    return <div className="sl-resource-transition"><h4>{resourceLabel[item.dimension!]}</h4>{item.dimension === 'SENSOR' ? <div className="sl-mask-transition"><span>Committed {before.committed}</span><b>→</b><strong>Committed {after.committed}</strong></div> : <><ResourceStrip entry={before} dimension={item.dimension!} label="Before" /><div className="sl-read-arrow">↓</div><ResourceStrip entry={after} dimension={item.dimension!} label="After" /></>}<p>{item.dimension === 'SENSOR' ? 'A bit identifies a sensor. These are masks, not scalar amounts.' : BigInt(after.committed) > BigInt(before.committed) ? 'The reserved share grows. Less remains for another job.' : BigInt(after.committed) < BigInt(before.committed) ? 'The reserved share shrinks. Released capacity becomes available again.' : 'The capacity or accounting changed without a new committed share.'}</p></div>;
  }
  if ((stage === 5 || stage === 6) && decision) {
    const state = cycle?.states.find(s => s.task === task.id);
    const events = items.map((i, index) => ({ i, index })).filter(({ i }) => stage === 5 || i.task === task.id);
    return <div className="sl-runtime"><div className="sl-transition"><span>{phaseName[decision.before]}</span><b>→</b><strong>{phaseName[decision.after]}</strong></div><div className="sl-work"><div><span>T{task.id} · {task.duration} sim ns required</span><strong>{decision.remaining} remaining</strong></div><div role="progressbar" aria-label={`Lab job ${task.id} completed work`} aria-valuemin={0} aria-valuemax={100} aria-valuenow={progress(task, state)}><span style={{ width: `${progress(task, state)}%` }} /></div></div>{stage === 6 && decision.invalidation && <div className="sl-risk-record"><span>{decision.invalidation.cause.replaceAll('_', ' ')}</span><strong>Recorded at t={decision.invalidation.time} → horizon t={decision.invalidation.horizon}</strong><p>{decision.restoration ? 'Admission passed again. Resume the same unfinished portion.' : decision.invalidation.cause === 'PREDICTED_RISK' ? 'A forecast warning, not an observed crash. Preserve progress while paused.' : 'An observed condition invalidated the commitment.'}</p></div>}<div className="sl-runtime-events" ref={eventsRef} aria-label="Chronological scheduling events">{events.map(({ i, index }) => <button key={i.id} className={index === step ? 'sl-event-current' : ''} onClick={() => choose(index)}><span>{i.cycle == null ? 'end' : `t=${run.cycles[i.cycle].time}`}</span><strong>{i.title}</strong><small>{i.text}</small></button>)}</div></div>;
  }
  if (stage === 7) return <div className="sl-results">{run.final.states.map((s, index) => <button key={s.task} className={s.task === item.task ? 'sl-active-artifact' : ''} onClick={() => choose(index)}><strong>T{s.task}</strong><span>{phaseName[s.phase]}</span><b>{number(s.value)} value</b><small>{s.completed == null ? `${s.remaining} sim ns remain` : `Completed at t=${s.completed}`}</small></button>)}</div>;
  return null;
}

function ResourceStrip({ entry, dimension, label }: { entry: Ledger; dimension: string; label: string }) {
  const scale = [BigInt(entry.capacity), BigInt(entry.reserve) + BigInt(entry.committed), 1n].reduce((a, b) => a > b ? a : b);
  return <div className="sl-resource-strip"><div><span>{label} · {resourceLabel[dimension]}</span><strong className={BigInt(entry.free) < 0n ? 'sl-negative' : ''}>{number(entry.free)} {unit[dimension]} free</strong></div><div className="sl-reservoir" role="img" aria-label={`Lab ${dimension} ${label}: capacity ${entry.capacity}, reserved ${entry.reserve}, committed ${entry.committed}, free ${entry.free}`}><span className="sl-protected" style={{ width: `${bounded(ratio(entry.reserve, scale))}%` }} /><span className="sl-committed" style={{ width: `${bounded(ratio(entry.committed, scale))}%` }} /><span className="sl-available" style={{ width: `${bounded(ratio(entry.free, scale))}%` }} /></div><small>{number(entry.committed)} {unit[dimension]} committed · capacity {number(entry.capacity)} {unit[dimension]} · protected reserve {number(entry.reserve)}</small></div>;
}

function StateMachine({ run, states, taskId, choose }: { run: Run; states: State[]; taskId: string; choose: (id: string) => void }) {
  return <div className="sl-process-board"><div className="sl-lane-headings">{phaseOrder.map(p => <span key={p}>{phaseName[p]}</span>)}</div>{run.scenario.tasks.map(task => {
    const state = states.find(s => s.task === task.id);
    if (!state) return null;
    return <div className="sl-process-row" key={task.id}><div className="sl-process-track" aria-hidden="true" /><button data-task={task.id} data-phase={state.phase} aria-label={`Lab T${task.id}: ${phaseName[state.phase]}`} aria-pressed={task.id === taskId} onClick={() => choose(task.id)} style={{ '--phase': Math.max(0, phaseOrder.indexOf(state.phase)) } as CSSProperties}><strong>T{task.id}</strong><span>{task.subsystem}</span><i style={{ width: `${progress(task, state)}%` }} /></button></div>;
  })}</div>;
}

function ResourceMachine({ run, item, stage, items, step }: { run: Run; item?: LabItem; stage: number; items: LabItem[]; step: number }) {
  if (stage < 3 || stage === 7) return <p className="sl-resource-placeholder">{stage === 7 ? 'Final settlement emits task states, not a new resource-allocation snapshot.' : 'Resource reservations appear when you inspect a recorded decision.'}</p>;
  const decision = decisionAt(run, item);
  if (!decision) return null;
  const revealed = new Set(items.slice(0, step + 1).filter(i => i.cycle === item?.cycle && i.task === item?.task && i.kind === 'resource').map(i => i.dimension));
  return <div className="sl-machine-resources">{['CPU', 'ENERGY', 'MEMORY', 'STORAGE', 'COMM'].map(dimension => {
    const after = stage > 4 || stage === 4 && revealed.has(dimension);
    const value = ledgerAt(decision, dimension, after);
    return value && <ResourceStrip key={dimension} entry={value} dimension={dimension} label={after ? 'After' : 'Before'} />;
  })}</div>;
}
