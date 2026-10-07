// Validate a NUL-delimited staged change list against a materialized Git-index
// snapshot. This file is executed from inside that snapshot.
//
// When a HEAD baseline directory is given, the same checks run against it and
// diagnostics already present there are not itemized and never block the
// commit. Existing errors are still counted, because PR validation in CI fails
// on any error in a changed card, so the author learns that before pushing.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseSourceFile } from './readSource.js';
import { validateInfoYaml } from './validateInfoYaml.js';
import { readCustomPanelManifest } from '../discover/customPanels.js';
import { loadKnownValues } from './knownValues.js';
import { evaluatePrRules, parseNameStatusZ } from './prRules.js';
import { color, printReport, step } from './stagedOutput.js';

const usage = 'Usage: stagedChangeSetCli.js CHANGES_FILE [--releases RELEASES_DIR] [--baseline BASELINE_DIR]';
const args = process.argv.slice(2);
const options = {};
let changesFile;
while (args.length) {
  const arg = args.shift();
  if (arg === '--releases' || arg === '--baseline') {
    if (!args.length) {
      console.error(usage);
      process.exit(2);
    }
    options[arg.slice(2)] = args.shift();
  } else if (!arg.startsWith('--') && !changesFile) {
    changesFile = arg;
  } else {
    console.error(usage);
    process.exit(2);
  }
}
if (!changesFile) {
  console.error(usage);
  process.exit(2);
}
const baselineRoot = options.baseline;

// Rules describing the state of a release directory, which may predate this
// commit. The remaining rules describe the change set itself and always apply.
const RELEASE_STATE_RULES = new Set([
  'draft-card-changed',
  'release-readme-recommended',
  'custom-panels',
  'uf2-required',
  'pico-xosc64-recommended',
]);

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const changes = parseNameStatusZ(fs.readFileSync(changesFile));
const infoChanges = [...new Map(changes
  .filter(change => !change.status.startsWith('D') && /(?:^|\/)info\.yaml$/i.test(change.path))
  .map(change => [change.path, change])).values()];

// Staged and baseline runs share one index so the comparison is like-for-like.
const knownValues = loadKnownValues(options.releases || path.join(root, 'releases'));

async function validateInfo(base, relative, displayPath = relative) {
  const file = path.join(base, relative);
  if (!fs.existsSync(file)) return null;
  const source = await parseSourceFile(file);
  source.file = displayPath;
  const customPanels = await readCustomPanelManifest(path.dirname(file));
  return validateInfoYaml(source, {
    customPanelsPresent: customPanels.present,
    panelIds: customPanels.items.map(item => item.id),
    knownValues,
    externalDiagnostics: customPanels.diagnostics.map(diagnostic => ({
      ...diagnostic,
      ruleId: 'custom-panel-manifest',
      key: 'panels',
    })),
  });
}

// Line numbers shift with unrelated edits, so identity ignores position.
const identity = diagnostic => [diagnostic.severity, diagnostic.ruleId, diagnostic.file ?? '', diagnostic.path ?? '', diagnostic.message].join('\0');

/** Split diagnostics into those absent from the baseline and those already in it. */
function subtractBaseline(diagnostics, baselineDiagnostics) {
  const remaining = new Map();
  for (const diagnostic of baselineDiagnostics) {
    const key = identity(diagnostic);
    remaining.set(key, (remaining.get(key) || 0) + 1);
  }
  const introduced = [];
  const existing = [];
  for (const diagnostic of diagnostics) {
    const key = identity(diagnostic);
    if (remaining.get(key) > 0) {
      remaining.set(key, remaining.get(key) - 1);
      existing.push(diagnostic);
    } else {
      introduced.push(diagnostic);
    }
  }
  return { introduced, existing };
}

const plural = (count, word) => `${count} ${word}${count === 1 ? '' : 's'}`;
const countBy = (diagnostics, severity) => diagnostics.filter(item => item.severity === severity).length;

function summarize(introduced, existing) {
  const parts = [];
  const errors = countBy(introduced, 'error');
  const warnings = countBy(introduced, 'warning');
  if (errors) parts.push(plural(errors, 'new error'));
  if (warnings) parts.push(plural(warnings, 'new warning'));
  if (!parts.length) parts.push('no new issues');
  const existingErrors = countBy(existing, 'error');
  const existingWarnings = countBy(existing, 'warning');
  if (existingErrors) parts.push(color.yellow(plural(existingErrors, 'existing error')));
  if (existingWarnings) parts.push(`${plural(existingWarnings, 'existing warning')} hidden`);
  return parts.join(', ');
}

const sections = [];
const existingIn = [];
let introducedAll = [];
let existingAll = [];

for (const change of infoChanges) {
  const progress = step(change.path);
  const result = await validateInfo(root, change.path);
  if (!result) {
    progress.done('not in snapshot');
    continue;
  }
  const baselineResult = baselineRoot ? await validateInfo(baselineRoot, change.oldPath || change.path, change.path) : null;
  const { introduced, existing } = subtractBaseline(result.diagnostics, baselineResult?.diagnostics || []);
  (countBy(introduced, 'error') ? progress.fail : progress.done)(summarize(introduced, existing));
  if (existing.length) existingIn.push(path.posix.dirname(change.path));
  existingAll = existingAll.concat(existing.map(item => ({ ...item, file: change.path })));
  if (introduced.length) sections.push({ title: change.path, diagnostics: introduced });
  introducedAll = introducedAll.concat(introduced.map(item => ({ ...item, file: change.path })));
}

const rulesProgress = step('Submission rules');
const ruleDiagnostics = await evaluatePrRules(changes, { root });
const baselineRuleDiagnostics = baselineRoot
  ? (await evaluatePrRules(changes, { root: baselineRoot })).filter(item => RELEASE_STATE_RULES.has(item.ruleId))
  : [];
const rules = subtractBaseline(ruleDiagnostics, baselineRuleDiagnostics);
(countBy(rules.introduced, 'error') ? rulesProgress.fail : rulesProgress.done)(summarize(rules.introduced, rules.existing));
if (rules.introduced.length) sections.push({ title: 'Submission rules', diagnostics: rules.introduced });
introducedAll = introducedAll.concat(rules.introduced);
existingAll = existingAll.concat(rules.existing);

function formatDiagnostic(diagnostic, showFile) {
  const tag = diagnostic.severity === 'error' ? color.red('error') : color.yellow('warning');
  const where = diagnostic.line == null ? '' : diagnostic.col == null ? `:${diagnostic.line}` : `:${diagnostic.line}:${diagnostic.col}`;
  const field = diagnostic.path ? ` [${diagnostic.path}]` : '';
  const file = showFile && diagnostic.file ? ` ${diagnostic.file}:` : '';
  const lines = [`  ${tag}${where}${field}${file} ${diagnostic.message} ${color.dim(`(${diagnostic.ruleId})`)}`];
  if (diagnostic.suggestion) lines.push(`    hint: ${diagnostic.suggestion}`);
  return lines;
}

const body = [];
for (const section of sections) {
  if (body.length) body.push('');
  body.push(color.bold(section.title));
  const showFile = section.title === 'Submission rules';
  for (const diagnostic of section.diagnostics) body.push(...formatDiagnostic(diagnostic, showFile));
}

const errors = countBy(introducedAll, 'error');
const warnings = countBy(introducedAll, 'warning');
const firstError = introducedAll.find(item => item.severity === 'error');
const existingErrors = existingAll.filter(item => item.severity === 'error');
const existingErrorFiles = [...new Set(existingErrors.map(item => item.file))];
const ciWarning = existingErrors.length
  ? `CI will still reject the PR: ${existingErrorFiles.join(', ')} already ${existingErrorFiles.length === 1 ? 'has' : 'have'} ${plural(existingErrors.length, 'error')}.`
  : '';

let headline;
if (errors) {
  const where = firstError.path ? `${firstError.file} [${firstError.path}]` : firstError.file;
  headline = `${color.red('✗ Commit blocked:')} ${plural(errors, 'new error')}${warnings ? `, ${plural(warnings, 'new warning')}` : ''}. First: ${where}: ${firstError.message}`;
} else if (existingErrors.length) {
  headline = `${color.yellow('⚠ Commit allowed, but')} ${ciWarning}${warnings ? ` Also ${plural(warnings, 'new warning')}.` : ''}`;
} else if (warnings) {
  headline = `${color.yellow('⚠')} Program card checks passed with ${plural(warnings, 'new warning')}.`;
} else {
  headline = `${color.green('✓')} Program card checks passed.`;
}

const footer = [];
if (errors && ciWarning) footer.push(color.yellow(ciWarning));
if (existingAll.length) {
  const targets = [...new Set(existingIn)];
  footer.push(color.dim(`Issues that already existed in HEAD are not listed${targets.length ? `; see them with: npm run validate-info -- ${targets.join(' ')}` : '.'}`));
}
if (errors) {
  footer.push('Fix the errors above, or bypass with git commit --no-verify (CI still validates the PR).');
}

printReport({ headline, body, footer });
process.exit(errors ? 1 : 0);
