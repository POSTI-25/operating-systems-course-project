import { useId } from 'react';
import { label } from './domain';
import type { Run, Task } from './domain';
import { bounded, executionSegments, phaseColors, ratio, remainingPoints } from './walkthrough-model';

const left = 54, top = 24, width = 590, height = 135;
function x(time: string, duration: string) { return left + bounded(ratio(time, duration)) * width / 100; }
function timeTicks(duration: string) { return [...new Set(Array.from({ length: 5 }, (_, i) => (BigInt(duration) * BigInt(i) / 4n).toString()))]; }
function Axis({ duration, now }: { duration: string; now: string }) {
  return <>{timeTicks(duration).map(time => <g key={time}><line x1={x(time, duration)} x2={x(time, duration)} y1={top} y2={top + height} className="plot-grid" /><text x={x(time, duration)} y={top + height + 21} textAnchor="middle">{time}</text></g>)}<line x1={x(now, duration)} x2={x(now, duration)} y1={top - 4} y2={top + height} className="plot-cursor" /><text x={left + width} y={top + height + 39} textAnchor="end">simulation time / ns</text></>;
}

export function HistoryPlots({ run, cursor, task }: { run: Run; cursor: number; task?: Task }) {
  const id = useId().replaceAll(':', '');
  const now = run.cycles[cursor]?.time ?? run.final.time;
  const readings = run.cycles.map(cycle => {
    const channel = cycle.telemetry.find(c => c.name === 'BATTERY_SOC_MV_PCT');
    return channel?.quality === 'OK' ? { time: cycle.time, value: Number(channel.value) / 10 } : null;
  });
  const observations = readings.filter(reading => reading != null);
  const floor = Number(run.scenario.socFloor) / 10;
  const min = Math.max(0, Math.floor(Math.min(floor, ...observations.map(p => p.value)) - 2));
  const max = Math.min(100, Math.max(min + 1, Math.ceil(Math.max(floor, ...observations.map(p => p.value)) + 2)));
  const y = (value: number) => top + height - (value - min) / Math.max(1, max - min) * height;
  const batteryPath = readings.map((point, index) => !point ? '' : `${index && readings[index - 1] ? `H${x(point.time, run.scenario.duration)} V` : `M${x(point.time, run.scenario.duration)},`}${y(point.value)}`).join(' ');
  const points = task ? remainingPoints(run, task).filter(p => p.phase !== 'REJECTED') : [];
  const workY = (value: string) => top + height - bounded(ratio(value, task?.duration ?? '1')) * height / 100;
  const workPath = points.map((point, index) => `${index ? 'L' : 'M'}${x(point.time, run.scenario.duration)},${workY(point.remaining)}`).join(' ');
  const invalidation = run.cycles.flatMap(c => c.decisions).find(d => d.task === task?.id && d.invalidation?.cause === 'PREDICTED_RISK')?.invalidation;
  const currentWork = run.cycles[cursor]?.states.find(s => s.task === task?.id) ?? run.final.states.find(s => s.task === task?.id);
  return <div className="history-plots">
    <section className="history-panel"><div className="visual-heading"><div><span className="eyebrow">OBSERVATIONS OVER TIME</span><h3>Battery charge & protected floor</h3></div><span className="plot-key"><i className="observed-line" />Observed input <i className="floor-line" />Floor</span></div>
      <div className="plot-viewport"><svg viewBox="0 0 680 210" role="img" aria-label="Recorded battery charge over simulation time with the protected SOC floor">
        <defs><clipPath id={`${id}-battery`}><rect x={left} y={0} width={Math.max(1, x(now, run.scenario.duration) - left)} height={210} /></clipPath></defs>
        {invalidation && <rect x={x(invalidation.time, run.scenario.duration)} y={top} width={Math.max(0, x(invalidation.horizon, run.scenario.duration) - x(invalidation.time, run.scenario.duration))} height={height} className="risk-window" />}
        {[min, (min + max) / 2, max].map(value => <g key={value}><line x1={left} x2={left + width} y1={y(value)} y2={y(value)} className="plot-grid" /><text x={left - 9} y={y(value) + 4} textAnchor="end">{value}%</text></g>)}
        <Axis duration={run.scenario.duration} now={now} /><line x1={left} x2={left + width} y1={y(floor)} y2={y(floor)} className="soc-floor" /><text x={left + 6} y={y(floor) - 6} className="floor-text">protected {floor}%</text>
        <path d={batteryPath} className="trace-future" /><path d={batteryPath} clipPath={`url(#${id}-battery)`} className="trace-observed" />
        {observations.filter(p => p.time === now).map((p, i) => <circle key={i} cx={x(p.time, run.scenario.duration)} cy={y(p.value)} r={4} className="trace-dot" />)}
      </svg></div><p className="visual-caption">Usable cycle observations; gaps omit STALE/MISSING readings. {invalidation ? `Amber marks the recorded risk window, t=${invalidation.time}–${invalidation.horizon}. ` : ''}No forecast curve is invented.</p>
    </section>
    <section className="history-panel"><div className="visual-heading"><div><span className="eyebrow">PRESERVED EXECUTION PROGRESS</span><h3>Task {task?.id ?? '—'} · work remaining</h3></div><span className="mono">{currentWork?.phase === 'PENDING' ? task?.duration : currentWork?.remaining ?? '—'} / {task?.duration ?? '—'} ns</span></div>
      <div className="plot-viewport"><svg viewBox="0 0 680 210" role="img" aria-label={`Task ${task?.id} remaining work; flat sections mean no execution progress`}>
        <defs><clipPath id={`${id}-work`}><rect x={left} y={0} width={Math.max(1, x(now, run.scenario.duration) - left)} height={210} /></clipPath></defs>
        {task && executionSegments(run, task.id).filter(segment => segment.phase === 'PAUSED').map(segment => <rect key={`${segment.cycle}-${segment.start}`} x={x(segment.start, run.scenario.duration)} y={top} width={x(segment.end, run.scenario.duration) - x(segment.start, run.scenario.duration)} height={height} className="risk-window" />)}
        {['0', task?.duration ?? '1'].map(value => <g key={value}><line x1={left} x2={left + width} y1={workY(value)} y2={workY(value)} className="plot-grid" /><text x={left - 9} y={workY(value) + 4} textAnchor="end">{value}</text></g>)}
        <Axis duration={run.scenario.duration} now={now} /><path d={workPath} className="trace-future" /><path d={workPath} clipPath={`url(#${id}-work)`} className="trace-work" />
      </svg></div><p className="visual-caption">Downhill = execution; flat = no progress. Amber = paused. Faint lines show later recorded results, not predictions. Work required is shown before admission.</p>
    </section>
  </div>;
}

export function ExecutionMap({ run, cursor, taskId, select }: { run: Run; cursor: number; taskId: string; select: (id: string) => void }) {
  const now = run.cycles[cursor]?.time ?? run.final.time;
  const states = run.cycles[cursor]?.states ?? run.final.states;
  return <section className="execution-map"><div className="visual-heading"><div><span className="eyebrow">SCHEDULING ACROSS THE RUN</span><h3>Where each task spends its time</h3></div><span className="muted">Widths represent elapsed simulation time</span></div>
    <div className="execution-map-body">{run.scenario.tasks.map(task => <div key={task.id} className={`execution-lane ${task.id === taskId ? 'active-lane' : ''}`}><button onClick={() => select(task.id)} aria-pressed={task.id === taskId}><strong>T{task.id}</strong><span>{task.subsystem}<small>class {task.criticality} · deadline {task.deadline}</small></span></button><div className="lane-track" role="img" aria-label={`Task ${task.id} execution intervals`}>
      {executionSegments(run, task.id).map(segment => <span key={`${segment.cycle}-${segment.start}`} className={`lane-segment ${segment.phase.toLowerCase()}`} style={{ left: `${bounded(ratio(segment.start, run.scenario.duration))}%`, width: `${ratio(BigInt(segment.end) - BigInt(segment.start), run.scenario.duration)}%`, background: phaseColors[segment.phase] }} title={`T${task.id} ${label(segment.phase)}: t=${segment.start} to ${segment.end}`} />)}
      <span className="lane-cursor" style={{ left: `${bounded(ratio(now, run.scenario.duration))}%` }} />
    </div><span className={`lane-state phase-${states.find(s => s.task === task.id)?.phase.toLowerCase()}`}>{label(states.find(s => s.task === task.id)?.phase ?? 'UNKNOWN')}</span></div>)}</div>
    <div className="map-axis"><span>0</span><span>Selected t = {now} sim ns</span><span>{run.scenario.duration}</span></div><div className="map-legend">{Object.entries(phaseColors).map(([phase, color]) => <span key={phase}><i style={{ background: color }} />{label(phase)}</span>)}</div>
  </section>;
}
