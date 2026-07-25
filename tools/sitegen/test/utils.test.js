// Tests for the pure utility helpers shared by build and browser preview.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { normalizeYamlKey, slugify, parseDisplayFromFolder, formatVersion } from '../src/utils/strings.js';
import { normalizeTags, normalizeDraft, normalizeContact, resolveAudioSample } from '../src/discover/infoFields.js';
import { extractIframeSrc, classifyAudioUrl, resolveAudioSamples } from '../src/utils/audio.js';

test('normalizeYamlKey strips spaces and hyphens, lowercases', () => {
  assert.equal(normalizeYamlKey('demo-link'), 'demolink');
  assert.equal(normalizeYamlKey('Short Description'), 'shortdescription');
});

test('slugify and parseDisplayFromFolder', () => {
  assert.equal(slugify("Chord Blimey!"), 'chord-blimey');
  assert.deepEqual(parseDisplayFromFolder('05_chord_blimey'), { number: '05', title: 'chord blimey' });
  assert.deepEqual(parseDisplayFromFolder('NoNumber'), { number: '', title: 'NoNumber' });
});

test('formatVersion adds .0 to bare integers only', () => {
  assert.equal(formatVersion('1'), '1.0');
  assert.equal(formatVersion('1.2.3'), '1.2.3');
  assert.equal(formatVersion('v2'), 'v2');
  assert.equal(formatVersion(''), '');
});

test('normalizeTags dedupes and slugs from list or delimited string', () => {
  assert.deepEqual(normalizeTags(['MIDI Host', 'midi-host', ' Utility ']), ['midi-host', 'utility']);
  assert.deepEqual(normalizeTags('midi; utility, midi'), ['midi', 'utility']);
  assert.deepEqual(normalizeTags(undefined), []);
});

test('normalizeDraft handles booleans, strings, and defaults false', () => {
  assert.equal(normalizeDraft(true), true);
  assert.equal(normalizeDraft('Yes'), true);
  assert.equal(normalizeDraft('false'), false);
  assert.equal(normalizeDraft(undefined), false);
});

test('normalizeContact drops empty blocks and normalizes platform keys', () => {
  assert.equal(normalizeContact({}), null);
  assert.equal(normalizeContact('nope'), null);
  const c = normalizeContact({ email: ' a@b.co ', social: { 'Blue Sky': 'https://bsky.app/x' } });
  assert.deepEqual(c, { email: 'a@b.co', social: { bluesky: 'https://bsky.app/x' } });
});

test('resolveAudioSample passes URLs through and resolves repo paths', () => {
  const makeRawUrl = rel => `https://raw.test/${rel}`;
  assert.equal(resolveAudioSample('https://x.com/a.mp3', 'releases/05_x', makeRawUrl), 'https://x.com/a.mp3');
  assert.equal(resolveAudioSample('demo.mp3', 'releases/05_x', makeRawUrl), 'https://raw.test/releases/05_x/demo.mp3');
  assert.equal(resolveAudioSample('', 'releases/05_x', makeRawUrl), '');
});

test('extractIframeSrc pulls src and height from pasted embed code', () => {
  const html = '<iframe style="border: 0; width: 100%; height: 120px;" src="https://bandcamp.com/EmbeddedPlayer/track=1" seamless>';
  assert.deepEqual(extractIframeSrc(html), { src: 'https://bandcamp.com/EmbeddedPlayer/track=1', height: 120 });
  assert.equal(extractIframeSrc('<p>no iframe</p>'), null);
});

test('classifyAudioUrl distinguishes soundcloud, bandcamp embed, file, link', () => {
  assert.equal(classifyAudioUrl('https://soundcloud.com/artist/track').kind, 'soundcloud');
  assert.equal(classifyAudioUrl('https://w.soundcloud.com/player/?url=x').embedUrl, 'https://w.soundcloud.com/player/?url=x');
  assert.equal(classifyAudioUrl('https://artist.bandcamp.com/EmbeddedPlayer/track=1').kind, 'bandcamp');
  assert.equal(classifyAudioUrl('https://artist.bandcamp.com/track/song').kind, 'link');
  assert.equal(classifyAudioUrl('https://x.com/demo.mp3?v=2').kind, 'file');
  assert.equal(classifyAudioUrl('https://example.com/page').kind, 'link');
});

test('resolveAudioSamples handles strings, objects, and pasted iframes', () => {
  const items = resolveAudioSamples(
    [{ url: 'https://x.com/a.mp3', title: 'Demo' }, 'local.wav', ''],
    rel => `https://raw.test/${rel}`
  );
  assert.equal(items.length, 2);
  assert.deepEqual(items[0], { kind: 'file', url: 'https://x.com/a.mp3', host: 'x.com', title: 'Demo' });
  assert.equal(items[1].url, 'https://raw.test/local.wav');
});
