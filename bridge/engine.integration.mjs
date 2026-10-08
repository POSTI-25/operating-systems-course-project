import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile, readdir } from 'node:fs/promises';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { fileURLToPath } from 'node:url';
import { parseCapture } from './parsers.mjs';

const root = fileURLToPath(new URL('..', import.meta.url));
const exec = promisify(execFile);
async function capture(name, variant = 'full') {
  const args = [`scenarios/${name}.scn`, variant];
  const linuxRoot = root.replace(/^([A-Za-z]):/, (_, drive) => `/mnt/${drive.toLowerCase()}`).replaceAll('\\', '/');
  const command = process.platform === 'win32' ? 'wsl.exe' : `${root}/build/frontend_runner`;
  const options = process.platform === 'win32' ? ['-d', 'Ubuntu', '--cd', linuxRoot, '--exec', './build/frontend_runner', ...args] : args;
  const { stdout } = await exec(command, options, { cwd: root, windowsHide: true, maxBuffer: 32 * 1024 * 1024 });
  return { raw: stdout, parsed: parseCapture(stdout) };
}

test('observation runner preserves every full-policy decision golden exactly', async () => {
  const names = (await readdir(new URL('../golden/', import.meta.url))).filter(name => name.endsWith('.decisions.expected'));
  for (const file of names) {
    const name = file.replace('.decisions.expected', '');
    const { raw, parsed } = await capture(name);
    const decisions = raw.split('\n').filter(line => line.startsWith('DECISION ')).join('\n') + '\n';
    assert.equal(decisions, await readFile(new URL(`../golden/${file}`, import.meta.url), 'utf8'), name);
    assert.ok(parsed.cycles.length > 0, name);
    assert.ok(parsed.final.states.length > 0, name);
    assert.ok(parsed.cycles.every(cycle => cycle.telemetry.length === 15), name);
  }
});

test('same-engine variant switches preserve genuine fault cadence', async () => {
  for (const variant of ['full', 'fixed', 'gomx', 'ranjbar']) {
    const { parsed } = await capture('sudden_fault', variant);
    assert.equal(parsed.cycles.some(cycle => cycle.trigger === 'FAULT'), ['full', 'ranjbar'].includes(variant), variant);
  }
});
