import path from 'node:path';
import YAML from 'yaml';
import { marked } from 'marked';
import { fsAsync as fs, fileExists } from '../utils/fs.js';
import { slugify, parseDisplayFromFolder, formatDisplayTitle } from '../utils/strings.js';
import { discoverDocs } from './docs.js';
import { discoverDownloads } from './downloads.js';

export function normalizeInfo(raw, fallbackTitle) {
  const out = {};
  const mapKey = k => String(k || '').toLowerCase().replace(/\s+/g, '');
  for (const [k, v] of Object.entries(raw || {})) out[mapKey(k)] = v;
  return {
    title: out.title || out.name || fallbackTitle,
    description: out.description || '',
    language: out.language || '',
    creator: out.creator || '',
    version: out.version || '',
    status: out.status || '',
  };
}

export async function discoverRelease(rootReleasesDir, folderName, outDirPrograms, makeRawUrl) {
  const abs = path.join(rootReleasesDir, folderName);
  const slug = slugify(folderName);

  // info.yaml
  const infoPath = path.join(abs, 'info.yaml');
  let info = { title: folderName };
  if (await fileExists(infoPath)) {
    const raw = await fs.readFile(infoPath, 'utf8');
    try { info = normalizeInfo(YAML.parse(raw), folderName); }
    catch { info = normalizeInfo({}, folderName); }
  } else {
    info = normalizeInfo({}, folderName);
  }

  // Helper: rewrite relative links in README HTML to raw GitHub URLs
  function rewriteHtmlLinksToRaw(html, repoRelBase) {
    if (!html) return html;
    const posixBase = path.posix.join(...repoRelBase.split(path.sep));
    return html.replace(/\b(href|src)=(['"])([^'"#]+)(\#[^'"]*)?\2/gi, (full, attr, quote, url, hash = '') => {
      const u = String(url).trim();
      // Skip anchors, data URIs, protocol URLs, protocol-relative URLs
      if (!u || u.startsWith('#') || /^(?:[a-zA-Z][a-zA-Z0-9+.-]*:)?\/\//.test(u) || /^(?:[a-zA-Z][a-zA-Z0-9+.-]*:)/.test(u) || u.startsWith('data:')) {
        return full;
      }
      let relPath;
      if (u.startsWith('/')) {
        relPath = u.replace(/^\/+/, '');
      } else {
        relPath = path.posix.normalize(path.posix.join(posixBase, u));
      }
      const newUrl = makeRawUrl(relPath) + (hash || '');
      return `${attr}=${quote}${newUrl}${quote}`;
    });
  }

  // README.md
  const readmePath = path.join(abs, 'README.md');
  let readmeHtml = '<p>No README.md found.</p>';
  if (await fileExists(readmePath)) {
    const md = await fs.readFile(readmePath, 'utf8');
    readmeHtml = marked.parse(md);
  }

  // docs
  const outProgramDir = path.join(outDirPrograms, slug);
  const { docs } = await discoverDocs(abs, outProgramDir);

  // downloads and README asset link rewriting base
  const repoRelBase = path.join('releases', folderName);
  // Rewrite relative links in README HTML to raw GitHub URLs
  readmeHtml = rewriteHtmlLinksToRaw(readmeHtml, repoRelBase);
  
  // downloads
  const { downloads, latestUf2 } = await discoverDownloads(abs, repoRelBase, makeRawUrl);

  // display fields
  const parsed = parseDisplayFromFolder(folderName);
  const finalTitle = info.title ? formatDisplayTitle(info.title) : (parsed.title || folderName);
  const display = { number: parsed.number, title: finalTitle };

  return {
    folderName,
    slug,
    info: { ...info, title: finalTitle },
    readmeHtml,
    docs,
    downloads,
    latestUf2,
    display,
  };
}
