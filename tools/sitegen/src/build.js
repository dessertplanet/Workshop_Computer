import { promises as fs } from 'node:fs';
import { execSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import YAML from 'yaml';
import { marked } from 'marked';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const ROOT = path.resolve(__dirname, '../../..');
const RELEASES_DIR = path.join(ROOT, 'releases');
const OUT_DIR = path.join(ROOT, 'site');

// Resolve repo details (for GitHub raw links)
const DEFAULT_REPO = 'TomWhitwell/Workshop_Computer';
const DEFAULT_BRANCH = 'main';

function detectRepoFromGit() {
  try {
    const url = execSync('git config --get remote.origin.url', { cwd: ROOT, encoding: 'utf8' }).trim();
    // Support ssh and https remotes
    // Examples:
    // git@github.com:owner/repo.git
    // https://github.com/owner/repo.git
    // ssh://git@github.com/owner/repo.git
    const m = url.match(/github\.com[:\/]([^\s]+?)(?:\.git)?$/i);
    if (m && m[1]) {
      return m[1].replace(/\.git$/i, '');
    }
  } catch {}
  return null;
}

function detectRefFromGit() {
  try {
    const sha = execSync('git rev-parse HEAD', { cwd: ROOT, encoding: 'utf8' }).trim();
    if (sha) return sha;
  } catch {}
  try {
    const branch = execSync('git rev-parse --abbrev-ref HEAD', { cwd: ROOT, encoding: 'utf8' }).trim();
    if (branch && branch !== 'HEAD') return branch;
  } catch {}
  return null;
}

// On GitHub, prefer GITHUB_REPOSITORY and GITHUB_SHA (falls back to branch name). Locally, fall back to git.
const REPO = process.env.GITHUB_REPOSITORY || detectRepoFromGit() || DEFAULT_REPO;
const BRANCH = process.env.GITHUB_SHA || process.env.GITHUB_REF_NAME || detectRefFromGit() || DEFAULT_BRANCH;

function slugify(name) {
  return name
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/(^-|-$)+/g, '');
}

function toPosix(p) {
  return p.replace(/\\/g, '/');
}

function encodePathSegments(p) {
  return toPosix(p).split('/').map(encodeURIComponent).join('/');
}

async function ensureDir(dir) {
  await fs.mkdir(dir, { recursive: true });
}

async function writeFileEnsured(filePath, content) {
  await ensureDir(path.dirname(filePath));
  await fs.writeFile(filePath, content);
}

async function listSubdirs(dir) {
  const entries = await fs.readdir(dir, { withFileTypes: true });
  return entries.filter(e => e.isDirectory()).map(e => e.name);
}

async function fileExists(file) {
  try {
    await fs.access(file);
    return true;
  } catch {
    return false;
  }
}

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

// Build friendly display title and number from folder name when info.title is missing
function parseDisplayFromFolder(folderName) {
  let number = '';
  let base = folderName;
  const m = folderName.match(/^(\d+)[-_\s]+(.+)$/);
  if (m) {
    number = m[1];
    base = m[2];
  }
  // Replace separators with spaces
  base = base.replace(/[_-]+/g, ' ').trim();
  // Title-case words (simple heuristic)
  base = base.split(/\s+/).map(w => w.charAt(0).toUpperCase() + w.slice(1).toLowerCase()).join(' ');
  return { number, title: base };
}

function sevenSegmentSvg(numStr) {
  const DIGIT_MAP = {
    '0': ['a','b','c','d','e','f'],
    '1': ['b','c'],
    '2': ['a','b','g','e','d'],
    '3': ['a','b','g','c','d'],
    '4': ['f','g','b','c'],
    '5': ['a','f','g','c','d'],
    '6': ['a','f','g','c','d','e'],
    '7': ['a','b','c'],
    '8': ['a','b','c','d','e','f','g'],
    '9': ['a','b','c','d','f','g']
  };
  const digits = String(numStr || '').replace(/[^0-9]/g, '') || '0';
  // Scaled by 1.5x, with thinner segments and slightly longer verticals
  const W = 42, H = 72, T = 7, PAD = 3, GAP = 6, DIGIT_GAP = 9;
  const half = H / 2;
  const vertLenTop = half - T - (PAD + GAP);
  const vertLenBot = vertLenTop;
  function segRects(xOffset, onSet) {
    const rx = 3, ry = 3;
    const rects = [];
    // a
    rects.push({ id:'a', x: xOffset + 7.5, y: PAD, w: W - 15, h: T });
    // d
    rects.push({ id:'d', x: xOffset + 7.5, y: H - T - PAD, w: W - 15, h: T });
    // g
    rects.push({ id:'g', x: xOffset + 7.5, y: half - T/2, w: W - 15, h: T });
    // f (upper-left)
    rects.push({ id:'f', x: xOffset + PAD, y: PAD + T + (GAP - T/2), w: T, h: vertLenTop });
    // e (lower-left)
    rects.push({ id:'e', x: xOffset + PAD, y: half + GAP, w: T, h: vertLenBot });
    // b (upper-right)
    rects.push({ id:'b', x: xOffset + W - T - PAD, y: PAD + T + (GAP - T/2), w: T, h: vertLenTop });
    // c (lower-right)
    rects.push({ id:'c', x: xOffset + W - T - PAD, y: half + GAP, w: T, h: vertLenBot });
    return rects.map(r => {
      const on = onSet.has(r.id);
      const fill = '#93820c';
      const opacity = on ? '1' : '0.3';
      return `<rect class=\"seg ${on ? 'on' : 'off'} ${r.id}\" x=\"${r.x}\" y=\"${r.y}\" width=\"${r.w}\" height=\"${r.h}\" rx=\"${rx}\" ry=\"${ry}\" fill=\"${fill}\" fill-opacity=\"${opacity}\"/>`;
    }).join('');
  }
  const totalWidth = digits.length * W + (digits.length - 1) * DIGIT_GAP;
  let x = 0;
  const parts = [];
  for (const ch of digits) {
    const onSet = new Set(DIGIT_MAP[ch] || []);
    parts.push(segRects(x, onSet));
    x += W + DIGIT_GAP;
  }
  return `<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"${totalWidth}\" height=\"${H}\" viewBox=\"0 0 ${totalWidth} ${H}\" aria-hidden=\"true\" focusable=\"false\" role=\"img\">${parts.join('')}</svg>`;
}

function renderLayout({ title, content, relativeRoot = '.' }) {
  return `<!doctype html>
<html lang="en" class="theme-dark" data-theme="dark">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>${title ? String(title).replace(/</g, '&lt;') : 'Workshop Computer'}</title>
  <script>(function(){try{var k='wc-theme';var d=document.documentElement;var prefersDark=window.matchMedia&&window.matchMedia('(prefers-color-scheme: dark)').matches;var t=localStorage.getItem(k);if(t!=='dark'&&t!=='light'){t=prefersDark?'dark':'light';}d.classList.remove('theme-dark','theme-light');d.classList.add('theme-'+t);d.setAttribute('data-theme',t);}catch(e){}})();</script>
  <link rel="stylesheet" href="${relativeRoot}/assets/style.css" />
</head>
<body>
  <header class="site-header">
    <div class="container header-bar">
      <h1 class="site-title"><a href="${relativeRoot}/index.html">Workshop Computer Releases</a></h1>
      <button id="themeToggle" class="theme-toggle" type="button" role="switch" aria-checked="true" aria-label="Toggle color scheme">
        <span class="track"><span class="thumb"></span><span class="icons" aria-hidden="true">☀️<span class="gap"></span>🌙</span></span>
      </button>
    </div>
  </header>
  <main class="container">
    ${content}
  </main>
  <footer class="site-footer">
    <div class="container">
      <p>Built from this repository's releases folder. Deployed via GitHub Pages.</p>
    </div>
  </footer>
  <script>(function(){var btn=document.getElementById('themeToggle');if(!btn)return;var k='wc-theme';function cur(){return document.documentElement.getAttribute('data-theme')||'dark';}function set(t){try{localStorage.setItem(k,t);}catch(e){}var d=document.documentElement;d.classList.remove('theme-dark','theme-light');d.classList.add('theme-'+t);d.setAttribute('data-theme',t);update();}function update(){var t=cur();btn.setAttribute('aria-checked',String(t==='dark'));}btn.addEventListener('click',function(){set(cur()==='dark'?'light':'dark');});update();})();</script>
</body>
</html>`;
}

const BASE_CSS = `:root{--bg:#0b0d10;--card:#11151a;--muted:#9aa6b2;--text:#e6edf3;--accent:#2f81f7;--border:#27313b;--ok:#3fb950;--toggle-track:#0e1217;--toggle-thumb:#ffffff;--pcb-start:#2d7c30;--pcb-end:#195f20}
.theme-dark{--thumb-x:26px}
.theme-light{--bg:#f7f8fa;--card:#ffffff;--muted:#5b6875;--text:#0b1220;--accent:#0969da;--border:#d0d7de;--ok:#1a7f37;--toggle-track:#e9eef3;--toggle-thumb:#0b1220;--thumb-x:2px}
*{box-sizing:border-box}html,body{margin:0;padding:0;background:var(--bg);color:var(--text);font:16px/1.5 system-ui,Segoe UI,Roboto,Helvetica,Arial,"Apple Color Emoji","Segoe UI Emoji"}
.container{max-width:1100px;margin:0 auto;padding:20px}
.site-header{border-bottom:1px solid var(--border);background:linear-gradient(90deg,var(--pcb-start),var(--pcb-end));color:#fff}
.header-bar{display:flex;align-items:center;justify-content:space-between;gap:12px}
.site-title{font-size:20px;margin:0}
.site-header a{color:#fff;text-decoration:none}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(260px,1fr));gap:16px}
.card{background:var(--card);border:1px solid var(--border);border-radius:10px;display:flex;flex-direction:column;overflow:hidden}
.card-head{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:12px 14px;background:linear-gradient(90deg,var(--pcb-start),var(--pcb-end));color:#fff}
.card-title{margin:0;font-size:18px}
.card-num{display:flex;align-items:center}
.card-num svg{height:42px;width:auto;display:block}
.card-body{padding:16px;display:flex;flex-direction:column;flex:1}
.card-body .btn{margin-top:12px}
.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:12px}
.meta{display:flex;flex-wrap:wrap;gap:8px;margin:8px 0 0}
.pill{border:1px solid var(--border);border-radius:999px;padding:2px 8px;color:var(--muted);font-size:12px}
.btn{display:inline-block;background:var(--accent);color:#fff;text-decoration:none;padding:10px 14px;border-radius:8px;margin-top:12px}
.btn.secondary{background:transparent;border:1px solid var(--border);color:var(--text)}
.section{margin:24px 0}
.section h2{margin:0 0 8px 0}
.kbd{font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,monospace;background:color-mix(in srgb, var(--bg), #000 10%);border:1px solid var(--border);border-radius:6px;padding:2px 6px}
table{width:100%;border-collapse:collapse}
th,td{border-bottom:1px solid var(--border);padding:8px;text-align:left}
pre,code{background:color-mix(in srgb, var(--bg), #000 10%);border:1px solid var(--border);border-radius:6px;padding:2px 6px}
blockquote{border-left:4px solid var(--border);margin:0;padding:8px 12px;background:color-mix(in srgb, var(--bg), #000 8%)}
.docs-list li{margin:6px 0}
.downloads a{display:inline-block;margin-right:10px;margin-bottom:10px}
.site-footer{border-top:1px solid var(--border);background:color-mix(in srgb, var(--bg), #000 10%);margin-top:40px}

/* theme toggle */
.theme-toggle{appearance:none;-webkit-appearance:none;border:0;background:transparent;cursor:pointer}
.theme-toggle .track{position:relative;display:inline-flex;align-items:center;gap:6px;width:58px;height:28px;border-radius:999px;background:var(--toggle-track);border:1px solid var(--border);padding:2px}
.theme-toggle .thumb{position:absolute;top:2px;left:2px;width:24px;height:24px;border-radius:999px;background:var(--toggle-thumb);transition:transform .2s ease}
.theme-toggle .thumb{transform:translateX(var(--thumb-x,26px))}
.theme-toggle .icons{font-size:12px;line-height:1;width:100%;display:flex;justify-content:space-between;color:var(--muted)}
.theme-toggle .gap{width:6px;display:inline-block}
/* intro card */
.intro-card{background:#212830;color:#fff;margin-bottom:16px;margin-top:-20px;border-top-left-radius:0;border-top-right-radius:0;border-top:0}
.intro-card a{color:#fff}
/* light mode intro card background */
.theme-light .intro-card{background:#cfd6dd;color:#000;border-top-left-radius:0;border-top-right-radius:0;border-top:0}
.theme-light .intro-card a{color:#000}
/* dark mode: all cards use #212830 background */
.theme-dark .card{background:#212830}
/* meta list on cards */
.meta-list{list-style:none;padding:0;margin:8px 0 0}
.meta-list li{margin:6px 0;display:grid;grid-template-columns:140px 1fr;align-items:baseline;column-gap:12px;border-bottom:1px solid var(--border);padding-bottom:6px}
.meta-list{margin-top:auto}
.meta-list .label{color:var(--pcb-start);font-weight:700}
.meta-list .value{color:var(--text)}
.theme-dark .meta-list .value{color:#fff}
.theme-light .meta-list .value{color:#000}

/* status pill */
.status-pill{display:inline-block;padding:2px 10px;border-radius:999px;font-size:12px;border:1px solid var(--border);background:#dde3ea;color:#0b1220}
.status-pill.status-stable{background:#cdeccd}
.status-pill.status-beta{background:#ffe4b5}
.status-pill.status-experimental{background:#f7d6e6}
.status-pill.status-draft{background:#e6eef7}
.status-pill.status-deprecated{background:#facdd1}
.status-pill.status-wip{background:#fff3b0}
.status-pill.status-unknown{background:#dde3ea}
`;

function makeRawUrl(relPathFromRepoRoot) {
  return `https://raw.githubusercontent.com/${REPO}/${BRANCH}/${encodePathSegments(relPathFromRepoRoot)}`;
}

async function discoverRelease(folderName) {
  const abs = path.join(RELEASES_DIR, folderName);
  const slug = slugify(folderName);

  // Info
  const infoPath = path.join(abs, 'info.yaml');
  let info = { title: folderName };
  if (await fileExists(infoPath)) {
    const raw = await fs.readFile(infoPath, 'utf8');
    try {
      const parsed = YAML.parse(raw);
      info = normalizeInfo(parsed, folderName);
    } catch {
      info = normalizeInfo({}, folderName);
    }
  } else {
    info = normalizeInfo({}, folderName);
  }

  // README
  const readmePath = path.join(abs, 'README.md');
  let readmeHtml = '<p>No README.md found.</p>';
  if (await fileExists(readmePath)) {
    const md = await fs.readFile(readmePath, 'utf8');
    readmeHtml = marked.parse(md);
  }

  // Docs (PDF)
  const entries = await fs.readdir(abs, { withFileTypes: true });
  const docsDirName = entries.find(e => e.isDirectory() && /^(docs|documentation)$/i.test(e.name))?.name;
  const docs = [];
  if (docsDirName) {
    const fullDocsDir = path.join(abs, docsDirName);
    const files = await fs.readdir(fullDocsDir, { withFileTypes: true });
    for (const f of files) {
      if (f.isFile() && f.name.toLowerCase().endsWith('.pdf')) {
        const relFromRelease = path.join(docsDirName, f.name);
        const relFromRepoRoot = path.join('releases', folderName, relFromRelease);
        docs.push({ name: f.name, rel: toPosix(relFromRelease), url: makeRawUrl(relFromRepoRoot) });
      }
    }
  }

  // Downloads (UF2, zip, bin, hex)
  const downloads = [];
  async function collectDownloads(dir) {
    const ents = await fs.readdir(dir, { withFileTypes: true });
    for (const ent of ents) {
      const p = path.join(dir, ent.name);
      if (ent.isDirectory()) {
        await collectDownloads(p);
      } else if (/\.(uf2|zip|bin|hex)$/i.test(ent.name)) {
        const relFromRelease = path.relative(abs, p);
        const relFromRepoRoot = path.join('releases', folderName, relFromRelease);
        downloads.push({ name: ent.name, rel: toPosix(relFromRelease), url: makeRawUrl(relFromRepoRoot) });
      }
    }
  }
  await collectDownloads(abs);

  // Display fields
  const display = info.title && info.title !== folderName
    ? { title: info.title, number: (folderName.match(/^(\d+)/)?.[1] || '') }
    : parseDisplayFromFolder(folderName);

  return { slug, folderName, abs, info, readmeHtml, docs, downloads, display };
}

function releaseCard(rel) {
  const { info, slug, display } = rel;
  const desc = info.description ? String(info.description) : 'No description available.';
  const num = display.number;
  const creator = info.creator || 'unknown';
  const version = info.version || 'unknown';
  const language = info.language || 'unknown';
  const statusRaw = (info.status || 'unknown').toString();
  const statusClass = `status-${statusRaw.toLowerCase().replace(/[^a-z0-9]+/g,'-')}`;
  const metaItems = [
    `<li><span class=\"label\">Creator</span><span class=\"value\">${creator}</span></li>`,
    `<li><span class=\"label\">Version</span><span class=\"value\">${version}</span></li>`,
    `<li><span class=\"label\">Language</span><span class=\"value\">${language}</span></li>`,
    `<li><span class=\"label\">Status</span><span class=\"value\"><span class=\"status-pill ${statusClass}\">${statusRaw}</span></span></li>`,
  ].join('');
  const hasDownload = rel.downloads && rel.downloads.length > 0;
  const mainDownload = hasDownload ? rel.downloads[0] : null;
  return `<article class="card">
  <div class="card-head">
    <h3 class="card-title">${display.title}</h3>
    ${num ? `<span class="card-num" aria-label="Program ${num}">${sevenSegmentSvg(num)}</span>` : ''}
  </div>
  <div class="card-body">
    <p>${desc}</p>
    ${metaItems ? `<ul class="meta-list">${metaItems}</ul>` : ''}
    <a class="btn" href="programs/${slug}/index.html">View Details</a>
  </div>
</article>`;
}

function detailPage(rel) {
  const { info, downloads, docs, readmeHtml } = rel;
  const rows = [
    info.version && `<tr><th>Version</th><td>${info.version}</td></tr>`,
    info.status && `<tr><th>Status</th><td>${info.status}</td></tr>`,
    info.language && `<tr><th>Language</th><td>${info.language}</td></tr>`,
    info.creator && `<tr><th>Creator</th><td>${info.creator}</td></tr>`,
  ].filter(Boolean).join('\n');

  const downloadsHtml = downloads.length
    ? downloads.map(d => `<a class="btn secondary" href="${d.url}" download>${d.name}</a>`).join('\n')
    : '<p class="muted">No downloadable artifacts found.</p>';

  const docsListHtml = docs.length
    ? `<ul class="docs-list">${docs.map(d => `<li><a href="${d.url}" target="_blank" rel="noopener">${d.name}</a></li>`).join('\n')}</ul>`
    : '<p class="muted">No documentation PDFs found.</p>';

  const firstDocUrl = docs[0]?.url;
  const embedHtml = firstDocUrl ? `<div class="section"><details><summary>Preview first PDF</summary>
  <object data="${firstDocUrl}" type="application/pdf" width="100%" height="700px">
    <p>PDF preview not available. <a href="${firstDocUrl}" target="_blank" rel="noopener">Open PDF</a></p>
  </object>
</details></div>` : '';

  return renderLayout({
    title: `${info.title} – Workshop Computer`,
    relativeRoot: '../..',
    content: `
<div class="section">
  <h1>${info.title}</h1>
  ${info.description ? `<p>${info.description}</p>` : ''}
</div>

${rows ? `<div class=\"section\"><h2>Details</h2><table>${rows}</table></div>` : ''}

<div class="section downloads">
  <h2>Downloads</h2>
  ${downloadsHtml}
</div>

<div class="section">
  <h2>Documentation PDFs</h2>
  ${docsListHtml}
</div>
${embedHtml}

<div class="section">
  <h2>README</h2>
  <div class="readme">${readmeHtml}</div>
</div>
`
  });
}

async function build() {
  await ensureDir(OUT_DIR);
  await ensureDir(path.join(OUT_DIR, 'assets'));
  await writeFileEnsured(path.join(OUT_DIR, 'assets', 'style.css'), BASE_CSS);

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
    title: 'Workshop Computer Releases',
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
