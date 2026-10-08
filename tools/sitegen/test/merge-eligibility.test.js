import { test } from 'node:test';
import assert from 'node:assert/strict';
import { evaluateMergeEligibility, touchesNoCards } from '../src/validate/mergeEligibility.js';
import { fetchCardCommitters } from '../src/validate/cardCommitters.js';
import { reportEligibilityMarkdown } from '../src/validate/reporters/index.js';

const update = { status: 'M', path: 'releases/42_card/info.yaml' };
const firmware = { status: 'A', path: 'releases/42_card/build/card.uf2' };

function check(overrides) {
  return evaluateMergeEligibility({
    changes: [update, firmware],
    author: 'maker',
    association: 'CONTRIBUTOR',
    cardOnBase: true,
    cardInHead: true,
    committers: ['maker'],
    ...overrides,
  });
}

test('an update from a past committer to the card is eligible', () => {
  const result = check({});
  assert.equal(result.eligible, true);
  assert.equal(result.card, '42_card');
  assert.match(result.basis, /maker has committed to 42_card/);
});

test('an update from someone who has not committed to the card is not eligible', () => {
  const result = check({ author: 'stranger', association: 'COLLABORATOR' });
  assert.equal(result.eligible, false);
  assert.match(result.reasons.join('\n'), /stranger has not committed to 42_card/);
});

test('an unknown committer list fails closed', () => {
  const result = check({ committers: null });
  assert.equal(result.eligible, false);
  assert.match(result.reasons.join('\n'), /Could not determine/);
});

test('a new card is eligible only once the author has contributed before', () => {
  for (const association of ['CONTRIBUTOR', 'COLLABORATOR', 'MEMBER', 'OWNER']) {
    assert.equal(check({ cardOnBase: false, committers: null, association }).eligible, true, association);
  }
  for (const association of ['FIRST_TIME_CONTRIBUTOR', 'FIRST_TIMER', 'NONE']) {
    const result = check({ cardOnBase: false, committers: null, association });
    assert.equal(result.eligible, false, association);
    assert.match(result.reasons.join('\n'), /first contribution needs a maintainer/);
  }
});

test('multiple cards, files outside the card, curation, and renames are not eligible', () => {
  const cases = {
    'two cards': [update, { status: 'M', path: 'releases/43_other/README.md' }],
    'tooling': [update, { status: 'M', path: 'tools/sitegen/src/build.js' }],
    'curation': [update, { status: 'M', path: 'tools/sitegen/src/curation/flairs.yml' }],
    'releases root': [update, { status: 'M', path: 'releases/README.md' }],
    'rename between cards': [{ status: 'R100', oldPath: 'releases/42_card/a.uf2', path: 'releases/43_other/a.uf2' }],
  };
  for (const [name, changes] of Object.entries(cases)) {
    assert.equal(check({ changes }).eligible, false, name);
  }
  assert.match(check({ changes: cases.curation }).reasons.join('\n'), /outside releases\/<card>\/: tools\/sitegen\/src\/curation\/flairs\.yml/);
});

test('website and curation updates are never eligible, whoever opens them', () => {
  const websiteOnly = [
    [{ status: 'M', path: 'tools/sitegen/src/build.js' }],
    [{ status: 'M', path: 'site/index.html' }],
    [{ status: 'M', path: '.github/workflows/validate-info.yml' }],
    [{ status: 'M', path: 'tools/sitegen/package-lock.json' }],
    [{ status: 'M', path: 'tools/sitegen/src/curation/discovery.yml' }],
  ];
  for (const changes of websiteOnly) {
    for (const association of ['OWNER', 'COLLABORATOR', 'CONTRIBUTOR']) {
      const result = check({ changes, association, committers: ['maker'] });
      assert.equal(result.eligible, false, `${changes[0].path} by ${association}`);
      assert.match(result.reasons.join('\n'), /Changes no program card/);
    }
  }
});

test('validation errors, drafts, and deleting the card are not eligible', () => {
  assert.match(check({ errorCount: 2 }).reasons.join('\n'), /2 errors/);
  assert.match(check({ draft: true }).reasons.join('\n'), /draft/);
  assert.match(check({ cardInHead: false }).reasons.join('\n'), /Deletes card 42_card/);
});

test('curation-only and tooling-only PRs touch no cards', () => {
  assert.equal(touchesNoCards([
    { status: 'M', path: 'tools/sitegen/src/curation/flairs.yml' },
    { status: 'M', path: 'tools/sitegen/src/curation/discovery.yml' },
  ]), true);
  assert.equal(touchesNoCards([update]), false);
});

test('card committers are collected across pages and fail closed on API errors', async () => {
  const pages = {
    first: { body: [{ author: { login: 'maker' } }, { author: null }], link: '<https://api.github.com/next>; rel="next"' },
    'https://api.github.com/next': { body: [{ author: { login: 'helper' } }, { author: { login: 'maker' } }], link: null },
  };
  const requested = [];
  const fetchImpl = async url => {
    requested.push(url);
    const page = url.startsWith('https://api.github.com/repos/') ? pages.first : pages[url];
    return { ok: true, json: async () => page.body, headers: { get: () => page.link } };
  };
  const logins = await fetchCardCommitters({ repo: 'owner/repo', card: '42_card', ref: 'abc', fetchImpl });
  assert.deepEqual(logins, ['helper', 'maker']);
  assert.match(requested[0], /repos\/owner\/repo\/commits\?sha=abc&path=releases%2F42_card&per_page=100/);

  const failing = async () => ({ ok: false, json: async () => ({}), headers: { get: () => null } });
  assert.equal(await fetchCardCommitters({ repo: 'owner/repo', card: '42_card', ref: 'abc', fetchImpl: failing }), null);
});

test('eligibility markdown explains the outcome', () => {
  assert.match(reportEligibilityMarkdown(check({})), /Eligible\.\*\* maker has committed/);
  const markdown = reportEligibilityMarkdown(check({ draft: true }));
  assert.match(markdown, /Not eligible/);
  assert.match(markdown, /- The pull request is a draft\./);
  assert.match(markdown, /Report only/);
});
