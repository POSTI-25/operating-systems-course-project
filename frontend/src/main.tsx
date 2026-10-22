import React, { useEffect, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { descriptions, download, label, number, policy, significant, titles } from './domain';
import type { Channel, Decision, Ledger, Metric, Page, Run, Scenario, Variant } from './domain';
import { Walkthrough } from './Walkthrough';
import { SchedulingLab } from './SchedulingLab';
import { SchedulerExplorer } from './SchedulerExplorer';
import { describeEvent, MissionMap, ScenarioBrief } from './MissionMap';
import { steps } from './walkthrough-model';
import './styles.css';

async function api<T>(path: string, body?: object): Promise<T> {
  const response = await fetch(`/api/${path}`, body ? { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) } : undefined);
  const data = await response.json();
  if (!response.ok) throw new Error(data.error ?? `Request failed (${response.status})`);
  return data;
}
function Arrow({ children }: { children: React.ReactNode }) { return <>{children}<span aria-hidden="true">↗</span></>; }
function Badge({ value }: { value: string }) { return <span className={`badge ${value.toLowerCase().replaceAll('_', '-')}`}>{label(value)}</span>; }
function PanelTitle({ eyebrow, title, extra }: { eyebrow: string; title: string; extra?: React.ReactNode }) { return <div className="panel-title"><div><span className="eyebrow">{eyebrow}</span><h2>{title}</h2></div>{extra}</div>; }
function Empty({ title, children }: { title: string; children: React.ReactNode }) { return <div className="empty"><span className="empty-symbol" aria-hidden="true">[ / ]</span><h2>{title}</h2><p>{children}</p></div>; }

function App() {
  const [page, setPage] = useState<Page>('lab');
  const [scenarios, setScenarios] = useState<Scenario[]>([]);
  const [scenarioId, setScenarioId] = useState('low_soc_eclipse');
  const [variant, setVariant] = useState<Variant>('full');
  const [health, setHealth] = useState<{ connected: boolean; engineReady: boolean } | null>(null);
  const [metrics, setMetrics] = useState<Metric[]>([]);
  const [run, setRun] = useState<Run | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const [cursor, setCursor] = useState(0);
  const [step, setStep] = useState(0);
  const [taskId, setTaskId] = useState('1');
  const [playing, setPlaying] = useState(false);
  const [speed, setSpeed] = useState(8000);
  const scenario = scenarios.find(s => s.id === scenarioId);
  useEffect(() => {
    let alive = true;
    Promise.all([api<{ connected: boolean; engineReady: boolean }>('health'), api<Scenario[]>('scenarios'), api<{ rows: Metric[] }>('metrics')]).then(([h, s, m]) => {
      if (alive) { setHealth(h); setScenarios(s); setMetrics(m.rows); }
    }).catch(e => { if (alive) setError(e.message); });
    const timer = setInterval(() => api<{ connected: boolean; engineReady: boolean }>('health').then(h => { if (alive) setHealth(h); }).catch(() => { if (alive) setHealth(null); }), 5000);
    return () => { alive = false; clearInterval(timer); };
  }, []);
  useEffect(() => {
    if (!playing || !run) return;
    const timer = setTimeout(() => {
      if (cursor >= run.cycles.length) { setPlaying(false); return; }
      if (step < steps.length - 1) setStep(step + 1);
      else if (cursor + 1 >= run.cycles.length) { setCursor(run.cycles.length); setStep(steps.length - 1); setPlaying(false); }
      else { setCursor(cursor + 1); setStep(0); }
    }, speed);
    return () => clearTimeout(timer);
  }, [playing, speed, run, cursor, step]);
  useEffect(() => { if (page !== 'inspect') setPlaying(false); }, [page]);
  async function execute() {
    await executeFor(scenarioId, variant, page === 'lab' ? 'lab' : 'inspect');
  }
  async function executeFor(nextScenario: string, nextVariant: Variant, destination: Page) {
    setBusy(true); setError(''); setPlaying(false);
    try {
      const capture = await api<Run>('runs', { scenario: nextScenario, variant: nextVariant });
      setRun(capture); setCursor(0); setStep(0); setTaskId(capture.scenario.tasks[0]?.id ?? '1'); setPage(destination);
    } catch (e) { setError(e instanceof Error ? e.message : 'Replay failed.'); }
    finally { setBusy(false); }
  }
  function move(index: number) { setPlaying(false); setCursor(index); setStep(index === run?.cycles.length ? steps.length - 1 : 0); }
  function explainStep(index: number) { setPlaying(false); setStep(index); }
  function selectTask(id: string) { setPlaying(false); setTaskId(id); setStep(0); }
  return <div className="application">
    <header className="app-header">
      <a className="identity" href="#" onClick={e => { e.preventDefault(); setPage('lab'); }} aria-label="SAGE-RT walkthrough"><span className="brandmark" aria-hidden="true">S<span>↗</span></span><span><strong>SAGE<span className="brand-dash">—</span>RT</strong><small>DECISION WORKSTATION</small></span></a>
      <nav aria-label="Workspaces">{(['lab', 'configure', 'inspect', 'compare'] as Page[]).map((item, i) => <button key={item} className={page === item ? 'nav-item active' : 'nav-item'} onClick={() => setPage(item)} aria-current={page === item ? 'page' : undefined}><span className="nav-number">0{i}</span>{item === 'lab' ? 'Walkthrough' : item === 'compare' ? 'Compare & verify' : item[0].toUpperCase() + item.slice(1)}</button>)}</nav>
      <span className={`connection ${health ? '' : 'disconnected'}`}><i />{health ? 'LOCAL BRIDGE CONNECTED' : 'BRIDGE OFFLINE'}</span>
    </header>
    <div className="context-bar"><span><span className="context-label">ENVIRONMENT</span>Host-PC simulation <span className="separator">/</span> C11 · POSIX</span><span className="mono">{run ? `CAPTURE ${run.id.slice(0, 8)}` : 'NO ACTIVE CAPTURE'}<span className="separator">/</span>Reference replay</span></div>
    {error && <div className="alert" role="alert"><strong>Operation failed.</strong> {error}<button onClick={() => setError('')} aria-label="Dismiss error">×</button></div>}
    {health && !health.engineReady && <div className="notice"><strong>Observation runner not built.</strong> In WSL Ubuntu, run <code>make workstation</code> from this checkout. Configuration and checked-in comparison remain available.</div>}
    <main>
      {page === 'lab' && <SchedulerExplorer scenarios={scenarios} scenarioId={scenarioId} variant={variant} choose={(nextScenario, nextVariant) => { setScenarioId(nextScenario); setVariant(nextVariant); if (run && health?.engineReady && !busy) void executeFor(nextScenario, nextVariant, 'lab'); }} run={run} execute={execute} busy={busy} ready={!!health?.engineReady} taskId={taskId} move={move} select={selectTask} guide={() => setPage('mission')} />}
      {page === 'mission' && <MissionMap configure={(scenario = 'false_alarm_restoration') => { setScenarioId(scenario); setVariant('full'); setPage('configure'); }} />}
      {page === 'configure' && <Configure scenarios={scenarios} scenario={scenario} selected={scenarioId} select={setScenarioId} variant={variant} setVariant={setVariant} execute={execute} busy={busy} ready={!!health?.engineReady} learn={() => setPage('mission')} />}
      {page === 'inspect' && (run ? <Inspector run={run} cursor={cursor} move={move} taskId={taskId} setTaskId={selectTask} playing={playing} setPlaying={setPlaying} speed={speed} setSpeed={setSpeed} step={step} setStep={explainStep} /> : <Empty title="A decision needs a run.">Choose a scenario in Configure and generate a reference replay. This workspace will show the engine’s actual decisions, resource reservations and task transitions.<br /><button className="primary" onClick={() => setPage('configure')}>Configure an experiment →</button></Empty>)}
      {page === 'compare' && <Compare metrics={metrics} />}
    </main>
    <footer><span>SAGE-RT / Safety-Assured, Gridless Energy-Aware Real-Time Scheduler</span><span>Virtual time · One-shot tasks · Scripted telemetry</span></footer>
  </div>;
}

function Configure({ scenarios, scenario, selected, select, variant, setVariant, execute, busy, ready, learn }: { scenarios: Scenario[]; scenario?: Scenario; selected: string; select: (id: string) => void; variant: Variant; setVariant: (v: Variant) => void; execute: () => void; busy: boolean; ready: boolean; learn: () => void }) {
  const p = policy[variant];
  return <>
    <div className="page-heading"><div><div className="eyebrow">01 / EXPERIMENT SETUP</div><h1>Decisions before dispatch.</h1><p>Choose the conditions. Inspect what survives the safety Gate.</p></div><span className="heading-note">ENGINEERING LABORATORY<br /><strong>REFERENCE REPLAY / VIRTUAL CLOCK</strong></span></div>
    <section className="mission-entry" aria-label="Start the project walkthrough"><div><h2>Learn scheduling, one drawing at a time.</h2><p>Start with limited energy. Then learn resource sharing, pausing and resuming.<button className="text-button" onClick={() => { select('false_alarm_restoration'); setVariant('full'); }}>Choose the recommended demo</button></p><div className="lesson-shortcuts"><button onClick={() => { select('low_soc_eclipse'); setVariant('full'); }}>1 · Fit a smaller mode</button><button onClick={() => { select('competing_payload_tasks'); setVariant('full'); }}>2 · Share CPU between jobs</button><button onClick={() => { select('false_alarm_restoration'); setVariant('full'); }}>3 · Pause and resume safely</button></div></div><button className="secondary" onClick={learn}>Explore the mission map ↗</button></section>
    <div className="setup-grid">
      <section className="scenario-list"><PanelTitle eyebrow="INPUT CATALOG" title="Scenario fixtures" extra={<span className="count">{scenarios.length || '—'}</span>} />
        <div className="scenario-options">{scenarios.map((s, i) => <button key={s.id} onClick={() => select(s.id)} className={`scenario-option ${selected === s.id ? 'selected' : ''}`} aria-pressed={selected === s.id}><span className="scenario-index">{String(i + 1).padStart(2, '0')}</span><span><strong>{titles[s.id] ?? s.name}</strong><small>{s.tasks.length} task{s.tasks.length !== 1 ? 's' : ''} · {s.duration} sim ns{s.id === 'prediction_trend' ? ' · A/B fixture' : ''}</small></span><span className="option-arrow">→</span></button>)}</div>
        <p className="list-note">Checked-in inputs are read-only.<br />Every run uses the C scenario parser.</p>
      </section>
      <section className="scenario-detail">
        {scenario ? <><PanelTitle eyebrow="SELECTED INPUT" title={titles[scenario.id] ?? scenario.name} extra={<span className="file-label">.scn</span>} /><p className="scenario-description">{descriptions[scenario.id]}</p>
          <div className="spec-strip"><div><small>DURATION</small><strong>{scenario.duration}<span> sim ns</span></strong></div><div><small>TICK STEP</small><strong>{scenario.step}<span> sim ns</span></strong></div><div><small>SOC FLOOR</small><strong>{Number(scenario.socFloor) / 10}<span>%</span></strong></div><div><small>BATTERY CAPACITY</small><strong>{number(scenario.batteryCapacity)}<span> mJ</span></strong></div></div>
          <ScenarioBrief scenario={scenario} />
          <div className="section-heading"><h3>Task & mission model</h3><span className="muted">One-shot execution</span></div>
          <div className="table-scroll"><table><thead><tr><th>Task / subsystem</th><th>Class</th><th>Execution</th><th>Deadline</th><th>Value</th></tr></thead><tbody>{scenario.tasks.map(task => <tr key={task.id}><td><strong className="mono">T{task.id}</strong><span className="subsystem">{task.subsystem}</span></td><td><span className={task.criticality === '0' ? 'critical-label' : 'mono'}>{task.criticality === '0' ? '0 · critical' : task.criticality}</span></td><td className="mono">{task.duration} ns</td><td className="mono">{task.deadline} ns</td><td className="mono">{task.value}</td></tr>)}</tbody></table></div>
          <div className="section-heading"><h3>Scheduled input changes</h3><span className="muted">{scenario.events.length} directives</span></div>
          {scenario.events.length ? <div className="input-events">{scenario.events.map((event, i) => <div key={i}><span className="event-marker" /><span className="mono event-time">{event.type === 'ORBIT' ? 'epoch' : 't'} = {event.time}</span><span className="event-type">{event.type}</span><code>{describeEvent(event)}</code></div>)}</div> : <p className="muted inset">No explicit event directives in this fixture.</p>}
          {scenario.id === 'compound_stress' && <p className="technical-note">This 12 ns fixture demonstrates shedding and Emergency Safe Mode. The critical task needs 20 ns; critical completion is not its exit condition.</p>}
          <details className="source-details"><summary>Inspect scenario source <span className="mono">{scenario.id}.scn</span></summary><pre>{scenario.raw}</pre></details>
        </> : <Empty title="Loading inputs">Reading the local scenario catalog…</Empty>}
      </section>
      <aside className="run-setup"><PanelTitle eyebrow="ENGINE CONFIGURATION" title="Execution policy" />
        <fieldset><legend>Variant</legend>{(Object.keys(policy) as Variant[]).map(v => <label key={v} className={`variant-option ${variant === v ? 'chosen' : ''}`}><input type="radio" name="variant" value={v} checked={variant === v} onChange={() => setVariant(v)} /><span><strong>{policy[v].name}</strong><small>{v === 'full' ? 'Complete decision policy' : 'Same-engine feature ablation'}</small></span></label>)}</fieldset>
        <div className="policy-description"><h3>{p.name}</h3><p>{p.description}</p><dl><dt>Safety Gate</dt><dd>{p.gate}</dd><dt>Forecast input</dt><dd>{p.prediction}</dd><dt>Event cadence</dt><dd>{p.fault}</dd></dl></div>
        <div className="reference-note"><span className="eyebrow">RUN CONTRACT</span><p>Reproducible reference replay. Governor self-cost is <strong>zero</strong>, the CPU cap is <strong>nonbinding</strong>, and task protected reserves are <strong>zero</strong>.</p><p>Live CPU measurements belong to the calibrated report, not this trace.</p></div>
        <button className="primary run-button" disabled={!scenario || !ready || busy} onClick={execute}><Arrow>{busy ? 'Executing C replay…' : 'Generate reference run'}</Arrow></button><p className="run-footnote">Runs the C engine locally.<br />Playback controls inspect the resulting capture.</p>
      </aside>
    </div>
    <div className="architecture-strip"><span className="eyebrow">DECISION PATH</span>{['Telemetry', 'Prediction', 'Candidates', 'Safety Gate', 'Utility', 'Commit', 'Revalidate'].map((stage, i) => <React.Fragment key={stage}>{i > 0 && <span className="path-arrow">→</span>}<span className={stage === 'Safety Gate' ? 'gate-stage' : ''}>{stage}</span></React.Fragment>)}</div>
  </>;
}

function Inspector({ run, cursor, move, taskId, setTaskId, playing, setPlaying, speed, setSpeed, step, setStep }: { run: Run; cursor: number; move: (i: number) => void; taskId: string; setTaskId: (id: string) => void; playing: boolean; setPlaying: (v: boolean) => void; speed: number; setSpeed: (v: number) => void; step: number; setStep: (index: number) => void }) {
  const [presentation, setPresentation] = useState<'lab' | 'evidence'>('lab');
  const final = cursor === run.cycles.length;
  const cycle = final ? null : run.cycles[cursor];
  const states = final ? run.final.states : cycle!.states;
  const time = final ? run.final.time : cycle!.time;
  const task = run.scenario.tasks.find(t => t.id === taskId);
  const decision = cycle?.decisions.find(d => d.task === taskId);
  const state = states.find(s => s.task === taskId);
  const assumption = cycle?.assumptions.find(a => a.task === taskId);
  function nextEvent() { const index = run.cycles.findIndex((c, i) => i > cursor && significant(c)); move(index < 0 ? run.cycles.length : index); }
  function bookmark(kind: 'risk' | 'restore' | 'fault' | 'complete') { const i = run.cycles.findIndex(c => kind === 'fault' ? c.trigger === 'FAULT' : c.decisions.some(d => kind === 'risk' ? d.invalidation?.cause === 'PREDICTED_RISK' : kind === 'restore' ? d.restoration : d.before !== 'COMPLETE' && d.after === 'COMPLETE')); if (i >= 0) move(i); }
  return <>
    <div className="page-heading compact"><div><span className="eyebrow">02 / DECISION INSPECTION</span><h1>{titles[run.scenario.id] ?? run.scenario.name}</h1><p>{policy[run.variant].name} <span className="separator">/</span> Actual C-engine capture <span className="separator">/</span> Reference replay</p></div><button className="secondary" onClick={() => download(`${run.scenario.id}-${run.variant}.json`, JSON.stringify(run, null, 2))}><Arrow>Export capture</Arrow></button></div>
    <div className="inspector-presentation" role="group" aria-label="Inspector presentation"><button aria-pressed={presentation === 'lab'} onClick={() => { setPlaying(false); setPresentation('lab'); }}>Interactive lab</button><button aria-pressed={presentation === 'evidence'} onClick={() => { setPlaying(false); setPresentation('evidence'); }}>Evidence & prior views</button></div>
    {presentation === 'lab' ? <SchedulingLab run={run} taskId={taskId} move={move} select={setTaskId} /> : <>
    <div className="playback"><div className="playback-buttons"><button aria-label="Previous cycle" disabled={cursor === 0} onClick={() => move(cursor - 1)}>←</button><button className="play-button" onClick={() => { if (final) move(0); setPlaying(!playing); }}>{playing ? 'Pause walkthrough' : 'Play walkthrough'}</button><button aria-label="Next cycle" disabled={final} onClick={() => move(cursor + 1)}>→</button></div><label className="cursor-slider"><span className="mono">{final ? 'FINAL' : `CYCLE ${cycle!.sequence}`}</span><input aria-label="Inspection cursor" type="range" min={0} max={run.cycles.length} value={cursor} onChange={e => move(Number(e.target.value))} /></label><label className="speed-label">Per step<select aria-label="Playback speed" value={speed} onChange={e => setSpeed(Number(e.target.value))}><option value={8000}>Read along (8s)</option><option value={3000}>Slow</option><option value={1500}>Normal</option><option value={500}>Fast</option></select></label><button className="text-button" onClick={nextEvent} disabled={final}>Next event ↗</button><span className="time-readout">t = <strong>{time}</strong> sim ns</span></div>
    <div className="inspection-notes"><span><i className="status-square" />{final ? 'FINAL EXECUTION SETTLEMENT' : `${cycle!.trigger} / ${cycle!.result}`}</span><span>{cycle?.emergency ? <Badge value="EMERGENCY_SAFE_MODE" /> : 'Captured state · engine is not running during playback'}</span><div className="bookmarks">{(['risk', 'restore', 'fault', 'complete'] as const).map(kind => <button key={kind} onClick={() => bookmark(kind)} disabled={!run.cycles.some(c => kind === 'fault' ? c.trigger === 'FAULT' : c.decisions.some(d => kind === 'risk' ? d.invalidation?.cause === 'PREDICTED_RISK' : kind === 'restore' ? d.restoration : d.before !== 'COMPLETE' && d.after === 'COMPLETE'))}>{kind === 'risk' ? 'Predicted risk' : kind === 'restore' ? 'Restoration' : kind === 'fault' ? 'Fault cycle' : 'Completion'}</button>)}</div></div>
    <Walkthrough run={run} cursor={cursor} move={move} taskId={taskId} select={setTaskId} step={step} setStep={setStep} playing={playing} />
    </>}
    <details className="technical-evidence"><summary>Detailed decision evidence <span>Cycle snapshots · Gate table · Ledger · telemetry · raw record</span></summary>
    <section className="timeline-panel"><PanelTitle eyebrow="EXECUTION STATE" title="Task lifecycles" extra={<span className="muted">Each cell is one decision cycle, not equal elapsed time</span>} /><div className="timeline-scroll"><div className="timeline"><div className="timeline-label">TASK / CLASS</div><div className="timeline-cells timeline-times">{run.cycles.map((c, i) => <button key={c.sequence} className={cursor === i ? 'time-cell current' : 'time-cell'} onClick={() => move(i)} title={`Cycle ${c.sequence}, t=${c.time}, ${c.trigger}`}>{c.time}{c.trigger === 'FAULT' && <span>F</span>}</button>)}<button className={final ? 'time-cell current' : 'time-cell'} onClick={() => move(run.cycles.length)}>end</button></div>{run.scenario.tasks.map(t => <React.Fragment key={t.id}><button className={`timeline-label task-select ${taskId === t.id ? 'selected-task' : ''}`} onClick={() => setTaskId(t.id)}><strong>T{t.id}</strong><span>{t.subsystem}<small>CLASS {t.criticality}</small></span></button><div className="timeline-cells">{run.cycles.map((c, i) => { const s = c.states.find(s => s.task === t.id); return <button key={c.sequence} onClick={() => { setTaskId(t.id); move(i); }} className={`state-cell ${s?.phase.toLowerCase()} ${i === cursor && t.id === taskId ? 'selected-cell' : ''}`} title={`Task ${t.id}: ${s?.phase}, ${s?.remaining} ns remaining, cycle ${c.sequence}`} aria-label={`Task ${t.id}, cycle ${c.sequence}, ${s?.phase}`}>{s?.phase === 'RUNNING' ? 'R' : s?.phase === 'PAUSED' ? 'Ⅱ' : s?.phase === 'COMPLETE' ? '✓' : s?.phase === 'REJECTED' ? '×' : '·'}</button>; })}<button onClick={() => { setTaskId(t.id); move(run.cycles.length); }} className={`state-cell ${run.final.states.find(s => s.task === t.id)?.phase.toLowerCase()} ${final && t.id === taskId ? 'selected-cell' : ''}`} aria-label={`Task ${t.id} final state`}>{run.final.states.find(s => s.task === t.id)?.phase === 'COMPLETE' ? '✓' : '·'}</button></div></React.Fragment>)}</div></div><div className="timeline-legend">{['PENDING', 'RUNNING', 'PAUSED', 'COMPLETE', 'REJECTED'].map(value => <span key={value}><i className={value.toLowerCase()} />{label(value)}</span>)}<span className="muted">F = fault-triggered cycle</span></div></section>
    <div className="inspect-grid"><section className="decision-panel"><PanelTitle eyebrow="GATE → RANK → COMMIT" title={`Task ${taskId} · candidate inspection`} extra={state && <Badge value={state.phase} />} />
      {decision?.candidates.length ? <><div className="table-scroll"><table className="candidates-table"><thead><tr><th>Mode</th><th>Gate verdict</th><th>First failure / role</th><th>Energy</th><th>Value</th></tr></thead><tbody>{decision.candidates.map(c => { const profile = task?.profiles.find(p => p.mode === c.mode); return <tr key={c.mode} className={decision.selected === c.mode ? 'selected-mode' : ''}><td className="mono"><strong>{c.mode}</strong>{decision.selected === c.mode && <span className="selection-arrow">← selected</span>}</td><td><Badge value={c.verdict} /></td><td>{c.predicate !== 'NONE' ? label(c.predicate.replace('GATE_', '')) : c.verdict === 'FALLBACK_ONLY' ? 'Not ranked' : c.verdict === 'DISABLED_BY_VARIANT' ? 'Variant disables this mode' : 'No failure recorded'}{c.skipMask !== '0x00' && <small className="skip-mask">Skipped predicate mask: {c.skipMask}</small>}</td><td className="mono">{profile ? `${number(profile.energy)} mJ` : '0'}</td><td className="mono">{profile && task ? number((BigInt(task.value) * BigInt(profile.fraction) / 1000n).toString()) : '—'}</td></tr>; })}</tbody></table></div><p className="table-caption">Only surviving executing modes reach Utility. A failed Gate reports the first failure; subsequent predicate results are not emitted.</p></> : <div className="no-candidates"><strong>{final ? 'Execution settled at scenario end.' : 'No new candidates evaluated.'}</strong><p>{final ? 'This snapshot includes completion during a final partial tick. It does not introduce another admission cycle.' : 'The selected task retained or changed its execution state without a new candidate evaluation in this record.'}</p></div>}
      <div className="selection-summary"><div><span className="eyebrow">SELECTED MODE</span><strong className="mono">{decision?.selected ?? '—'}</strong></div><div><span className="eyebrow">SELECTED UTILITY</span><strong className="mono">{decision?.score ?? 'Not emitted'}</strong></div><div><span className="eyebrow">REMAINING WORK</span><strong className="mono">{state?.remaining ?? '—'} <small>sim ns</small></strong></div></div>
    </section><aside className="reason-panel"><PanelTitle eyebrow="DECISION RATIONALE" title="Why this state?" /><h3 className="reason-title">{decision ? label(decision.reason) : final ? 'Final execution state' : 'No decision emitted'}</h3><p>{explain(decision, final)}</p>
      <dl className="state-details"><dt>Subsystem</dt><dd>{task?.subsystem}</dd><dt>Criticality</dt><dd>Class {task?.criticality}{task?.criticality === '0' ? ' · safety-critical' : ''}</dd><dt>Work delivered</dt><dd>{state?.value ?? '—'} / {task?.value ?? '—'}</dd><dt>Completion instant</dt><dd>{state?.completed ? `${state.completed} sim ns` : 'Not completed'}</dd></dl>
      {decision?.invalidation && <div className="invalidation-detail"><span className="eyebrow">{decision.restoration ? 'RESTORED COMMITMENT' : 'INVALIDATION RECORD'}</span><dl><dt>Cause</dt><dd>{label(decision.invalidation.cause)}</dd><dt>Trigger</dt><dd>{label(decision.invalidation.quantity)}</dd><dt>Invalidated at</dt><dd>{decision.invalidation.time} ns</dd><dt>Original mode</dt><dd>{decision.invalidation.originalMode}</dd><dt>{decision.invalidation.cause === 'PREDICTED_RISK' ? 'Risk horizon' : 'Clear ticks'}</dt><dd>{decision.invalidation.cause === 'PREDICTED_RISK' ? `${decision.invalidation.horizon} ns` : `${decision.invalidation.clearCount} / ${decision.invalidation.requiredClear}`}</dd></dl></div>}
      {assumption && <div className="assumption-detail"><span className="eyebrow">STORED COMMITMENT ASSUMPTION</span><dl><dt>Committed at</dt><dd>{assumption.commit_ns} ns</dd><dt>Forecast SOC</dt><dd>{Number(assumption.soc) / 10}%</dd><dt>Forecast horizon</dt><dd>{assumption.horizon_ns} ns</dd></dl><small>This is the stored snapshot, not every candidate’s effective Gate input.</small></div>}
    </aside></div>
    <div className="observation-grid"><section className="ledger-panel"><PanelTitle eyebrow="JOINT RESOURCE FEASIBILITY" title="Mission Safety Resource Ledger" extra={<span className="muted">Before → after this task’s decision</span>} />{decision ? <LedgerTable before={decision.ledgerBefore} after={decision.ledgerAfter} /> : <p className="inset muted">No new decision Ledger snapshot is emitted at final execution settlement.</p>}<p className="table-caption">Reserve = class-0 task pool. Governor self-cost is zero in this reference replay. Sensor values are masks; scalar residuals may be negative.</p></section><section className="telemetry-panel"><PanelTitle eyebrow="OBSERVED INPUT" title="Telemetry at the cycle" extra={<span className="muted">{cycle ? 'Engine-observed frame' : 'No new observation'}</span>} />{cycle ? <Telemetry channels={cycle.telemetry} /> : <p className="inset muted">Final settlement advances execution without generating a new telemetry sample.</p>}</section></div>
    <details className="source-details raw-record"><summary>Raw decision & capture provenance <span className="mono">{run.id.slice(0, 8)}</span></summary><dl className="provenance"><dt>Scenario SHA-256</dt><dd>{run.provenance.scenarioHash}</dd><dt>Executable SHA-256</dt><dd>{run.provenance.executableHash}</dd><dt>Replay contract</dt><dd>Virtual clock / zero self-cost / nonbinding cap / zero task reserve</dd></dl><pre>{decision?.raw ?? 'No DECISION record at this inspection point.'}</pre><button className="secondary" onClick={() => download(`${run.scenario.id}.trace.txt`, run.raw, 'text/plain')}>Export raw observation stream</button></details>
    </details>
  </>;
}
function explain(d: Decision | undefined, final: boolean) {
  if (final) return 'The scenario has ended. These states come directly from the engine after final execution settlement.';
  if (!d) return 'The engine did not publish a decision for this task at this point.';
  if (d.restoration) return 'The paused task re-entered candidate generation and passed the Gate again. Execution resumes from its remaining work, rather than restarting.';
  if (d.invalidation?.cause === 'PREDICTED_RISK') return 'A fresh forecast predicts a future safety failure. The governor pauses the commitment while preserving completed progress.';
  if (d.reason === 'EMERGENCY_SAFE_MODE') return 'The critical task cannot safely dispatch or resume. Emergency Safe Mode blocks new dispatch while preserving established safe commitments.';
  if (d.invalidation) return 'An observed fault or deviation invalidates the commitment. Rate and stock resources are released; energy holds follow the engine’s freshness rule.';
  if (d.reason === 'SELECTED') return 'The governor eliminated unsafe candidates first, then selected a surviving mode through Utility and reserved its resources.';
  if (d.reason === 'COMPLETED') return 'The simulated execution finished. Delivered value is credited once and rate/stock resources are released.';
  if (d.reason === 'DEFERRED') return 'No executing mode survived admission. The deadline still permits a later execution window, so the task remains pending without a reservation.';
  if (d.reason === 'REJECTED') return 'No safe executing mode can be admitted within the remaining execution window. Rejection is counted separately from deadline misses.';
  if (d.reason === 'RESTORE_BLOCKED_BUDGET') return 'Restoration is waiting for the held energy reservation to clear through a fresh SOC-budget update.';
  if (d.reason === 'RESTORE_BLOCKED_GATE') return 'Restoration was considered but did not produce a Gate-approved executable commitment.';
  return 'The commitment remains in its captured state. Paused time earns no execution progress; running time advances the remaining work.';
}
function LedgerTable({ before, after }: { before: Ledger[]; after: Ledger[] }) {
  const units: Record<string, string> = { CPU: '%', ENERGY: 'mJ', MEMORY: 'KB', STORAGE: 'KB', SENSOR: 'mask', COMM: '%' };
  return <div className="table-scroll"><table className="ledger-table"><thead><tr><th>Dimension</th><th>Capacity</th><th>Reserve</th><th>Committed</th><th>Free</th></tr></thead><tbody>{after.map((a, i) => <tr key={a.dimension}><td><strong className="mono">{a.dimension}</strong><small>{units[a.dimension]}</small></td>{(['capacity', 'reserve', 'committed', 'free'] as const).map(key => <td key={key} className={`mono ${a.dimension !== 'SENSOR' && BigInt(a[key]) < 0 ? 'negative' : ''}`}>{before[i][key] !== a[key] && <span className="previous-value">{number(before[i][key])} → </span>}{number(a[key])}</td>)}</tr>)}</tbody></table></div>;
}
function Telemetry({ channels }: { channels: Channel[] }) {
  const [all, setAll] = useState(false);
  const important = ['BATTERY_SOC_MV_PCT', 'CPU_AVAIL_PCT', 'COMM_WINDOW_OPEN', 'FAULT_FLAGS', 'TEMP_MC[2]', 'TEMP_MC[3]'];
  function format(c: Channel) { if (c.name === 'BATTERY_SOC_MV_PCT') return `${Number(c.value) / 10}%`; if (c.name.startsWith('TEMP_MC')) return `${(Number(c.value) / 1000).toFixed(2)} °C`; if (c.name === 'FAULT_FLAGS') return `0x${BigInt(c.value).toString(16).padStart(8, '0')}`; return number(c.value); }
  return <><div className="table-scroll"><table className="telemetry-table"><thead><tr><th>Channel</th><th>Value</th><th>Source t</th><th>Quality</th></tr></thead><tbody>{channels.filter(c => all || important.includes(c.name)).map(c => <tr key={c.name}><td className="mono">{c.name.replace('BATTERY_SOC_MV_PCT', 'BATTERY_SOC')}</td><td className="mono">{format(c)}</td><td className="mono">{c.timestamp}</td><td><span className={c.quality === 'OK' ? 'quality-ok' : 'negative'}>{c.quality}</span></td></tr>)}</tbody></table></div><button className="text-button inset-button" onClick={() => setAll(!all)}>{all ? 'Show primary channels' : 'Inspect all 15 channels'} ↗</button><p className="table-caption">Source timestamps are independent of the cycle time. Quality comes from the engine; the UI does not invent freshness thresholds.</p></>;
}

function Compare({ metrics }: { metrics: Metric[] }) {
  const [filter, setFilter] = useState('compound_stress');
  const visible = metrics.filter(row => filter === 'all' || row.scenario === filter);
  const [expanded, setExpanded] = useState(false);
  return <><div className="page-heading"><div><span className="eyebrow">03 / COMPARISON & EVIDENCE</span><h1>Safety, with the tradeoffs visible.</h1><p>Eight fixtures. Four policies. The same engine and independent full-invariant audit.</p></div><span className="heading-note">EVIDENCE SOURCE<br /><strong>CHECKED-IN PHASE 5 REPORT</strong></span></div>
    <div className="comparison-toolbar"><label>Scenario<select value={filter} onChange={e => setFilter(e.target.value)}><option value="all">All eight scenarios</option>{Array.from(new Set(metrics.map(m => m.scenario))).map(id => <option key={id} value={id!}>{titles[id!] ?? id}</option>)}</select></label><label className="checkbox-label"><input type="checkbox" checked={expanded} onChange={e => setExpanded(e.target.checked)} />Include host timing & counts</label><span className="muted">{visible.length} / {metrics.length} rows · calibrated virtual replay</span></div>
    <section className="comparison-panel"><div className="table-scroll"><table className="comparison-table"><thead><tr>{filter === 'all' && <th>Scenario</th>}<th>Variant</th><th>Completed</th><th>Mission value</th><th>Safety failures / observations</th><th>Protected failures</th><th>Recovery</th><th>Replans</th>{expanded && <><th>Deadline misses</th><th>Rejected</th><th>CPU total / ns</th><th>Cycle cap / ns</th><th>Self energy / mJ*</th></>}</tr></thead><tbody>{visible.map(row => <tr key={`${row.scenario}-${row.variant}`} className={row.variant === 'SAGE_FULL' ? 'full-row' : ''}>{filter === 'all' && <td>{titles[row.scenario!]}</td>}<td><strong>{row.variant === 'SAGE_FULL' ? 'SAGE-RT full' : row.variant === 'FIXED_EDF' ? 'Fixed / EDF' : row.variant === 'GOMX_STYLE' ? 'GomX style' : 'Ranjbar style'}</strong></td><td className="mono">{row.completed_count} / {row.task_count}</td><td className="mono">{number(row.mission_work_completed)}</td><td className="mono"><span className={row.safety_violations !== '0' ? 'negative' : ''}>{row.safety_violations}</span> / {row.safety_task_instants}</td><td className="mono">{row.protected_resource_violations}</td><td className="mono">{row.recovery_latency_censored === '1' ? '≥ ' : ''}{row.recovery_latency_ns} ns{row.recovery_latency_censored === '1' && <small>Censored · no transition observed</small>}</td><td className="mono">{row.replan_count}</td>{expanded && <><td className="mono">{row.deadline_misses}</td><td className="mono">{row.rejected_by_design}</td><td className="mono">{number(row.cpu_time_ns)}</td><td className="mono">{number(row.self_cost_cap_ns)}</td><td className="mono">{row.modeled_self_energy_mJ}</td></>}</tr>)}</tbody></table></div><p className="table-caption">Safety failures count failing task-predicate observations. Several predicates may fail at one task-instant; the ratio is not a probability.</p></section>
    <div className="evidence-grid"><section className="evidence-reading"><PanelTitle eyebrow="INTERPRETATION" title="What this evidence establishes" /><p>SAGE-RT’s full policy records zero audited task-safety violations in the checked-in matrix. Some baselines deliver more mission value while violating a safety predicate.</p><p>Compound stress ties SAGE-RT and Ranjbar on safety. Critical completion is 0/1 for all variants: the task needs 20 ns and the fixture ends at 12 ns. Strict dominance was not observed.</p><p className="technical-note">Battery telemetry is scripted; energy survival is not a measured scheduling benefit. *Self energy assumes 2.0 W. CPU total covers the run, while the cap applies to one cycle.</p></section><section className="evidence-reading"><PanelTitle eyebrow="VERIFICATION BOUNDARY" title="A report is not a test run" /><p>This table reads <code>docs/phase5_metrics.csv</code>. It does not imply that tests have run in this browser session.</p><dl className="verification-list"><dt>Unit / golden / traceability</dt><dd>Not run in this session</dd><dt>ASan / UBSan</dt><dd>Not run in this session</dd><dt>Measured live timing</dt><dd>Not captured in reference replay</dd></dl><p>Run <code>make test</code> and <code>make sanitize</code> in WSL to verify the C implementation. The replay inspector exports the actual capture for review.</p></section></div>
    <details className="source-details"><summary>Variant policy definitions <span>Same-engine ablations</span></summary><div className="table-scroll"><table><thead><tr><th>Variant</th><th>Modes</th><th>Gate</th><th>Fault cadence</th></tr></thead><tbody>{(Object.keys(policy) as Variant[]).map(v => <tr key={v}><td>{policy[v].name}</td><td>{policy[v].modes}</td><td>{policy[v].gate}</td><td>{policy[v].fault}</td></tr>)}</tbody></table></div></details>
  </>;
}

createRoot(document.getElementById('root')!).render(<React.StrictMode><App /></React.StrictMode>);
