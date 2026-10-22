import type { Run, Scenario, Variant } from './domain';
import { download, policy, titles } from './domain';
import { SchedulingLab } from './SchedulingLab';
import { labStages } from './lab-model';

interface Props {
  scenarios: Scenario[];
  scenarioId: string;
  variant: Variant;
  choose: (scenario: string, variant: Variant) => void;
  run: Run | null;
  execute: () => void;
  busy: boolean;
  ready: boolean;
  taskId: string;
  move: (index: number) => void;
  select: (id: string) => void;
  guide: () => void;
}
export function SchedulerExplorer({ scenarios, scenarioId, variant, choose, run, execute, busy, ready, taskId, move, select, guide }: Props) {
  const scenario = scenarios.find(s => s.id === scenarioId);
  const capture = run?.scenario.id === scenarioId && run.variant === variant ? run : null;
  return <div className="scheduler-explorer">
    <section className="sl-hero"><div><span className="eyebrow">OPERATING SYSTEMS, MADE VISIBLE</span><h1>A spacecraft has work.<br /><span>Its scheduler makes the choices.</span></h1><p>What happens when jobs compete for CPU, the battery is low, or a fault arrives? Follow one experiment from its inputs to the decisions and the work that actually finishes.</p><div className="sl-hero-route"><span>01 Set the situation</span><b>→</b><span>02 Follow the decisions</span><b>→</b><span>03 Watch the jobs</span></div></div><aside><span className="eyebrow">THE BIG IDEA</span><h2>A scheduler is a planner<br />with limited supplies.</h2><p>It decides which jobs can work, how they may run, and what to reserve. When a promise stops being safe, it must reconsider.</p><small>Actual C backend · simulated spacecraft inputs</small></aside></section>
    <div className="sl-launcher"><div><span className="eyebrow">TRY AN EXPERIMENT</span><h2>Inside SAGE-RT</h2></div><div className="sl-experiment-actions"><label>Choose a situation<select aria-label="Lab scenario" disabled={busy} value={scenarioId} onChange={e => choose(e.target.value, variant)}>{scenarios.map(s => <option key={s.id} value={s.id}>{titles[s.id] ?? s.name}</option>)}</select></label><label>Decision policy<select aria-label="Lab decision policy" disabled={busy} value={variant} onChange={e => choose(scenarioId, e.target.value as Variant)}>{Object.entries(policy).map(([id, p]) => <option key={id} value={id}>{p.name}</option>)}</select></label><button className="sl-run" disabled={!scenario || !ready || busy} onClick={execute}>{busy ? 'Running C engine…' : 'Run & explore'} <span>↗</span></button></div></div>
    <div className="sl-run-notice" role="status"><span>{busy ? 'Generating a new capture. The diagrams will use its published C records.' : capture ? `Capture ${capture.id.slice(0, 8)} · ${capture.cycles.length} decision cycles · choose a stage or press Play.` : 'Choose a situation and run it once. Then explore every stage without rerunning the engine.'}</span>{capture && <button onClick={() => download(`${capture.scenario.id}-${capture.variant}.json`, JSON.stringify(capture, null, 2))}>↓ Download run evidence</button>}<button onClick={guide}>Mission map & OS concepts ↗</button></div>
    {capture ? <SchedulingLab key={capture.id} run={capture} taskId={taskId} move={move} select={select} /> : <section className="sl-awaiting" aria-label="Scheduler lab ready to run"><div className="sl-awaiting-overview">{labStages.map((stage, i) => <span key={stage.name}><small>0{i + 1}</small>{stage.short}</span>)}</div><div className="sl-awaiting-grid"><div><span className="eyebrow">YOUR VISUAL WORKSPACE</span><h2>{scenario ? titles[scenario.id] : 'Loading experiments…'}</h2><p>Run the C engine to reveal sensor frames, execution profiles, candidate checks, allocations and chronological job states here.</p><button className="sl-run" disabled={!scenario || !ready || busy} onClick={execute}>{busy ? 'Running…' : 'Run & explore'} <span>→</span></button><small>No results are invented before a capture exists.</small></div><aside><span className="eyebrow">SCENARIO INPUT</span><pre>{scenario?.raw ?? 'Loading source…'}</pre></aside></div></section>}
    <p className="sl-explorer-boundary">Host-PC simulation · one-shot jobs · scripted telemetry. Playback shows recorded evidence, not a live spacecraft, individual C call timings, or Linux thread assignments.</p>
  </div>;
}
