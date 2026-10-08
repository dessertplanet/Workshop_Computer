// Validate a pull request's card changes for GitHub Actions.
// Usage: git diff --name-status -z BASE...HEAD | node prValidationCli.js [SUMMARY.md]
//
// Prints GitHub annotations and a text report, writes the Markdown PR report
// to SUMMARY.md, and exits 1 when any validated file or submission rule has
// an error. A PR that touches no program card is skipped: it succeeds and
// writes no report.
//
// Environment:
//   BASE_SHA               base commit; enables the synchronized-flairs
//                          exception and the auto-merge eligibility report
//   PR_AUTHOR              PR author login            (eligibility report)
//   PR_AUTHOR_ASSOCIATION  PR author_association      (eligibility report)
//   PR_DRAFT               "true" for draft PRs       (eligibility report)
//   GITHUB_REPOSITORY, GITHUB_TOKEN                   (card committer lookup)

import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import YAML from 'yaml';
import { evaluateChangeSet } from './changeSet.js';
import { fetchCardCommitters } from './cardCommitters.js';
import { cardScope, evaluateMergeEligibility, touchesNoCards } from './mergeEligibility.js';
import { parseNameStatusZ } from './prRules.js';
import {
  reportEligibilityMarkdown, reportGithub, reportMarkdown, reportOtherRulesGithub, reportText,
} from './reporters/index.js';

const summaryFile = process.argv[2];
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const flairsPath = 'tools/sitegen/src/curation/flairs.yml';

const changes = parseNameStatusZ(fs.readFileSync(0));
if (touchesNoCards(changes)) {
  console.log('No program card changes; card validation skipped.');
  process.exit(0);
}
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
const errorCount = results.reduce((count, result) => count + result.errorCount, 0) + otherRules.errorCount;

// Report-only: show whether this PR would be merged without review.
let eligibility = null;
const base = process.env.BASE_SHA;
if (base && process.env.PR_AUTHOR) {
  const { cards } = cardScope(changes);
  const card = cards.length === 1 ? cards[0] : null;
  const treeHas = (ref, relative) => {
    try {
      execFileSync('git', ['cat-file', '-e', `${ref}:${relative}`], { cwd: root, stdio: 'ignore' });
      return true;
    } catch {
      return false;
    }
  };
  const cardOnBase = card ? treeHas(base, `releases/${card}`) : false;
  const committers = card && cardOnBase && process.env.GITHUB_REPOSITORY
    ? await fetchCardCommitters({ repo: process.env.GITHUB_REPOSITORY, card, ref: base, token: process.env.GITHUB_TOKEN })
    : null;
  eligibility = evaluateMergeEligibility({
    changes,
    author: process.env.PR_AUTHOR,
    association: process.env.PR_AUTHOR_ASSOCIATION || 'NONE',
    draft: process.env.PR_DRAFT === 'true',
    errorCount,
    cardOnBase,
    cardInHead: card ? fs.existsSync(path.join(root, 'releases', card)) : false,
    committers,
  });
  console.log(`Auto-merge eligibility (report only): ${eligibility.eligible ? 'eligible' : 'not eligible'}`);
  for (const line of eligibility.eligible ? [eligibility.basis] : eligibility.reasons) console.log(`  ${line}`);
}

if (summaryFile) {
  const sections = [reportMarkdown(results, otherRules)];
  if (eligibility) sections.push(reportEligibilityMarkdown(eligibility));
  fs.writeFileSync(summaryFile, sections.join('\n'));
}

process.exit(errorCount ? 1 : 0);
