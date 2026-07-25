// Tests for the shared validation pipeline (parseSource -> validateInfoYaml).
// This seam is consumed by the site build, the author preview, and CI, so its
// behavior — especially "never throws" and diagnostic shape — must stay stable.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { parseSource } from '../src/validate/parseSource.js';
import { validateInfoYaml } from '../src/validate/validateInfoYaml.js';
import { getInfoYamlSchemaAdapter } from '../src/schema/schemaAdapter.js';
import { infoYamlJsonSchema } from '../src/schema/infoYamlJsonSchema.js';
import { reportGithub } from '../src/validate/reporters/index.js';
import {
  classifyDiagnostics,
  comparisonTotals,
  reportGithubComparison,
} from '../src/validate/diagnosticComparison.js';

function validate(yamlText) {
  return validateInfoYaml(parseSource(yamlText, 'test/info.yaml'));
}

function ruleIds(result) {
  return result.diagnostics.map(d => d.ruleId);
}

test('author schema fields are represented in the published JSON schema', () => {
  const adapter = getInfoYamlSchemaAdapter();
  for (const field of adapter.listFields()) {
    assert.ok(infoYamlJsonSchema.properties[field.path], `${field.path} is missing from the JSON schema`);
  }
  assert.deepEqual(
    new Set(infoYamlJsonSchema.required),
    new Set(adapter.requiredFields().map(field => field.path)),
  );
});

test('GitHub reporter emits inline annotations and a PR-check summary', () => {
  const output = reportGithub([{
    file: 'releases/42_test/info.yaml', ok: false, errorCount: 1, warningCount: 1,
    diagnostics: [
      { severity: 'error', ruleId: 'required', path: 'Name', line: 2, col: 1, message: 'Missing Name.' },
      { severity: 'warning', ruleId: 'tags', path: 'tags', message: 'Check tags.' },
    ],
  }]);
  assert.match(output, /::error file=releases\/42_test\/info\.yaml,line=2,col=1,title=info\.yaml required::Name: Missing Name\./);
  assert.match(output, /::warning file=releases\/42_test\/info\.yaml,title=info\.yaml tags::tags: Check tags\./);
  assert.match(output, /::notice title=info\.yaml validation::1 file\(s\), 1 failing — 1 error\(s\), 1 warning\(s\)\./);
});

test('PR comparison labels unchanged diagnostics as existing and changed diagnostics as new', () => {
  const baseline = [{
    file: '../../base/releases/42_test/info.yaml', diagnostics: [
      { severity: 'warning', ruleId: 'tags', path: 'tags', line: 4, message: 'Use kebab case.' },
      { severity: 'error', ruleId: 'required', path: 'Name', line: 1, message: 'Missing Name.' },
    ],
  }];
  const current = [{
    file: 'releases/42_test/info.yaml', diagnostics: [
      // Its line moved, but the underlying issue is unchanged.
      { severity: 'warning', ruleId: 'tags', path: 'tags', line: 9, message: 'Use kebab case.' },
      { severity: 'error', ruleId: 'required', path: 'Creator', line: 2, message: 'Missing Creator.' },
    ],
  }];
  const compared = classifyDiagnostics(current, baseline);
  assert.equal(compared[0].diagnostics[0].origin, 'existing');
  assert.equal(compared[0].diagnostics[1].origin, 'new');
  assert.deepEqual(comparisonTotals(compared), {
    new: { errors: 1, warnings: 0 },
    existing: { errors: 0, warnings: 1 },
    files: 1,
  });
  const annotations = reportGithubComparison(compared);
  assert.match(annotations, /title=info\.yaml EXISTING tags/);
  assert.match(annotations, /title=info\.yaml NEW required/);
});

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
  assert.ok(missing.diagnostics.some(d =>
    d.ruleId === 'ajv-schema' && d.severity === 'error' && d.path === 'Name'));

  const withTitle = validate(`Title: Legacy Card\nCreator: S\nLanguage: C\nVersion: "1"\nStatus: WIP\n`);
  assert.ok(!withTitle.diagnostics.some(d => d.message.includes('"Name"')));
});

test('documented key spelling variants validate through the canonical schema', () => {
  const result = validate(`
name: Test
short description: Short
SUMMARY: Long
language: C++
creator: Someone
version: "1.0"
status: Released
`);
  assert.equal(result.errorCount, 0);
  assert.equal(result.warningCount, 0);
});

test('boolean values are rejected for authored text while numeric versions remain compatible', () => {
  const result = validate(`
Name: true
short-description: Short
summary: Long
Language: C++
Creator: Someone
Version: 1.0
Status: Released
`);
  assert.ok(result.diagnostics.some(d => d.ruleId === 'ajv-schema' && d.path === 'Name'));
  assert.ok(!result.diagnostics.some(d => d.path === 'Version'));
});

test('current nested panel, control, LED, host, audio, and date shapes validate cleanly', () => {
  const result = validate(`
Name: Structured
short-description: Short
summary: Long
Language: C++
Creator: Someone
Version: "1.0"
Status: Released
date: 2026-07-25
audio-sample:
  - url: samples/demo.wav
    title: Demo
panel:
  inputs:
    - id: AudioIn1
      name: Audio
      type: audio
  outputs:
    - id: CVOut1
      name: Pitch
      when: { z: up }
controls:
  switch:
    up: { name: Alternate }
    middle: Normal
    down: { name: Write, description: Hold to write }
  knobs:
    - main: { name: Amount }
      x: { name: Shape, description: Selects shape }
  leds:
    - display: list
      items:
        - { id: LED0, name: Activity }
host:
  usb:
    - { name: MIDI, role: device, description: USB MIDI device }
  notes: Connect to a host.
`);
  assert.equal(result.errorCount, 0);
  assert.equal(result.warningCount, 0);
});

test('malformed nested metadata is diagnosed instead of silently ignored', () => {
  const result = validate(`
Name: Broken
short-description: Short
summary: Long
Language: C++
Creator: Someone
Version: "1.0"
Status: Released
date: yesterday
audio-sample:
  - title: Missing URL
panel:
  inputs:
    - { id: BogusJack, name: 123, type: banana }
controls:
  knobs:
    - { main: broken }
  leds:
    - { display: nonsense, items: broken }
host:
  usb: broken
`);
  for (const path of ['date', 'audio-sample.0.url', 'panel.inputs.0.id', 'panel.inputs.0.type', 'controls.knobs.0.main', 'controls.leds.0.items', 'host.usb']) {
    assert.ok(result.diagnostics.some(d => d.path === path), `missing diagnostic for ${path}`);
  }
});

test('external firmware hashes must be complete SHA-256 values', () => {
  const result = validate(`
Name: Firmware
short-description: Short
summary: Long
Language: C++
Creator: Someone
Version: "1.0"
Status: Released
uf2:
  - name: Mirror
    download:
      url: https://example.com/fw.uf2
      sha256: not-a-hash
`);
  assert.ok(result.diagnostics.some(d => d.path === 'uf2.0.download.sha256'));
});

test('filesystem consumers can validate custom panel references', () => {
  const source = parseSource(`
Name: Panels
short-description: Short
summary: Long
Language: C++
Creator: Someone
Version: "1.0"
Status: Released
controls:
  knobs:
    - when: { panel: alternate }
      main: { name: Amount }
`, 'test/info.yaml');
  const absent = validateInfoYaml(source, { customPanelsPresent: false });
  assert.ok(absent.diagnostics.some(d => d.ruleId === 'custom-panel-reference' && d.severity === 'error'));
  const unknown = validateInfoYaml(source, { customPanelsPresent: true, panelIds: ['main'] });
  assert.ok(unknown.diagnostics.some(d => d.message.includes('unknown custom panel id')));
  const valid = validateInfoYaml(source, { customPanelsPresent: true, panelIds: ['main', 'alternate'] });
  assert.ok(!valid.diagnostics.some(d => d.ruleId === 'custom-panel-reference'));
});

test('filesystem consumers can attach manifest diagnostics to the shared result', () => {
  const source = parseSource('Name: X\nshort-description: S\nsummary: L\nLanguage: C\nCreator: A\nVersion: "1"\nStatus: WIP\n');
  const result = validateInfoYaml(source, { externalDiagnostics: [{
    severity: 'error', ruleId: 'custom-panel-manifest', path: 'panels/manifest.yaml',
    message: 'Invalid custom panel manifest.',
  }] });
  assert.ok(result.diagnostics.some(d => d.ruleId === 'custom-panel-manifest'));
  assert.equal(result.errorCount, 1);
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
