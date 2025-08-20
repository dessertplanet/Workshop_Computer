import { promises as fs } from 'node:fs';
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
const DEFAULT_REPO = 'jr0dsgarage/Workshop_Computer';
const REPO = process.env.GITHUB_REPOSITORY || DEFAULT_REPO;
const DEFAULT_BRANCH = 'main';
const BRANCH = process.env.GITHUB_REF_NAME || DEFAULT_BRANCH;

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

function renderLayout({ title, content, relativeRoot = '.' }) {
  return `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>${title ? String(title).replace(/</g, '&lt;') : 'Workshop Computer'}</title>
  <link rel="stylesheet" href="${relativeRoot}/assets/style.css" />
</head>
<body>
  <header class="site-header">
    <div class="container">
      <h1><a href="${relativeRoot}/index.html">Workshop Computer Releases</a></h1>
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
</body>
</html>`;
}

const BASE_CSS = `:root{--bg:#0b0d10;--card:#11151a;--muted:#9aa6b2;--text:#e6edf3;--accent:#2f81f7;--border:#27313b;--ok:#3fb950}
*{box-sizing:border-box}html,body{margin:0;padding:0;background:var(--bg);color:var(--text);font:16px/1.5 system-ui,Segoe UI,Roboto,Helvetica,Arial,"Apple Color Emoji","Segoe UI Emoji"}
.container{max-width:1100px;margin:0 auto;padding:20px}
.site-header{border-bottom:1px solid var(--border);background:#0e1217}
.site-header h1{font-size:20px;margin:0}
.site-header a{color:var(--text);text-decoration:none}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(260px,1fr));gap:16px}
.card{background:var(--card);border:1px solid var(--border);border-radius:10px;padding:16px;display:flex;flex-direction:column}
.card h3{margin:0 0 8px 0;font-size:18px}
.meta{display:flex;flex-wrap:wrap;gap:8px;margin:8px 0 0}
.pill{border:1px solid var(--border);border-radius:999px;padding:2px 8px;color:var(--muted);font-size:12px}
.btn{display:inline-block;background:var(--accent);color:#fff;text-decoration:none;padding:10px 14px;border-radius:8px;margin-top:12px}
.btn.secondary{background:transparent;border:1px solid var(--border);color:var(--text)}
.section{margin:24px 0}
.section h2{margin:0 0 8px 0}
.kbd{font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,monospace;background:#0a0d12;border:1px solid var(--border);border-radius:6px;padding:2px 6px}
table{width:100%;border-collapse:collapse}
th,td{border-bottom:1px solid var(--border);padding:8px;text-align:left}
pre,code{background:#0a0d12;border:1px solid var(--border);border-radius:6px;padding:2px 6px}
blockquote{border-left:4px solid var(--border);margin:0;padding:8px 12px;background:#0f1318}
.docs-list li{margin:6px 0}
.downloads a{display:inline-block;margin-right:10px;margin-bottom:10px}
.site-footer{border-top:1px solid var(--border);background:#0e1217;margin-top:40px}
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

  return { slug, folderName, abs, info, readmeHtml, docs, downloads };
}

function releaseCard(rel) {
  const { info, slug } = rel;
  const desc = info.description ? String(info.description) : '';
  return `<article class="card">
  <h3>${info.title}</h3>
  <p>${desc}</p>
  <div class="meta">
    ${info.version ? `<span class="pill">v${info.version}</span>` : ''}
    ${info.status ? `<span class="pill">${info.status}</span>` : ''}
    ${info.language ? `<span class="pill">${info.language}</span>` : ''}
    ${info.creator ? `<span class="pill">by ${info.creator}</span>` : ''}
  </div>
  <a class="btn" href="programs/${slug}/index.html">View Details</a>
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
<div class="section">
  <p>Auto-generated listing of programs found in the <span class="kbd">/releases</span> folder.</p>
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
    content: '<h1>404</h1><p>Page not found.</p>'
  }));

  console.log(`Built site with ${releases.length} releases -> ${OUT_DIR}`);
}

build().catch(err => {
  console.error(err);
  process.exit(1);
});
