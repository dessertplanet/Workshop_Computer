import { fsAsync as fs, ensureDir, writeFileEnsured, listSubdirs } from './utils/fs.js';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { detectRepoFromGit, detectRefFromGit } from './utils/git.js';
import { makeRawUrl as makeRawUrlExternal } from './links.js';
import { renderLayout } from './render/layout.js';
import { sevenSegmentSvg, mapStatusToClass, renderMetaList } from './render/components.js';
import { formatVersion } from './utils/strings.js';
import { discoverRelease as discoverReleaseMod } from './discover/release.js';
import { githubPagesBase, copyWebAssets } from './discover/webEditor.js';
import { getInfoYamlSchemaAdapter } from './schema/schemaAdapter.js';
import { renderCardArticle } from './render/cardPage.js';

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
const PAGES_BASE = githubPagesBase(REPO);
const schemaAdapter = getInfoYamlSchemaAdapter();


// (info parsing handled in discover/release.js)

function makeRawUrl(relPathFromRepoRoot) {
  return makeRawUrlExternal(REPO, BRANCH, relPathFromRepoRoot);
}

async function discoverRelease(folderName) {
  const outPrograms = path.join(OUT_DIR, 'programs');
  return discoverReleaseMod(RELEASES_DIR, folderName, outPrograms, makeRawUrl, PAGES_BASE, REPO, BRANCH);
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
  const editorLink = (info.editor != '');
  const editor = info.editor;
  const date = info.date || '';
  const searchText = normalizeSpaces(`${display.title} ${desc}`).toLowerCase();
  return `<article class="card" data-creator="${escapeAttr(creator)}" data-language="${escapeAttr(language)}" data-type="${escapeAttr(typeOrStatus)}" data-type-key="${escapeAttr(typeKeyVal)}" data-date="${escapeAttr(date)}" data-search="${escapeAttr(searchText)}">
<div class="card-head">
    <h3 class="card-title">${display.title}</h3>
    ${num ? `<span class="card-num" aria-label="Program ${num}">${sevenSegmentSvg(num)}</span>` : ''}
  </div>
  <div class="card-body">
    <p>${desc}</p>
    ${metaItems ? `<ul class="meta-list">${metaItems}</ul>` : ''}
    <div class="actions actions-grid">
      <a class="btn wide" href="programs/${slug}/index.html">📄 View Details</a>
      ${latestUf2 ? `<a class="btn download" href="${latestUf2.url}" data-uf2-url="${latestUf2.url}">💾 Download</a>` : `<span class="btn disabled" aria-disabled="true">💾 Download</span>`}
      ${editorLink ? `<a class="btn editor" href="${editor}">🛠️ Web Editor</a>` : `<span class="btn disabled" aria-disabled="true">🛠️ Web Editor</span>`}
    </div>
  </div>
</article>`;
}
//  

function detailPage(rel) {
  const { info, docs, readmeHtml, card } = rel;
  const uf2Url = rel.latestUf2?.url || '';
  const yamlUrl = card?.source_file
    ? `https://github.com/${REPO}/blob/${BRANCH}/${card.source_file}`
    : `https://github.com/${REPO}`;

  const readmeSection = `<div class="program-card-section"><h3>README</h3><div class="markdown-body">${readmeHtml}</div></div>`;
  const pdfSection = docs.length ? `
    <div class="program-card-section docs-section">
      <h3>Documentation PDF</h3>
      <object data="${docs[0].url}" type="application/pdf" width="100%" height="700px">
        <p>PDF preview not available.</p>
      </object>
      <div style="margin-top:16px;text-align:center">
        <a class="btn download" href="${docs[0].url}" download>📜 Download ${docs[0].name}</a>
      </div>
      ${docs.length > 1 ? `<ul class="docs-list">${docs.slice(1).map(d => `<li><a class="btn download" href="${d.url}" download>📄 ${d.name}</a></li>`).join('')}</ul>` : ''}
    </div>` : '';

  const article = renderCardArticle({
    card,
    panelImg: '../../assets/program_cards/Standalone_computer_rev1.svg',
    yamlUrl,
    uf2Url,
    extraDocs: readmeSection + pdfSection,
  });

  return renderLayout({
    title: `${info.title} – Workshop Computer`,
    relativeRoot: '../..',
    repoUrl: `https://github.com/${REPO}`,
    content: `
${article}
<div class="actions actions-duo">
  <a class="btn" href="../../index.html">⬅️ Back to All Programs</a>
  <a class="btn" href="#page-top">⬆️ Back to Top</a>
</div>
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

  // Copy GitHub-flavoured markdown stylesheet (used by embedded README bodies)
  await fs.copyFile(
    path.join(ROOT, 'tools', 'sitegen', 'assets', 'github-markdown.css'),
    path.join(OUT_DIR, 'assets', 'github-markdown.css')
  );

  // Copy program-card detail-page stylesheet
  await fs.copyFile(
    path.join(ROOT, 'tools', 'sitegen', 'assets', 'program-cards.css'),
    path.join(OUT_DIR, 'assets', 'program-cards.css')
  );

  // Copy program-card panel diagram asset
  const panelSrcDir = path.join(ROOT, 'tools', 'sitegen', 'assets', 'program_cards');
  const panelDestDir = path.join(OUT_DIR, 'assets', 'program_cards');
  await ensureDir(panelDestDir);
  for (const f of await fs.readdir(panelSrcDir)) {
    await fs.copyFile(path.join(panelSrcDir, f), path.join(panelDestDir, f));
  }

  // Copy JS assets (picoboot / uf2 libs for WebUSB programmer)
  const jsSrcDir = path.join(ROOT, 'tools', 'sitegen', 'assets', 'js');
  const jsDestDir = path.join(OUT_DIR, 'assets', 'js');
  await ensureDir(jsDestDir);
  for (const f of await fs.readdir(jsSrcDir)) {
    if (f.endsWith('.js')) await fs.copyFile(path.join(jsSrcDir, f), path.join(jsDestDir, f));
  }

  const releaseFolders = (await listSubdirs(RELEASES_DIR)).sort();

  const releases = [];
  const normalizedCards = [];
  const rawInfoIndex = [];
  const typeMap = new Map(); // key -> display (original YAML text, normalized spacing)
  const creatorSet = new Set();
  const languageSet = new Set();
  for (const folder of releaseFolders) {
    const relPath = path.join(RELEASES_DIR, folder);
    const hasFiles = (await fs.readdir(relPath)).length > 0;
    if (!hasFiles) continue;
    const rel = await discoverRelease(folder);
    releases.push(rel);
    normalizedCards.push(rel.card);
    if (rel.rawInfoSource) {
      rawInfoIndex.push({
        id: rel.folderName,
        slug: rel.slug,
        sourceFile: rel.card.source_file,
        path: `raw-info/${rel.folderName}/info.yaml`,
      });
    }
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
    <p>The Workshop Computer is part of the <a href="https://www.musicthing.co.uk/workshopsystem/">Music Thing Workshop System</a>.  This site provides access to all the available program cards, their documentation, and downloadable firmware files (.uf2). On Chrome and some other browsers, cards can be programmed directly from this site.</p>
  </div>
</article>
<div class="filter-bar card" aria-label="Filter programs">
  <div class="card-body">
    <div style="margin-bottom:12px; padding-bottom:12px; border-bottom:1px solid var(--border);">
      <div class="search-wrapper">
        <input type="text" id="filter-search" placeholder="Search programs..." class="search-input" aria-label="Search programs">
        <button id="search-clear" class="search-clear" aria-label="Clear search" type="button">✕</button>
      </div>
    </div>
    <div class="filter-row">
      <div class="filter-group">
        <label for="filter-type">Release type</label>
        <select id="filter-type">${typeOptions}</select>
      </div>
      <div class="filter-group">
        <label for="filter-creator">Creator</label>
        <select id="filter-creator">${creatorOptions}</select>
      </div>
      <div class="filter-group">
        <label for="filter-language">Language</label>
        <select id="filter-language">${languageOptions}</select>
      </div>
    </div>
    <div style="margin-top:12px; padding-top:12px; border-top:1px solid var(--border);" class="filter-row">
      <div class="filter-group">
        <label for="sort-latest" style="display:flex;align-items:center;cursor:pointer">
          Sort by Latest Update
          <input type="checkbox" id="sort-latest" style="margin-left:8px;transform:scale(1.2)">
        </label>
      </div>
    </div>
  </div>
</div>
<div class="grid">${cards}</div>`
  });
  await writeFileEnsured(path.join(OUT_DIR, 'index.html'), indexHtml);
  await writeFileEnsured(path.join(OUT_DIR, 'cards.json'), JSON.stringify({
    schema: {
      id: schemaAdapter.id,
      version: schemaAdapter.version,
      source: schemaAdapter.source,
      requiredFields: schemaAdapter.requiredFields().map(field => field.path),
    },
    cards: normalizedCards,
  }, null, 2));
  await writeFileEnsured(path.join(OUT_DIR, 'raw-info', 'index.json'), JSON.stringify(rawInfoIndex, null, 2));

  for (const rel of releases) {
    if (rel.rawInfoSource) {
      await writeFileEnsured(path.join(OUT_DIR, 'raw-info', rel.folderName, 'info.yaml'), rel.rawInfoSource);
    }
  }

  for (const rel of releases) {
    const base = path.join(OUT_DIR, 'programs', rel.slug);
    await ensureDir(base);
    const html = detailPage(rel);
    await writeFileEnsured(path.join(base, 'index.html'), html);

    if (rel.web?.copySrc) {
      const webDest = path.join(base, rel.web.siteSubdir || 'web');
      await copyWebAssets(rel.web.copySrc, webDest);
    }
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

