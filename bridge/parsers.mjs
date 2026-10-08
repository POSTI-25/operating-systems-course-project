export const variants = ['full', 'fixed', 'gomx', 'ranjbar'];
export const modes = ['FULL', 'THROTTLED', 'DEGRADED', 'DEFERRED', 'REJECTED'];
export const dimensions = ['CPU', 'ENERGY', 'MEMORY', 'STORAGE', 'SENSOR', 'COMM'];
const phases = ['PENDING', 'RUNNING', 'PAUSED', 'COMPLETE', 'REJECTED'];
const integer = value => {
  if (typeof value !== 'string' || !/^-?\d+$/.test(value)) throw new Error(`Invalid integer: ${value}`);
  return value;
};
function fields(line) {
  const result = {};
  for (const token of line.trim().split(/\s+/).slice(1)) {
    const at = token.indexOf('=');
    if (at < 1) throw new Error(`Malformed record: ${line}`);
    const key = token.slice(0, at);
    if (Object.hasOwn(result, key)) throw new Error(`Duplicate field: ${key}`);
    result[key] = token.slice(at + 1);
  }
  return result;
}
function ledger(text) {
  const entries = text?.split('|');
  if (entries?.length !== 6) throw new Error('Incomplete Ledger snapshot');
  return entries.map((entry, index) => {
    const [dimension, capacity, reserve, committed, free] = entry.split(':');
    if (dimension !== dimensions[index]) throw new Error('Invalid Ledger dimension order');
    const validate = dimension === 'SENSOR' ? value => {
      if (!/^0x[0-9a-f]{8}$/i.test(value)) throw new Error('Invalid sensor mask');
      return value;
    } : integer;
    return { dimension, capacity: validate(capacity), reserve: validate(reserve), committed: validate(committed), free: validate(free) };
  });
}
export function parseDecision(line) {
  if (!line.startsWith('DECISION ')) throw new Error('Expected DECISION');
  const f = fields(line);
  if (!phases.includes(f.state_before) || !phases.includes(f.state_after)) throw new Error('Invalid execution phase');
  const candidates = f.candidates === '-' ? [] : f.candidates.split('|').map((entry, index) => {
    const [mode, verdict, predicate, skipMask] = entry.split('/');
    if (mode !== modes[index] || !['PASS', 'ELIMINATED', 'FALLBACK_ONLY', 'DISABLED_BY_VARIANT'].includes(verdict) || !/^0x[0-9a-f]{2}$/i.test(skipMask)) throw new Error('Invalid candidate entry');
    return { mode, verdict, predicate, skipMask };
  });
  if (candidates.length !== 0 && candidates.length !== 5) throw new Error('Incomplete candidate list');
  let invalidation = null;
  if (f.invalidation !== 'NONE') {
    const values = f.invalidation.split(':');
    if (values.length !== 9) throw new Error('Incomplete invalidation');
    invalidation = { task: integer(values[0]), cause: values[1], quantity: values[2], time: integer(values[3]), originalMode: values[4], horizon: integer(values[5]), clearCount: integer(values[6]), requiredClear: integer(values[7]), upgraded: values[8] === '1' };
  }
  if (f.score !== 'NA' && !Number.isFinite(Number(f.score))) throw new Error('Invalid score');
  return { task: integer(f.task), tick: integer(f.tick), time: integer(f.sim_ns), trigger: f.trigger,
    before: f.state_before, after: f.state_after, candidates, selected: f.selected,
    score: f.score === 'NA' ? null : f.score, reason: f.reason,
    ledgerBefore: ledger(f.ledger_before), ledgerAfter: ledger(f.ledger_after), invalidation,
    restoration: f.restoration === '1', remaining: integer(f.remaining_ns),
    completed: f.completed_ns === 'NA' ? null : integer(f.completed_ns), value: integer(f.delivered_value), raw: line };
}
export function parseFrame(line) {
  const f = fields(line);
  return Object.entries(f).filter(([name]) => name !== 't').map(([name, text]) => {
    const match = /^(-?\d+)@(\d+):(OK|STALE|MISSING)$/.exec(text);
    if (!match) throw new Error(`Invalid telemetry channel ${name}`);
    return { name, value: integer(match[1]), timestamp: integer(match[2]), quality: match[3] };
  });
}
export function parseCapture(raw) {
  const cycles = [];
  let current = null;
  let final = null;
  for (const line of raw.trim().split(/\r?\n/)) {
    if (line.startsWith('CYCLE ')) {
      if (current) throw new Error('Unclosed cycle');
      const f = fields(line);
      if (f.sequence !== String(cycles.length)) throw new Error('Invalid cycle sequence');
      current = { sequence: f.sequence, time: integer(f.sim_ns), trigger: f.trigger, result: f.result, emergency: f.emergency === '1', telemetry: [], decisions: [], states: [], assumptions: [] };
    } else if (line === 'CYCLE_END') {
      if (!current || current.telemetry.length !== 15) throw new Error('Incomplete cycle');
      if (current.result !== 'OK' && current.decisions.length) throw new Error('Discarded decisions must not be published');
      cycles.push(current); current = null;
    } else if (line.startsWith('FRAME ')) {
      if (!current) throw new Error('Frame outside cycle');
      current.telemetry = parseFrame(line);
    } else if (line.startsWith('DECISION ')) {
      if (!current) throw new Error('Decision outside cycle');
      current.decisions.push(parseDecision(line));
    } else if (line.startsWith('ASSUMPTION ')) {
      if (!current) throw new Error('Assumption outside cycle');
      current.assumptions.push(fields(line));
    } else if (line.startsWith('STATE ')) {
      const f = fields(line);
      if (!phases.includes(f.phase)) throw new Error('Invalid final phase');
      const state = { task: integer(f.task), phase: f.phase, remaining: integer(f.remaining_ns), completed: f.phase === 'COMPLETE' ? integer(f.completed_ns) : null, value: integer(f.delivered_value) };
      if (current) current.states.push(state);
      else if (final) final.states.push(state);
      else throw new Error('State outside snapshot');
    } else if (line.startsWith('FINAL ')) {
      if (current || final) throw new Error('Invalid final snapshot');
      final = { time: integer(fields(line).sim_ns), states: [], complete: false };
    } else if (line === 'FINAL_END') {
      if (!final) throw new Error('Missing final snapshot');
      final.complete = true;
    } else throw new Error(`Unknown observer record: ${line}`);
  }
  if (current || !final?.complete || !cycles.length) throw new Error('Truncated observation stream');
  return { cycles, final };
}
export function parseScenario(id, raw) {
  const result = { id, name: id, duration: '0', step: '0', batteryCapacity: '0', socFloor: '0', tasks: [], events: [], initial: [], raw };
  for (const line of raw.split(/\r?\n/)) {
    const tokens = line.split('#')[0].trim().split(/\s+/);
    const [key, ...v] = tokens;
    if (key === 'SCENARIO') result.name = v[0];
    if (key === 'DURATION_NS') result.duration = v[0];
    if (key === 'STEP_NS') result.step = v[0];
    if (key === 'BATTERY_CAPACITY_MJ') result.batteryCapacity = v[0];
    if (key === 'SOC_FLOOR_PERMILLE') result.socFloor = v[0];
    if (key === 'TASK') result.tasks.push({ id: v[0], period: v[1], deadline: v[2], duration: v[3], criticality: v[4], value: v[5], subsystem: v[6], profiles: [] });
    if (key === 'TASK_MODE') result.tasks.find(task => task.id === v[0])?.profiles.push({ mode: v[1], energy: v[2], thermal: v[3], fraction: v[4], cpu: v[5], memory: v[6], storage: v[7], sensor: v[8], comm: v[9] });
    if (key === 'INITIAL') result.initial.push({ channel: v[0] === 'TEMP_MC' ? `${v[0]}[${v[1]}]` : v[0], value: v[v.length - 3], timestamp: v[v.length - 2], quality: v[v.length - 1] });
    if (['UPDATE', 'FAULT', 'ORBIT'].includes(key)) result.events.push({ type: key, time: v[0], detail: v.slice(1).join(' ') });
  }
  return result;
}
export function parseMetrics(raw) {
  const lines = raw.trim().split(/\r?\n/);
  const header = lines.shift().split(',');
  if (header.length !== 25 || header[0] !== 'scenario' || header[24] !== 'prediction_abs_error') throw new Error('Unknown metrics schema');
  return lines.map(line => {
    const cells = line.split(',');
    if (cells.length !== header.length) throw new Error('Incomplete metrics row');
    return Object.fromEntries(header.map((key, i) => [key, cells[i] === 'NA' ? null : cells[i]]));
  });
}
