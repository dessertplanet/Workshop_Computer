// Tests for the shared validation pipeline (parseSource -> validateInfoYaml).
// This seam is consumed by the site build, the author preview, and CI, so its
// behavior — especially "never throws" and diagnostic shape — must stay stable.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { parseSource } from '../src/validate/parseSource.js';
import { validateInfoYaml } from '../src/validate/validateInfoYaml.js';

function validate(yamlText) {
  return validateInfoYaml(parseSource(yamlText, 'test/info.yaml'));
}

function ruleIds(result) {
  return result.diagnostics.map(d => d.ruleId);
}

test('canonical card validates clean', () => {
  const result = validate(`
Name: Test Card
Creator: Someone
Language: C++
Version: "1.0"
Status: Released
short-description: A test card.
summary: A longer summary of the test card.
tags: [midi-host, utility]
`);
  assert.equal(result.ok, true);
  assert.equal(result.errorCount, 0);
});

test('missing Name is an error, legacy Title satisfies it', () => {
  const missing = validate(`Creator: Someone\nLanguage: C\nVersion: "1"\nStatus: WIP\n`);
  assert.ok(ruleIds(missing).includes('required-core-fields'));
  assert.ok(missing.diagnostics.some(d => d.message.includes('"Name"')));

  const withTitle = validate(`Title: Legacy Card\nCreator: S\nLanguage: C\nVersion: "1"\nStatus: WIP\n`);
  assert.ok(!withTitle.diagnostics.some(d => d.message.includes('"Name"')));
});

test('broken YAML yields a yaml-syntax diagnostic with a line, never throws', () => {
  const result = validate('Name: ok\n  bad: [unclosed\n');
  assert.equal(result.ok, false);
  const syntax = result.diagnostics.find(d => d.ruleId === 'yaml-syntax');
  assert.ok(syntax);
  assert.equal(typeof syntax.line, 'number');
});

test('diagnostics are anchored to the offending key source line', () => {
  const result = validate(`Name: X\nCreator: S\nLanguage: C\nVersion: "1"\nStatus: WIP\ntags:\n  - Not Kebab\n`);
  const tagDiag = result.diagnostics.find(d => d.ruleId === 'tags-format');
  assert.ok(tagDiag);
  assert.equal(tagDiag.line, 6); // the `tags:` key line
});

test('legacy object tags and non-kebab tags warn', () => {
  const result = validate(`Name: X\ntags:\n  - id: midi\n    label: MIDI\n  - Not Kebab\n`);
  const messages = result.diagnostics.filter(d => d.ruleId === 'tags-format').map(d => d.message);
  assert.ok(messages.some(m => m.includes('legacy shape')));
  assert.ok(messages.some(m => m.includes('"Not Kebab"')));
});

test('uf2 download without sha256 is an error', () => {
  const result = validate(`
Name: X
uf2:
  - name: firmware
    download:
      url: https://example.com/fw.uf2
`);
  assert.ok(result.diagnostics.some(d =>
    d.ruleId === 'uf2-entries' && d.severity === 'error' && d.path === 'uf2[0].download.sha256'));
});

test('when cannot combine z and panel', () => {
  const result = validate(`
Name: X
panel:
  inputs:
    - id: audio-in-1
      when: { z: up, panel: alt }
`);
  assert.ok(result.diagnostics.some(d =>
    d.severity === 'error' && d.path === 'panel.inputs[0].when'));
});

test('generated card-model keys in source are flagged once', () => {
  const result = validate(`Name: X\nslug: x\nurl: programs/x/\ndownload_url: https://example.com\n`);
  const hits = result.diagnostics.filter(d => d.ruleId === 'generated-model-shape');
  assert.equal(hits.length, 1);
});

test('a crashing rule becomes a diagnostic, not an exception', () => {
  const boom = { id: 'boom', check() { throw new Error('nope'); } };
  const result = validateInfoYaml(parseSource('Name: X\n'), { rules: [boom] });
  assert.ok(result.diagnostics.some(d => d.ruleId === 'rule-crash:boom'));
});
