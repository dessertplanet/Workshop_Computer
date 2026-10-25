import { test } from 'node:test';
import assert from 'node:assert/strict';
import { GRACE_MS, VALIDATION_CHECK, VERDICT_CHECK, decideAutoMerge } from '../src/validate/autoMerge.js';

const now = Date.parse('2026-10-25T12:00:00Z');
const ago = minutes => new Date(now - minutes * 60000).toISOString();
const run = (name, conclusion, startedMinutesAgo, overrides = {}) => ({
  name, status: 'completed', conclusion, started_at: ago(startedMinutesAgo), app: { slug: 'github-actions' }, ...overrides,
});
const pr = overrides => ({ number: 1, draft: false, base: { ref: 'main' }, head: { sha: 'abc' }, labels: [], ...overrides });
const ready = [run(VERDICT_CHECK, 'success', 61), run(VALIDATION_CHECK, 'success', 61)];
const decide = (checkRuns, prOverrides = {}) => decideAutoMerge({ pr: pr(prOverrides), checkRuns, now });

test('an eligible, validated PR merges once the grace period is over', () => {
  assert.equal(GRACE_MS, 60 * 60 * 1000);
  const decision = decide(ready);
  assert.equal(decision.merge, true, decision.reason);
  assert.match(decision.reason, /waited 61 min/);
});

test('the grace period runs from the first verdict on the commit', () => {
  assert.match(decide([run(VERDICT_CHECK, 'success', 30), run(VALIDATION_CHECK, 'success', 30)]).reason, /grace period \(30 min left\)/);
  // A label change re-runs the verdict; the earlier verdict on the same commit still starts the clock.
  const relabelled = [run(VERDICT_CHECK, 'neutral', 90), run(VERDICT_CHECK, 'success', 5), run(VALIDATION_CHECK, 'success', 90)];
  assert.equal(decide(relabelled).merge, true);
});

test('the latest verdict decides eligibility', () => {
  assert.match(decide([run(VERDICT_CHECK, 'success', 90), run(VERDICT_CHECK, 'neutral', 5), run(VALIDATION_CHECK, 'success', 90)]).reason, /not eligible/);
  assert.match(decide([run(VALIDATION_CHECK, 'success', 90)]).reason, /No trusted verdict/);
  assert.match(decide([run(VERDICT_CHECK, 'success', 90, { status: 'in_progress', conclusion: null }), run(VALIDATION_CHECK, 'success', 90)]).reason, /not eligible/);
});

test('validation must have passed on the head commit', () => {
  assert.match(decide([run(VERDICT_CHECK, 'success', 90)]).reason, /awaiting approval/);
  assert.match(decide([run(VERDICT_CHECK, 'success', 90), run(VALIDATION_CHECK, 'failure', 90)]).reason, /has not passed/);
  assert.match(decide([run(VERDICT_CHECK, 'success', 90), run(VALIDATION_CHECK, 'success', 90), run(VALIDATION_CHECK, 'failure', 2)]).reason, /has not passed/);
});

test('check runs from other apps are ignored', () => {
  const spoofed = [run(VERDICT_CHECK, 'success', 90, { app: { slug: 'someone-else' } }), run(VALIDATION_CHECK, 'success', 90)];
  assert.match(decide(spoofed).reason, /No trusted verdict/);
});

test('drafts, held PRs and other base branches are skipped', () => {
  assert.match(decide(ready, { draft: true }).reason, /Draft/);
  assert.match(decide(ready, { labels: [{ name: 'hold' }] }).reason, /"hold" label/);
  assert.match(decide(ready, { base: { ref: 'workshop-ops' } }).reason, /not main/);
});
