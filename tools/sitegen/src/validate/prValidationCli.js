// Validate a pull request's card changes for GitHub Actions.
// Usage: git diff --name-status -z BASE...HEAD | node prValidationCli.js [SUMMARY.md]
//
// Prints GitHub annotations and a text report, writes the Markdown PR report
// to SUMMARY.md, and exits 1 when any validated file or submission rule has
// an error. Set BASE_SHA to apply the synchronized-flairs exception.

import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import YAML from 'yaml';
import { evaluateChangeSet } from './changeSet.js';
import { parseNameStatusZ } from './prRules.js';
import { reportGithub, reportMarkdown, reportOtherRulesGithub, reportText } from './reporters/index.js';

const summaryFile = process.argv[2];
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const flairsPath = 'tools/sitegen/src/curation/flairs.yml';

const changes = parseNameStatusZ(fs.readFileSync(0));
let baseFlairs = null;
if (process.env.BASE_SHA && changes.some(change => change.path === flairsPath || change.oldPath === flairsPath)) {
  try {
    const source = execFileSync('git', ['show', `${process.env.BASE_SHA}:${flairsPath}`], { cwd: root, encoding: 'utf8' });
    baseFlairs = YAML.parse(source) || {};
  } catch {}
}

const report = await evaluateChangeSet(changes, { root, baseFlairs });
const results = report.info.map(entry => entry.result);
const ruleDiagnostics = report.rules.diagnostics;
const otherRules = {
  trigger: report.trigger,
  diagnostics: ruleDiagnostics,
  errorCount: ruleDiagnostics.filter(item => item.severity === 'error').length,
  warningCount: ruleDiagnostics.filter(item => item.severity === 'warning').length,
};

if (results.length) console.log(`Validated ${results.map(result => result.file).join(', ')}`);
console.log(reportGithub(results));
const ruleAnnotations = reportOtherRulesGithub(otherRules);
if (ruleAnnotations) console.log(ruleAnnotations);
if (results.length) console.log(reportText(results));
if (summaryFile) fs.writeFileSync(summaryFile, reportMarkdown(results, otherRules));

const failed = results.some(result => result.errorCount > 0) || otherRules.errorCount > 0;
process.exit(failed ? 1 : 0);
