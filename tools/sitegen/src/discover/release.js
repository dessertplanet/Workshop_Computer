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

  // Helper: convert YouTube links to embedded iframes
  function youtubeStartSeconds(u) {
    // parse t=1h2m3s or t=75s or t=75; also support start=
    const parseTs = (val) => {
      if (!val) return 0;
      // 1h2m3s, 2m10s, 75s, 75
      const m = String(val).match(/(?:(\d+)h)?(?:(\d+)m)?(?:(\d+)s?)?$/i);
      if (m) {
        const h = parseInt(m[1]||'0',10), mnt = parseInt(m[2]||'0',10), s = parseInt(m[3]||'0',10);
        if (h||mnt||s) return h*3600+mnt*60+s;
      }
      const n = parseInt(val,10);
      return isNaN(n) ? 0 : Math.max(0,n);
    };
    try {
      const url = new URL(u);
      const sp = url.searchParams;
      let t = sp.get('t') || sp.get('start') || '';
      if (!t && url.hash && url.hash.includes('t=')) {
        const hm = url.hash.match(/t=([^&#]+)/);
        if (hm) t = hm[1];
      }
      return parseTs(t);
    } catch { return 0; }
  }

  function getYouTubeEmbedUrl(u) {
    try {
      const url = new URL(u);
      const host = url.hostname.replace(/^www\./,'');
      let id = null;
      if (host === 'youtu.be') {
        id = url.pathname.split('/').filter(Boolean)[0] || null;
      } else if (host.endsWith('youtube.com') || host.endsWith('youtube-nocookie.com')) {
        if (url.pathname === '/watch') {
          id = url.searchParams.get('v');
        } else if (url.pathname.startsWith('/shorts/')) {
          id = url.pathname.split('/').filter(Boolean)[1] || null;
        }
      }
      if (!id) return null;
      const start = youtubeStartSeconds(u);
      const base = `https://www.youtube-nocookie.com/embed/${id}`;
      const qs = start > 0 ? `?start=${start}&rel=0` : `?rel=0`;
      return base + qs;
    } catch { return null; }
  }

  function embedYouTube(html) {
    if (!html) return html;
    // Replace <p><a href="youtube...">...</a></p> first to avoid invalid <div> inside <p>
    html = html.replace(/<p>\s*(<a\s+[^>]*href=(['"])([^'"#]+)\2[^>]*>[^<]*<\/a>)\s*<\/p>/gi, (full, aTag, q, href) => {
      const embed = getYouTubeEmbedUrl(href);
      if (!embed) return full;
      return `<div class="video-embed"><iframe src="${embed}" allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share" allowfullscreen title="YouTube video"></iframe></div>`;
    });
    // Replace any remaining <a href="youtube...">...</a>
    html = html.replace(/<a\s+[^>]*href=(['"])([^'"#]+)\1[^>]*>[^<]*<\/a>/gi, (full, q, href) => {
      const embed = getYouTubeEmbedUrl(href);
      if (!embed) return full;
      return `<div class="video-embed"><iframe src="${embed}" allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share" allowfullscreen title="YouTube video"></iframe></div>`;
    });
    // Replace <a href="youtube..."><img ...></a>
    html = html.replace(/<a\s+[^>]*href=(['"])([^'"#]+)\1[^>]*>\s*<img\b[^>]*>\s*<\/a>/gi, (full, hrefQ, href) => {
      const embed = getYouTubeEmbedUrl(href);
      if (!embed) return full;
      return `<div class="video-embed"><iframe src="${embed}" allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share" allowfullscreen title="YouTube video"></iframe></div>`;
    });
    return html;
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
  // Convert YouTube links to embeds
  readmeHtml = embedYouTube(readmeHtml);
  
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
