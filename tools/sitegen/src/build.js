import { fsAsync as fs, ensureDir, writeFileEnsured, listSubdirs } from './utils/fs.js';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { detectRepoFromGit, detectRefFromGit } from './utils/git.js';
import { makeRawUrl as makeRawUrlExternal } from './links.js';
import { renderLayout } from './render/layout.js';
import { discoverRelease as discoverReleaseMod } from './discover/release.js';
import { githubPagesBase, copyWebAssets } from './discover/webEditor.js';
import { getInfoYamlSchemaAdapter } from './schema/schemaAdapter.js';
import { renderCardArticle } from './render/cardPage.js';
import { renderDiscovery, renderArchive, renderTile } from './render/discovery.js';
import { curation } from './curation/index.js';
import { parseSource } from './validate/parseSource.js';
import { validateInfoYaml } from './validate/validateInfoYaml.js';

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

function detailPage(rel) {
  const { docs, readmeHtml, card } = rel;
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
    title: `${card.title} – Workshop Computer`,
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
  const validationResults = []; // non-fatal info.yaml conformance pass
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
      // Validate the raw author source against the canonical schema. This is a
      // non-fatal reporting pass: it never blocks the build.
      const source = parseSource(rel.rawInfoSource, `releases/${rel.folderName}/info.yaml`);
      validationResults.push(validateInfoYaml(source));
    }
    const meta = rel.card?.metadata || {};
    const typeRaw = (meta.status || 'Unknown').toString();
    const key = typeKey(typeRaw) || 'unknown';
    const display = normalizeSpaces(typeRaw) || 'Unknown';
    if (!typeMap.has(key)) typeMap.set(key, display);
    const creatorVal = (meta.creator || 'Unknown').toString().trim() || 'Unknown';
    const languageVal = (meta.language || 'Unknown').toString().trim() || 'Unknown';
    creatorSet.add(creatorVal);
    languageSet.add(languageVal);
  }

  // Index page
  const discoveryHtml = renderDiscovery(normalizedCards, '.');
  const resultsTiles = normalizedCards.map(card => renderTile(card, { root: '.' })).join('');
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
  const tagOptions = ['<option value="">All</option>'].concat(
    curation.availableTags.map(t=>`<option value="${escapeAttr(t.id)}">${escapeAttr(t.label)}</option>`)
  ).join('');
  const sortOptions = [
    ['', 'Default order'],
    ['updated-desc', 'Recently updated'],
    ['updated-asc', 'Oldest updated'],
    ['name-asc', 'Name A\u2013Z'],
    ['name-desc', 'Name Z\u2013A'],
    ['number-asc', 'Number (low to high)'],
    ['number-desc', 'Number (high to low)'],
  ].map(([v,l])=>`<option value="${v}">${l}</option>`).join('');
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
    <div class="search-bar-row">
      <div class="search-wrapper">
        <input type="text" id="filter-search" placeholder="Search programs..." class="search-input" aria-label="Search programs">
        <button id="search-clear" class="search-clear" aria-label="Clear search" type="button">✕</button>
      </div>
      <a class="btn secondary" href="archive/">Browse all cards →</a>
    </div>
    <details class="advanced-options">
      <summary>Advanced search</summary>
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
        <div class="filter-group">
          <label for="filter-tag">Tag</label>
          <select id="filter-tag">${tagOptions}</select>
        </div>
      </div>
      <div class="filter-row" style="margin-top:12px;">
        <div class="filter-group">
          <label for="sort-mode">Sort</label>
          <select id="sort-mode">${sortOptions}</select>
        </div>
      </div>
    </details>
  </div>
</div>
<div class="program-cards program-cards--index">
  ${discoveryHtml}
  <div id="search-results" hidden>
    <section class="program-card-shelf">
      <header class="program-card-shelf__header"><h2>Results <span id="cards-count"></span></h2></header>
      <div class="program-card-grid program-card-grid--list">${resultsTiles}</div>
      <p id="no-results" class="program-card-empty" hidden>No matching cards.</p>
    </section>
  </div>
</div>`
  });
  await writeFileEnsured(path.join(OUT_DIR, 'index.html'), indexHtml);

  // Archive page (complete one-line index, with search + sort)
  const archiveHtml = renderLayout({
    title: 'All cards – Workshop Computer',
    relativeRoot: '..',
    repoUrl: `https://github.com/${REPO}`,
    content: `
<div class="program-cards program-cards--archive">
  <header class="program-cards__title">
    <h1>All cards</h1>
    <nav class="program-cards__links" aria-label="Program card links">
      <a href="../index.html">Program cards</a>
      <a href="https://github.com/${REPO}">Make a card</a>
    </nav>
  </header>
  <div class="filter-bar card" aria-label="Search cards">
    <div class="card-body">
      <div class="search-wrapper">
        <input type="text" id="filter-search" placeholder="Search cards..." class="search-input" aria-label="Search cards">
        <button id="search-clear" class="search-clear" aria-label="Clear search" type="button">✕</button>
      </div>
      <div class="filter-actions">
        <div class="filter-group">
          <label for="sort-mode">Sort</label>
          <select id="sort-mode">${sortOptions}</select>
        </div>
      </div>
    </div>
  </div>
  ${renderArchive(normalizedCards, '..')}
</div>`
  });
  await writeFileEnsured(path.join(OUT_DIR, 'archive', 'index.html'), archiveHtml);
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
  reportValidation(validationResults);
}

/** Print a concise, non-fatal summary of the info.yaml conformance pass. */
function reportValidation(results) {
  if (!results.length) return;
  const errorCount = results.reduce((n, r) => n + r.errorCount, 0);
  const warningCount = results.reduce((n, r) => n + r.warningCount, 0);
  const failing = results.filter(r => r.errorCount > 0);
  if (!errorCount && !warningCount) {
    console.log('info.yaml validation: all cards conform to documentation/info.yaml.md.');
    return;
  }
  console.log(`info.yaml validation (non-fatal): ${errorCount} error(s), ${warningCount} warning(s) across ${results.length} card(s).`);
  if (failing.length) {
    console.log(`  ${failing.length} card(s) with errors: ${failing.map(r => r.file.replace(/^releases\//, '').replace(/\/info\.yaml$/, '')).join(', ')}`);
    console.log('  Run `npm run validate-info` for details.');
  }
}

build().catch(err => {
  console.error(err);
  process.exit(1);
});

