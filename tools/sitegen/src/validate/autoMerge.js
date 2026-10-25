// Decide whether the scheduled auto-merge job should merge a pull request now.
//
// The trusted eligibility workflow (merge-eligibility.yml) publishes its
// verdict as a check run on the PR's head commit. This module combines those
// check runs with the contributor-facing validation check and the PR's live
// state. It is pure: auto-merge.yml gathers the facts from the GitHub API and
// imports it from the default branch, so it never runs code from a PR.

import { HOLD_LABEL } from './mergeEligibility.js';

/** Name of the check run carrying the trusted verdict. */
export const VERDICT_CHECK = 'Auto-merge verdict';

/** Name of the Validate info.yaml job, which must pass on the merged commit. */
export const VALIDATION_CHECK = 'Validate changed card metadata';

/** How long a commit must wait after its first verdict, so a maintainer can add the hold label. */
export const GRACE_MS = 60 * 60 * 1000;

const time = value => (value ? Date.parse(value) : NaN);
const latest = runs => runs.reduce((best, run) => (!best || time(run.started_at) > time(best.started_at) ? run : best), null);

/**
 * Facts:
 *   pr        { number, draft, base: { ref }, head: { sha }, labels: [{ name }] }
 *   checkRuns check runs on pr.head.sha: { name, status, conclusion, started_at,
 *             app: { slug } }
 *   now       current time in milliseconds
 *
 * Returns { merge, reason }: `reason` says why a PR is or is not merged.
 */
export function decideAutoMerge({ pr, checkRuns, now, graceMs = GRACE_MS }) {
  const skip = reason => ({ merge: false, reason });
  if (pr.base?.ref !== 'main') return skip(`Targets ${pr.base?.ref}, not main.`);
  if (pr.draft) return skip('Draft.');
  if ((pr.labels || []).some(label => label.name === HOLD_LABEL)) return skip(`Has the "${HOLD_LABEL}" label.`);

  // Only check runs created by GitHub Actions count: a PR author cannot post
  // one with that app from a fork, and pushing to the repo already implies trust.
  const fromActions = (checkRuns || []).filter(run => run.app?.slug === 'github-actions');
  const verdicts = fromActions.filter(run => run.name === VERDICT_CHECK);
  const verdict = latest(verdicts);
  if (!verdict) return skip('No trusted verdict for the head commit yet.');
  if (verdict.status !== 'completed' || verdict.conclusion !== 'success') return skip('The trusted verdict is not eligible.');

  // The grace period runs from this commit's first verdict, so a new push
  // restarts it but a label change does not.
  const firstJudged = Math.min(...verdicts.map(run => time(run.started_at)).filter(Number.isFinite));
  if (!Number.isFinite(firstJudged)) return skip('The verdict has no start time.');
  const waited = now - firstJudged;
  if (waited < graceMs) return skip(`In the grace period (${Math.ceil((graceMs - waited) / 60000)} min left).`);

  const validation = latest(fromActions.filter(run => run.name === VALIDATION_CHECK));
  if (!validation) return skip('Validation has not run on the head commit (it may be awaiting approval).');
  if (validation.status !== 'completed' || validation.conclusion !== 'success') return skip('Validation has not passed on the head commit.');

  return { merge: true, reason: `Eligible, and the head commit has waited ${Math.floor(waited / 60000)} min.` };
}
