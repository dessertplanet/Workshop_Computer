// Materialize the exact Git index state for affected releases, plus the HEAD
// state as a baseline, then run the staged copy of the card validator.
// Unstaged working-tree edits are excluded, and issues already present in HEAD
// are hidden so authors only see what their commit introduces. If the
// installed validator dependencies no longer match package-lock.json (e.g.
// after pulling a Dependabot update), they are reinstalled with `npm ci` first.

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawn, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { color, interactive, step } from './stagedOutput.js';

const sourceRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const sitegenDir = path.join(sourceRoot, 'tools', 'sitegen');
const dependencyDir = path.join(sitegenDir, 'node_modules');

function git(args, options = {}) {
  const result = spawnSync('git', args, { cwd: sourceRoot, encoding: 'utf8', ...options });
  if (result.status !== 0) throw new Error(result.stderr?.trim() || `git ${args.join(' ')} failed`);
  return result.stdout;
}

function fieldsFrom(buffer) {
  const fields = buffer.toString('utf8').split('\0');
  if (fields.at(-1) === '') fields.pop();
  return fields;
}

function affectedReleases(changeBuffer) {
  const fields = fieldsFrom(changeBuffer);
  const releases = new Set();
  for (let index = 0; index < fields.length;) {
    const status = fields[index++];
    const paths = /^[RC]/.test(status)
      ? [fields[index++], fields[index++]]
      : [fields[index++]];
    for (const value of paths) {
      const parts = String(value).replaceAll('\\', '/').split('/');
      if (parts[0] === 'releases' && parts.length >= 3) releases.add(parts[1]);
    }
  }
  return [...releases].sort();
}

/**
 * Why node_modules does not match package-lock.json, or null when it does.
 * npm records what it installed in node_modules/.package-lock.json; optional
 * packages for other platforms appear only in the lockfile and are ignored.
 */
function dependencyDrift() {
  if (!fs.existsSync(dependencyDir)) return 'not installed';
  let locked;
  let installed;
  try {
    locked = JSON.parse(fs.readFileSync(path.join(sitegenDir, 'package-lock.json'), 'utf8')).packages || {};
  } catch {
    return null; // Nothing to compare against; let npm report problems itself.
  }
  try {
    installed = JSON.parse(fs.readFileSync(path.join(dependencyDir, '.package-lock.json'), 'utf8')).packages || {};
  } catch {
    return 'install record missing';
  }
  for (const name of new Set([...Object.keys(locked), ...Object.keys(installed)])) {
    if (!name) continue;
    if (!installed[name] && locked[name]?.optional) continue;
    if (locked[name]?.version !== installed[name]?.version) {
      return 'package-lock.json changed since the last install';
    }
  }
  return null;
}

function installDependencies(reason) {
  const progress = step(`Installing validator dependencies (${reason})`);
  const result = spawnSync('npm', ['ci', '--no-audit', '--no-fund'], {
    cwd: sitegenDir, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024,
  });
  // npm can crash ("Exit handler never called!") yet exit 0 with a partial
  // install, so trust the install record rather than the exit status.
  if (result.status !== 0 || dependencyDrift()) {
    progress.fail();
    const output = `${result.stdout || ''}${result.stderr || ''}`.trim().split('\n')
      .filter(line => !line.startsWith('npm warn')).slice(-15).join('\n');
    throw new Error(`npm ci failed${result.error ? ` (${result.error.message})` : ''}. Run it manually: npm ci --prefix tools/sitegen${output ? `\n${output}` : ''}`);
  }
  progress.done();
}

function treeContains(tree, relative) {
  return spawnSync('git', ['cat-file', '-e', `${tree}:${relative}`], {
    cwd: sourceRoot, stdio: 'ignore',
  }).status === 0;
}

function archive(tree, paths, destination) {
  return new Promise((resolve, reject) => {
    const gitArchive = spawn('git', ['archive', '--format=tar', tree, '--', ...paths], {
      cwd: sourceRoot, stdio: ['ignore', 'pipe', 'pipe'],
    });
    const tar = spawn('tar', ['-xf', '-', '-C', destination], { stdio: ['pipe', 'ignore', 'pipe'] });
    gitArchive.stdout.pipe(tar.stdin);
    let errors = '';
    gitArchive.stderr.on('data', chunk => { errors += chunk; });
    tar.stderr.on('data', chunk => { errors += chunk; });
    let gitStatus;
    let tarStatus;
    const finish = () => {
      if (gitStatus === undefined || tarStatus === undefined) return;
      if (gitStatus === 0 && tarStatus === 0) resolve();
      else reject(new Error(errors.trim() || 'Could not materialize the staged snapshot.'));
    };
    gitArchive.on('close', code => { gitStatus = code; finish(); });
    tar.on('close', code => { tarStatus = code; finish(); });
  });
}

let temporary;
let snapshotStep;
try {
  const changes = spawnSync('git', ['diff', '--cached', '--name-status', '--find-renames=50%', '-z'], {
    cwd: sourceRoot, encoding: null, maxBuffer: 64 * 1024 * 1024,
  });
  if (changes.status !== 0) throw new Error(changes.stderr?.toString().trim() || 'Could not inspect staged changes.');
  if (!changes.stdout.length) {
    console.log('No staged changes to validate.');
    process.exit(0);
  }
  const releases = affectedReleases(changes.stdout);
  if (!releases.length) {
    console.log('No staged program card changes; validation skipped.');
    process.exit(0);
  }

  if (interactive) console.log(color.bold('Checking staged program cards'));
  const drift = dependencyDrift();
  if (drift) installDependencies(drift);
  snapshotStep = step(`Snapshotting ${releases.length} release${releases.length === 1 ? '' : 's'}`);
  const tree = git(['write-tree']).trim();
  const head = spawnSync('git', ['rev-parse', '--verify', '-q', 'HEAD^{tree}'], {
    cwd: sourceRoot, encoding: 'utf8',
  });
  const headTree = head.status === 0 ? head.stdout.trim() : null;
  temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'workshop-card-staged-'));
  const snapshot = path.join(temporary, 'snapshot');
  fs.mkdirSync(snapshot);
  const releasePaths = treeish => releases
    .map(release => `releases/${release}`)
    .filter(relative => treeContains(treeish, relative));
  await archive(tree, ['tools/sitegen/src', 'tools/sitegen/package.json', ...releasePaths(tree)], snapshot);
  fs.symlinkSync(dependencyDir, path.join(snapshot, 'tools', 'sitegen', 'node_modules'), 'dir');
  let baseline = null;
  if (headTree) {
    baseline = path.join(temporary, 'baseline');
    fs.mkdirSync(baseline);
    const paths = releasePaths(headTree);
    if (paths.length) await archive(headTree, paths, baseline);
  }
  const changesFile = path.join(temporary, 'changes.bin');
  fs.writeFileSync(changesFile, changes.stdout);
  snapshotStep.done(headTree ? 'staged + HEAD' : 'staged; no HEAD to compare');

  const runner = spawnSync(process.execPath, [
    path.join(snapshot, 'tools', 'sitegen', 'src', 'validate', 'stagedChangeSetCli.js'),
    changesFile,
    // The snapshot only holds the changed cards; index the rest from the repo.
    '--releases', path.join(sourceRoot, 'releases'),
    ...(baseline ? ['--baseline', baseline] : []),
  ], { cwd: snapshot, stdio: 'inherit' });
  process.exitCode = runner.status ?? 2;
} catch (error) {
  snapshotStep?.fail();
  console.error(`${color.red('Pre-commit validation could not run:')} ${error.message}`);
  process.exitCode = 2;
} finally {
  if (temporary) fs.rmSync(temporary, { recursive: true, force: true });
}
