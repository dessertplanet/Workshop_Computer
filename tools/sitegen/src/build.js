import { fsAsync as fs, ensureDir, writeFileEnsured, listSubdirs } from './utils/fs.js';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { detectRepoFromGit, detectRefFromGit } from './utils/git.js';
import { makeRawUrl as makeRawUrlExternal } from './links.js';
import { renderLayout } from './render/layout.js';
import { sevenSegmentSvg, mapStatusToClass, renderMetaList, renderActionButtons } from './render/components.js';
import { formatVersion } from './utils/strings.js';
import { discoverRelease as discoverReleaseMod } from './discover/release.js';

// ========== Path & Globals ==========
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const ROOT = path.resolve(__dirname, '../../..');
const RELEASES_DIR = path.join(ROOT, 'releases');
const OUT_DIR = path.join(ROOT, 'site');

// Resolve repo details (for GitHub raw links)
const DEFAULT_REPO = 'TomWhitwell/Workshop_Computer';
const DEFAULT_BRANCH = 'main';


/** Try to infer repo slug (owner/name) from local git */

// On GitHub, prefer GITHUB_REPOSITORY and GITHUB_SHA (falls back to branch name). Locally, fall back to git.
const REPO = process.env.GITHUB_REPOSITORY || detectRepoFromGit() || DEFAULT_REPO;
const BRANCH = process.env.GITHUB_SHA || process.env.GITHUB_REF_NAME || detectRefFromGit() || DEFAULT_BRANCH;


// (info parsing handled in discover/release.js)

function makeRawUrl(relPathFromRepoRoot) {
  return makeRawUrlExternal(REPO, BRANCH, relPathFromRepoRoot);
}

async function discoverRelease(folderName) {
  const outPrograms = path.join(OUT_DIR, 'programs');
  return discoverReleaseMod(RELEASES_DIR, folderName, outPrograms, makeRawUrl);
}

function escapeAttr(s) {
  return String(s ?? '').replaceAll('"', '&quot;');
}

// Helpers for Release Type: preserve original YAML text, but dedupe case/space-insensitively
function normalizeSpaces(s) {
  return String(s || '').trim().replace(/\s+/g, ' ');
}
function typeKey(raw) {
  // Lowercase, remove punctuation/symbols, collapse spaces
  const s = normalizeSpaces(raw).toLowerCase().replace(/[^a-z0-9\s]+/g, ' ');
  return s.replace(/\s+/g, ' ').trim();
}

function releaseCard(rel) {
  const { info, slug, display } = rel;
  const desc = info.description ? String(info.description) : 'No description available.';
  const num = display.number;
  const creator = info.creator || 'Unknown';
  const version = formatVersion(info.version || 'unknown');
  const language = info.language || 'Unknown';
  const typeOrStatus = normalizeSpaces(info.type || info.status || 'Unknown');
  const typeKeyVal = typeKey(typeOrStatus);
  const statusRaw = (info.status || 'Unknown').toString();
  const statusClass = mapStatusToClass(statusRaw);
    const metaItems = renderMetaList({ creator, version, language, statusRaw, statusClass });
	const latestUf2 = rel.latestUf2;
	const editorLink = (rel.editor != '');
	const editor = rel.editor;
  return `<article class="card" data-creator="${escapeAttr(creator)}" data-language="${escapeAttr(language)}" data-type="${escapeAttr(typeOrStatus)}" data-type-key="${escapeAttr(typeKeyVal)}">
  <div class="card-head">
    <h3 class="card-title">${display.title}</h3>
    ${num ? `<span class="card-num" aria-label="Program ${num}">${sevenSegmentSvg(num)}</span>` : ''}
  </div>
  <div class="card-body">
    <p>${desc}</p>
    ${metaItems ? `<ul class="meta-list">${metaItems}</ul>` : ''}
    <div class="actions">
  <a class="btn" href="programs/${slug}/index.html">📄 View Details</a>
  ${latestUf2 ? `<a class="btn download" href="${latestUf2.url}" download>💾 Download</a>` : ''}
  ${editorLink ? `<a class="btn editor" href="${editor}" download>Editor</a>` : ''}
    </div>
  </div>
</article>`;
}

function detailPage(rel) {
  const { info, slug, display, downloads, docs, readmeHtml } = rel;
  const desc = info.description ? String(info.description) : 'No description available.';
  const creator = info.creator || 'unknown';
  const version = formatVersion(info.version || 'unknown');
  const language = info.language || 'unknown';
  const statusRaw = (info.status || 'unknown').toString();
  const statusClass = mapStatusToClass(statusRaw);
    const metaItems = renderMetaList({ creator, version, language, statusRaw, statusClass });
  const num = display.number;
  const uf2Downloads = (downloads || []).filter(d => /\.uf2$/i.test(d.name));

  return renderLayout({
    title: `${info.title} – Workshop Computer`,
    relativeRoot: '../..',
  repoUrl: `https://github.com/${REPO}`,
    content: `
<article class="card large">
  <div class="card-head">
    <h1 class="card-title">${display.title}</h1>
    ${num ? `<span class="card-num" aria-label="Program ${num}">${sevenSegmentSvg(num)}</span>` : ''}
  </div>
  <div class="card-body">
    <p>${desc}</p>
    ${metaItems ? `<ul class="meta-list">${metaItems}</ul>` : ''}
  <div class="actions">${renderActionButtons(uf2Downloads)}</div>

    <div class="section">
      <h2>README</h2>
      <hr style="margin-top:32px;">
      <div class="readme markdown-body">${readmeHtml}</div>
    </div>

    <!-- PDF Preview Section -->
    ${docs.length ? `
    <div class="section">
      <h2>Documentation PDF</h2>
        <hr style="margin-top:16px; margin-bottom:16px;">
        <object data="${docs[0].url}" type="application/pdf" width="100%" height="700px">
          <p>PDF preview not available.</p>
        </object>
        <div style="margin-top:16px;text-align:center">
          <a class="btn download" href="${docs[0].url}" download>Download ${docs[0].name}</a>
        </div>
        ${docs.length > 1 ? `
        <div class="section">
          <h3>Other PDFs</h3>
          <ul class="docs-list">
        ${docs.slice(1).map(d => `<li><a class="btn download" href="${d.url}" download>📄 ${d.name}</a></li>`).join('')}
          </ul>
        </div>
        ` : ''}
    </div>
    ` : ''}
  </div>
  <div class="actions" style="margin-top:16px; margin-bottom:24px; text-align:center;">${renderActionButtons(uf2Downloads)}</div>
</article>
`
  });
}

async function build() {
  await ensureDir(OUT_DIR);
  await ensureDir(path.join(OUT_DIR, 'assets'));
  // Copy physical CSS asset
  const cssSrc = path.join(ROOT, 'tools', 'sitegen', 'assets', 'style.css');
  const cssDest = path.join(OUT_DIR, 'assets', 'style.css');
  await fs.copyFile(cssSrc, cssDest);

  const releaseFolders = (await listSubdirs(RELEASES_DIR)).sort();

  const releases = [];
  const typeMap = new Map(); // key -> display (original YAML text, normalized spacing)
  const creatorSet = new Set();
  const languageSet = new Set();
  for (const folder of releaseFolders) {
    const relPath = path.join(RELEASES_DIR, folder);
    const hasFiles = (await fs.readdir(relPath)).length > 0;
    if (!hasFiles) continue;
    const rel = await discoverRelease(folder);
    releases.push(rel);
    const info = rel.info || {};
    const typeRaw = (info.type || info.status || 'Unknown').toString();
    const key = typeKey(typeRaw) || 'unknown';
    const display = normalizeSpaces(typeRaw) || 'Unknown';
    if (!typeMap.has(key)) typeMap.set(key, display);
    const creatorVal = (info.creator || 'Unknown').toString().trim() || 'Unknown';
    const languageVal = (info.language || 'Unknown').toString().trim() || 'Unknown';
    creatorSet.add(creatorVal);
    languageSet.add(languageVal);
  }

  // Index page
  const cards = releases.map(releaseCard).join('\n');
  const typeOptions = ['<option value="">All</option>'].concat(
    Array.from(typeMap.entries())
      .sort((a,b)=>a[1].toLowerCase().localeCompare(b[1].toLowerCase()))
      .map(([,v])=>`<option value="${escapeAttr(v)}">${v}</option>`)
  ).join('');
  const creatorOptions = ['<option value="">All</option>'].concat(
    Array.from(creatorSet).sort((a,b)=>a.localeCompare(b)).map(v=>`<option value="${escapeAttr(v)}">${v}</option>`)
  ).join('');
  const languageOptions = ['<option value="">All</option>'].concat(
    Array.from(languageSet).sort((a,b)=>a.localeCompare(b)).map(v=>`<option value="${escapeAttr(v)}">${v}</option>`)
  ).join('');
  const indexHtml = renderLayout({
    title: 'Workshop Computer Program Cards',
    relativeRoot: '.',
  repoUrl: `https://github.com/${REPO}`,
    content: `
<article class="card intro-card">
  <div class="card-body">
    <p>The Workshop Computer is part of the <a href="https://www.musicthing.co.uk/workshopsystem/">Music Thing Workshop System</a>.  This site provides access to all the available program cards, their documentation, and downloadable firmware files (.uf2)</p>
  </div>
</article>
<div class="filter-bar card" aria-label="Filter programs">
  <div class="card-body">
    <div class="filter-row">
      <label for="filter-type">Release type</label>
      <select id="filter-type">${typeOptions}</select>
      <label for="filter-creator">Creator</label>
      <select id="filter-creator">${creatorOptions}</select>
      <label for="filter-language">Language</label>
      <select id="filter-language">${languageOptions}</select>
    </div>
  </div>
</div>
<div class="grid">${cards}</div>`
  });
  await writeFileEnsured(path.join(OUT_DIR, 'index.html'), indexHtml);

  // Each release detail page (no copying of release assets)
  for (const rel of releases) {
    const base = path.join(OUT_DIR, 'programs', rel.slug);
    await ensureDir(base);
    const html = detailPage(rel);
    await writeFileEnsured(path.join(base, 'index.html'), html);
  }

  // 404 fallback
  await writeFileEnsured(path.join(OUT_DIR, '404.html'), renderLayout({
    title: 'Not found',
    relativeRoot: '.',
  repoUrl: `https://github.com/${REPO}`,
    content: '<h1>404</h1><p>Page not found.</p>'
  }));

  console.log(`Built site with ${releases.length} releases -> ${OUT_DIR}`);
}

build().catch(err => {
  console.error(err);
  process.exit(1);
});

