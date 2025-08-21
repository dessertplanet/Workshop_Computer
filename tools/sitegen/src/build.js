import { fsAsync as fs, ensureDir, writeFileEnsured, listSubdirs, fileExists, toPosix, encodePathSegments } from './utils/fs.js';
import { execSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import YAML from 'yaml';
import { marked } from 'marked';
import { debugLog } from './utils/logger.js';
import { slugify, parseDisplayFromFolder } from './utils/strings.js';
import { detectRepoFromGit, detectRefFromGit } from './utils/git.js';
import { makeRawUrl as makeRawUrlExternal } from './links.js';
import { renderLayout } from './render/layout.js';
import { sevenSegmentSvg, mapStatusToClass, renderMetaList, renderActionButtons } from './render/components.js';
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


function normalizeInfo(raw, fallbackTitle) {
  const out = {};
  const mapKey = k => String(k || '').toLowerCase().replace(/\s+/g, '');
  for (const [k, v] of Object.entries(raw || {})) {
    out[mapKey(k)] = v;
  }
  return {
    title: out.title || out.name || fallbackTitle,
    description: out.description || '',
    language: out.language || '',
    creator: out.creator || '',
    version: out.version || '',
    status: out.status || '',
  };
}

function makeRawUrl(relPathFromRepoRoot) {
  return makeRawUrlExternal(REPO, BRANCH, relPathFromRepoRoot);
}

async function discoverRelease(folderName) {
  const outPrograms = path.join(OUT_DIR, 'programs');
  return discoverReleaseMod(RELEASES_DIR, folderName, outPrograms, makeRawUrl);
}

function releaseCard(rel) {
  const { info, slug, display } = rel;
  const desc = info.description ? String(info.description) : 'No description available.';
  const num = display.number;
  const creator = info.creator || 'unknown';
  const version = info.version || 'unknown';
  const language = info.language || 'unknown';
  const statusRaw = (info.status || 'unknown').toString();
  const statusClass = mapStatusToClass(statusRaw);
    const metaItems = renderMetaList({ creator, version, language, statusRaw, statusClass });
  const latestUf2 = rel.latestUf2;
  return `<article class="card">
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
    </div>
  </div>
</article>`;
}

function detailPage(rel) {
  const { info, slug, display, downloads, docs, readmeHtml } = rel;
  const desc = info.description ? String(info.description) : 'No description available.';
  const creator = info.creator || 'unknown';
  const version = info.version || 'unknown';
  const language = info.language || 'unknown';
  const statusRaw = (info.status || 'unknown').toString();
  const statusClass = mapStatusToClass(statusRaw);
    const metaItems = renderMetaList({ creator, version, language, statusRaw, statusClass });
  const num = display.number;
  const uf2Downloads = (downloads || []).filter(d => /\.uf2$/i.test(d.name));

  return renderLayout({
    title: `${info.title} – Workshop Computer`,
    relativeRoot: '../..',
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
      <div class="readme">${readmeHtml}</div>
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
  for (const folder of releaseFolders) {
    const relPath = path.join(RELEASES_DIR, folder);
    const hasFiles = (await fs.readdir(relPath)).length > 0;
    if (!hasFiles) continue;
    const rel = await discoverRelease(folder);
    releases.push(rel);
  }

  // Index page
  const cards = releases.map(releaseCard).join('\n');
  const indexHtml = renderLayout({
    title: 'Workshop Computer Program Cards',
    relativeRoot: '.',
    content: `
<article class="card intro-card">
  <div class="card-body">
    <p>The Workshop Computer is part of the <a href="https://www.musicthing.co.uk/workshopsystem/">Music Thing Workshop System</a>.  This site provides access to all the available program cards, their documentation, and downloadable firmware files (.uf2)</p>
  </div>
</article>
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
    content: '<h1>404</h1><p>Page not found.</p>'
  }));

  console.log(`Built site with ${releases.length} releases -> ${OUT_DIR}`);
}

build().catch(err => {
  console.error(err);
  process.exit(1);
});
