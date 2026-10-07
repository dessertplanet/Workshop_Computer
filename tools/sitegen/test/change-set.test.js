import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { evaluateChangeSet, infoFilesToValidate } from '../src/validate/changeSet.js';

const repositoryRoot = fileURLToPath(new URL('../../..', import.meta.url));

async function fixture(t) {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'workshop-change-set-'));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  return root;
}

async function write(root, relative, contents = '') {
  const file = path.join(root, relative);
  await fs.mkdir(path.dirname(file), { recursive: true });
  await fs.writeFile(file, contents);
}

const validInfo = `Name: Test\nshort-description: Short\nsummary: Long\nLanguage: C++\nCreator: Test\nVersion: "1.0"\nStatus: WIP\n`;

test('info.yaml selection matches added, modified, and renamed files under releases/', () => {
  const selected = infoFilesToValidate([
    { status: 'A', path: 'releases/01_new/info.yaml' },
    { status: 'M', path: 'releases/02_card/legacy/info.yaml' },
    { status: 'R087', oldPath: 'releases/03_old/info.yaml', path: 'releases/03_new/info.yaml' },
    { status: 'D', path: 'releases/04_gone/info.yaml' },
    { status: 'M', path: 'releases/05_card/Info.yaml' },
    { status: 'M', path: 'documentation/info.yaml' },
    { status: 'M', path: 'releases/06_card/README.md' },
  ]);
  assert.deepEqual(selected.map(change => change.path), [
    'releases/01_new/info.yaml',
    'releases/02_card/legacy/info.yaml',
    'releases/03_new/info.yaml',
  ]);
  assert.equal(selected[2].oldPath, 'releases/03_old/info.yaml');
});

test('without a baseline every diagnostic is introduced', async t => {
  const root = await fixture(t);
  await write(root, 'releases/42_test/info.yaml', validInfo.replace('Creator: Test\n', ''));
  const report = await evaluateChangeSet([{ status: 'M', path: 'releases/42_test/info.yaml' }], { root });
  assert.equal(report.info.length, 1);
  assert.equal(report.info[0].existing.length, 0);
  assert.ok(report.info[0].introduced.some(item => item.path === 'Creator' && item.severity === 'error'));
  assert.ok(report.rules.introduced.some(item => item.ruleId === 'uf2-required'));
});

test('baseline diagnostics are existing, including across renames', async t => {
  const root = await fixture(t);
  const baselineRoot = await fixture(t);
  const broken = validInfo.replace('Creator: Test\n', '');
  await write(baselineRoot, 'releases/42_old/info.yaml', broken);
  await write(root, 'releases/42_new/info.yaml', broken.replace('Status: WIP', 'Status: WIP\nLicense: MIT'));
  await write(root, 'releases/42_new/info.extra', '');
  const report = await evaluateChangeSet([
    { status: 'R090', oldPath: 'releases/42_old/info.yaml', path: 'releases/42_new/info.yaml' },
  ], { root, baselineRoot });
  const [entry] = report.info;
  assert.ok(entry.existing.some(item => item.path === 'Creator'));
  assert.ok(!entry.introduced.some(item => item.path === 'Creator'));
  // The full result still carries every diagnostic for consumers that block on all errors.
  assert.ok(entry.result.diagnostics.some(item => item.path === 'Creator'));
  // A change-scoped rule is never subtracted, even if the baseline would also report it.
  assert.ok(report.rules.introduced.some(item => item.ruleId === 'multiple-release-directories'));
});

test('PR validation CLI fails on any error in a changed card, including pre-existing ones', async t => {
  const root = await fixture(t);
  const run = (args, cwd) => spawnSync(args[0], args.slice(1), { cwd, encoding: 'utf8' });
  await fs.mkdir(path.join(root, 'tools', 'sitegen'), { recursive: true });
  await fs.cp(path.join(repositoryRoot, 'tools', 'sitegen', 'src'), path.join(root, 'tools', 'sitegen', 'src'), { recursive: true });
  await fs.symlink(path.join(repositoryRoot, 'tools', 'sitegen', 'node_modules'), path.join(root, 'tools', 'sitegen', 'node_modules'), 'dir');
  assert.equal(run(['git', 'init', '-q'], root).status, 0);
  run(['git', 'config', 'user.email', 'test@example.com'], root);
  run(['git', 'config', 'user.name', 'Test'], root);
  await write(root, 'releases/42_test/info.yaml', validInfo.replace('Creator: Test\n', ''));
  await write(root, 'releases/42_test/card.uf2', 'firmware');
  run(['git', 'add', '.'], root);
  assert.equal(run(['git', 'commit', '-qm', 'base'], root).status, 0);
  await write(root, 'releases/42_test/info.yaml', `${validInfo.replace('Creator: Test\n', '')}# comment\n`);
  run(['git', 'commit', '-qam', 'comment only'], root);

  const diff = spawnSync('git', ['diff', '--name-status', '-z', 'HEAD~1...HEAD'], { cwd: root });
  const result = spawnSync(process.execPath, ['tools/sitegen/src/validate/prValidationCli.js', 'summary.md'], {
    cwd: root, input: diff.stdout, encoding: 'utf8',
  });
  assert.equal(result.status, 1, result.stderr || result.stdout);
  assert.match(result.stdout, /::error file=releases\/42_test\/info\.yaml/);
  const summary = await fs.readFile(path.join(root, 'summary.md'), 'utf8');
  assert.match(summary, /Program card PR validation failed/);
});
