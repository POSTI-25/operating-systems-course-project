import http from 'node:http';
import { spawn } from 'node:child_process';
import { readFile, readdir, mkdir, writeFile, access } from 'node:fs/promises';
import { resolve, extname, sep } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash, randomUUID } from 'node:crypto';
import { parseCapture, parseScenario, parseMetrics, variants } from './parsers.mjs';

const root = fileURLToPath(new URL('..', import.meta.url));
const dist = resolve(root, 'frontend/dist');
const port = Number(process.env.SAGE_UI_PORT ?? 4317);
const host = '127.0.0.1';
let running = false;
const active = new Set();
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
function execute(binary, args) {
  return new Promise((accept, reject) => {
    const windows = process.platform === 'win32';
    const linuxRoot = windows ? root.replace(/^([A-Za-z]):/, (_, drive) => `/mnt/${drive.toLowerCase()}`).replaceAll('\\', '/') : root;
    const child = spawn(windows ? 'wsl.exe' : resolve(root, 'build', binary), windows ?
      ['-d', 'Ubuntu', '--cd', linuxRoot, '--exec', `./build/${binary}`, ...args] : args,
      { cwd: root, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    active.add(child);
    let stdout = '', stderr = '', bytes = 0, failed = false;
    const timer = setTimeout(() => { failed = true; child.kill(); reject(new Error('Engine timed out after 30 seconds.')); }, 30000);
    const collect = (chunk, output) => {
      bytes += chunk.length;
      if (bytes > 32 * 1024 * 1024) { failed = true; child.kill(); reject(new Error('Capture exceeds the 32 MB limit.')); }
      else if (output) stdout += chunk.toString(); else stderr += chunk.toString();
    };
    child.stdout.on('data', chunk => collect(chunk, true));
    child.stderr.on('data', chunk => collect(chunk, false));
    child.on('error', error => { clearTimeout(timer); active.delete(child); reject(error); });
    child.on('close', code => {
      clearTimeout(timer); active.delete(child);
      if (failed) return;
      if (code !== 0) reject(new Error(stderr.trim() || `Engine exited with status ${code}. Build with make workstation in WSL Ubuntu.`));
      else accept(stdout);
    });
  });
}
async function scenarios() {
  const names = (await readdir(resolve(root, 'scenarios'))).filter(name => /^[a-z0-9_-]+\.scn$/.test(name)).sort();
  return Promise.all(names.map(async name => parseScenario(name.slice(0, -4), await readFile(resolve(root, 'scenarios', name), 'utf8'))));
}
async function jsonBody(req) {
  if (!req.headers['content-type']?.startsWith('application/json')) throw new Error('Expected application/json');
  let text = '';
  for await (const chunk of req) { text += chunk; if (text.length > 4096) throw new Error('Request too large'); }
  return JSON.parse(text);
}
function send(res, status, value) {
  res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' });
  res.end(JSON.stringify(value));
}
const server = http.createServer(async (req, res) => {
  try {
    const origin = req.headers.origin;
    if (origin && ![`http://127.0.0.1:${port}`, 'http://127.0.0.1:5173', `http://localhost:${port}`, 'http://localhost:5173'].includes(origin)) return send(res, 403, { error: 'This workstation accepts local requests only.' });
    const url = new URL(req.url, `http://${host}:${port}`);
    if (req.method === 'GET' && url.pathname === '/api/health') {
      let engineReady = true;
      try { await access(resolve(root, 'build/frontend_runner')); } catch { engineReady = false; }
      return send(res, 200, { connected: true, engineReady, busy: running, platform: process.platform, mode: 'reference-replay' });
    }
    if (req.method === 'GET' && url.pathname === '/api/scenarios') return send(res, 200, await scenarios());
    if (req.method === 'GET' && url.pathname === '/api/metrics') {
      const raw = await readFile(resolve(root, 'docs/phase5_metrics.csv'), 'utf8');
      return send(res, 200, { source: 'checked-in', hash: hash(raw), rows: parseMetrics(raw) });
    }
    if (req.method === 'POST' && url.pathname === '/api/runs') {
      let body;
      try { body = await jsonBody(req); }
      catch (error) { return send(res, 400, { error: error.message }); }
      if (!body || typeof body !== 'object' || Array.isArray(body)) return send(res, 400, { error: 'Expected a scenario and variant object.' });
      const scenario = (await scenarios()).find(item => item.id === body.scenario);
      if (!scenario || !variants.includes(body.variant)) return send(res, 400, { error: 'Choose a known scenario and variant.' });
      if (running) return send(res, 409, { error: 'Another reference run is in progress.' });
      running = true;
      try {
        const binary = await readFile(resolve(root, 'build/frontend_runner'));
        const raw = await execute('frontend_runner', [`scenarios/${scenario.id}.scn`, body.variant]);
        const run = { id: randomUUID(), createdAt: new Date().toISOString(), source: 'reference-replay', variant: body.variant,
          scenario, provenance: { scenarioHash: hash(scenario.raw), executableHash: hash(binary), selfCost: 'zero', cap: 'nonbinding', protectedReserves: 'zero', clock: 'virtual' }, ...parseCapture(raw), raw };
        const directory = resolve(root, 'output/ui-runs');
        await mkdir(directory, { recursive: true });
        await writeFile(resolve(directory, `${run.id}.json`), JSON.stringify(run, null, 2));
        return send(res, 201, run);
      } finally { running = false; }
    }
    if (url.pathname.startsWith('/api/')) return send(res, 404, { error: 'Unknown API operation.' });
    if (req.method !== 'GET') return send(res, 405, { error: 'Method not allowed.' });
    const file = resolve(dist, `.${decodeURIComponent(url.pathname === '/' ? '/index.html' : url.pathname)}`);
    if (!file.startsWith(dist + sep)) return send(res, 403, { error: 'Invalid path.' });
    try {
      const data = await readFile(file);
      const mime = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.svg': 'image/svg+xml' };
      res.writeHead(200, { 'Content-Type': mime[extname(file)] ?? 'application/octet-stream' }); res.end(data);
    } catch { send(res, 404, { error: 'Browser build missing. Run npm run build, or npm run dev for development.' }); }
  } catch (error) { send(res, 500, { error: error.message }); }
});
server.listen(port, host, () => console.log(`SAGE-RT workstation: http://${host}:${port}`));
function shutdown() { for (const child of active) child.kill(); server.close(); }
process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);
