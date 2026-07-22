import path from 'node:path';
import YAML from 'yaml';
import { marked } from 'marked';
import { fsAsync as fs, ensureDir, encodePathSegments } from '../utils/fs.js';

export const CUSTOM_PANEL_WIDTH = 560;
export const CUSTOM_PANEL_HEIGHT = 1785;
const PANEL_ID = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;

function diagnostic(severity, pathName, message) {
  return { severity, path: pathName, message };
}

function safeRelativePath(value) {
  if (typeof value !== 'string' || !value.trim()) return null;
  const normalized = path.posix.normalize(value.trim().replace(/\\/g, '/'));
  if (normalized === '.' || normalized.startsWith('../') || normalized.includes('/../') || path.posix.isAbsolute(normalized)) return null;
  return normalized;
}

async function pngDimensions(file) {
  const handle = await fs.open(file, 'r');
  try {
    const header = Buffer.alloc(24);
    const { bytesRead } = await handle.read(header, 0, header.length, 0);
    const signature = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
    if (bytesRead < 24 || !header.subarray(0, 8).equals(signature) || header.toString('ascii', 12, 16) !== 'IHDR') return null;
    return { width: header.readUInt32BE(16), height: header.readUInt32BE(20) };
  } finally {
    await handle.close();
  }
}

async function regularFileInside(root, relative) {
  const file = path.join(root, ...relative.split('/'));
  try {
    const stat = await fs.lstat(file);
    if (!stat.isFile() || stat.isSymbolicLink()) return null;
    const [realRoot, realFile] = await Promise.all([fs.realpath(root), fs.realpath(file)]);
    if (realFile !== realRoot && !realFile.startsWith(realRoot + path.sep)) return null;
    return file;
  } catch {
    return null;
  }
}

function assetUrl(relative) {
  return `panels/${encodePathSegments(relative)}`;
}

function rewritePanelHtmlLinks(html, markdownPath) {
  const base = path.posix.dirname(markdownPath);
  return String(html || '').replace(/\b(href|src)=(['"])([^'"#]+)(#[^'"]*)?\2/gi, (full, attr, quote, rawUrl, hash = '') => {
    const value = String(rawUrl).trim();
    if (!value || /^(?:[a-z][a-z0-9+.-]*:)?\/\//i.test(value) || /^[a-z][a-z0-9+.-]*:/i.test(value) || value.startsWith('/')) return full;
    const relative = safeRelativePath(path.posix.join(base, value));
    return relative ? `${attr}=${quote}${assetUrl(relative)}${hash}${quote}` : full;
  });
}

/**
 * Discover an optional release-local panels/ presentation override.
 * Directory presence is authoritative: even an invalid manifest suppresses
 * generated Up/Middle/Down presentation views.
 */
export async function discoverCustomPanels(absReleaseDir, outProgramDir) {
  const panelsDir = path.join(absReleaseDir, 'panels');
  let stat;
  try {
    stat = await fs.lstat(panelsDir);
  } catch {
    return { present: false, panels: null, diagnostics: [] };
  }

  const diagnostics = [];
  const empty = { source: 'custom', default: '', items: [] };
  if (!stat.isDirectory() || stat.isSymbolicLink()) {
    diagnostics.push(diagnostic('error', 'panels', 'panels must be a real directory, not a file or symbolic link.'));
    return { present: true, panels: empty, diagnostics };
  }

  const manifestPath = path.join(panelsDir, 'manifest.yaml');
  let manifest;
  try {
    manifest = YAML.parse(await fs.readFile(manifestPath, 'utf8')) || {};
  } catch (error) {
    diagnostics.push(diagnostic('error', 'panels/manifest.yaml', `Could not read the custom-panel manifest: ${error.message}`));
    return { present: true, panels: empty, diagnostics };
  }

  if (!manifest || typeof manifest !== 'object' || Array.isArray(manifest)) {
    diagnostics.push(diagnostic('error', 'panels/manifest.yaml', 'The manifest root must be a mapping.'));
    return { present: true, panels: empty, diagnostics };
  }
  if (manifest.version !== 1) diagnostics.push(diagnostic('error', 'panels/manifest.yaml', 'version must be 1.'));
  if (!Array.isArray(manifest.panels) || !manifest.panels.length) {
    diagnostics.push(diagnostic('error', 'panels/manifest.yaml', 'panels must be a non-empty list.'));
    return { present: true, panels: empty, diagnostics };
  }

  const ids = new Set();
  const items = [];
  for (let index = 0; index < manifest.panels.length; index += 1) {
    const raw = manifest.panels[index];
    const at = `panels/manifest.yaml panels[${index}]`;
    if (!raw || typeof raw !== 'object' || Array.isArray(raw)) {
      diagnostics.push(diagnostic('error', at, 'Panel entry must be a mapping.'));
      continue;
    }
    const id = typeof raw.id === 'string' ? raw.id.trim() : '';
    const name = typeof raw.name === 'string' ? raw.name.trim() : '';
    const imagePath = safeRelativePath(raw.image);
    const contentPath = safeRelativePath(raw.content);
    let valid = true;
    if (!PANEL_ID.test(id)) { diagnostics.push(diagnostic('error', `${at}.id`, 'id must be unique lowercase kebab-case.')); valid = false; }
    if (ids.has(id.toLowerCase())) { diagnostics.push(diagnostic('error', `${at}.id`, `Duplicate panel id "${id}".`)); valid = false; }
    if (!name) { diagnostics.push(diagnostic('error', `${at}.name`, 'name must be non-empty text.')); valid = false; }
    if (!imagePath) { diagnostics.push(diagnostic('error', `${at}.image`, 'image must be a safe relative PNG path.')); valid = false; }
    if (!contentPath) { diagnostics.push(diagnostic('error', `${at}.content`, 'content must be a safe relative Markdown path.')); valid = false; }
    if (imagePath && path.posix.extname(imagePath).toLowerCase() !== '.png') { diagnostics.push(diagnostic('error', `${at}.image`, 'image must be a PNG file.')); valid = false; }
    if (contentPath && path.posix.extname(contentPath).toLowerCase() !== '.md') { diagnostics.push(diagnostic('error', `${at}.content`, 'content must be a Markdown file.')); valid = false; }
    if (id) ids.add(id.toLowerCase());
    if (!valid) continue;

    const imageFile = await regularFileInside(panelsDir, imagePath);
    const contentFile = await regularFileInside(panelsDir, contentPath);
    if (!imageFile) { diagnostics.push(diagnostic('error', `${at}.image`, `Missing or unsafe file "${imagePath}".`)); valid = false; }
    if (!contentFile) { diagnostics.push(diagnostic('error', `${at}.content`, `Missing or unsafe file "${contentPath}".`)); valid = false; }
    if (!valid) continue;

    const dimensions = await pngDimensions(imageFile);
    if (!dimensions) {
      diagnostics.push(diagnostic('error', `${at}.image`, `"${imagePath}" is not a valid PNG.`));
      continue;
    }
    if (dimensions.width !== CUSTOM_PANEL_WIDTH || dimensions.height !== CUSTOM_PANEL_HEIGHT) {
      diagnostics.push(diagnostic('error', `${at}.image`, `"${imagePath}" must be ${CUSTOM_PANEL_WIDTH} × ${CUSTOM_PANEL_HEIGHT} px; found ${dimensions.width} × ${dimensions.height}.`));
      continue;
    }
    const markdown = await fs.readFile(contentFile, 'utf8');
    if (!markdown.trim()) diagnostics.push(diagnostic('warning', `${at}.content`, `"${contentPath}" is empty.`));
    items.push({
      id,
      name,
      kind: 'custom',
      image: { url: assetUrl(imagePath), width: dimensions.width, height: dimensions.height },
      content_html: rewritePanelHtmlLinks(marked.parse(markdown), contentPath),
      source: { image: `panels/${imagePath}`, content: `panels/${contentPath}` },
    });
  }

  const requestedDefault = typeof manifest.default === 'string' ? manifest.default.trim() : '';
  const defaultItem = items.find(item => item.id === requestedDefault);
  if (!requestedDefault) diagnostics.push(diagnostic('error', 'panels/manifest.yaml default', 'default must name one panel id.'));
  else if (!defaultItem) diagnostics.push(diagnostic('error', 'panels/manifest.yaml default', `default references missing or invalid panel "${requestedDefault}".`));

  // Preserve the complete authored directory so Markdown can use supplementary
  // relative images. Referenced primary files were checked above; symlinks are
  // rejected by fs.cp rather than followed.
  try {
    await ensureDir(outProgramDir);
    await fs.cp(panelsDir, path.join(outProgramDir, 'panels'), {
      recursive: true,
      force: true,
      filter: async source => !(await fs.lstat(source)).isSymbolicLink(),
    });
  } catch (error) {
    diagnostics.push(diagnostic('error', 'panels', `Could not copy custom-panel assets: ${error.message}`));
  }

  return {
    present: true,
    panels: { source: 'custom', default: defaultItem?.id || items[0]?.id || '', items },
    diagnostics,
  };
}

function objectField(value, wanted) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return undefined;
  const normalized = wanted.toLowerCase().replace(/[-_\s]/g, '');
  const entry = Object.entries(value).find(([key]) => key.toLowerCase().replace(/[-_\s]/g, '') === normalized);
  return entry?.[1];
}

/** Validate cross-file when.panel references after info.yaml and the manifest are available. */
export function validateCustomPanelReferences(info, customPanels) {
  const diagnostics = [];
  const ids = new Set((customPanels?.items || []).map(item => item.id));
  const controls = objectField(info, 'controls');
  const panel = objectField(info, 'panel');
  const lists = [
    ['controls.knobs', objectField(controls, 'knobs')],
    ['controls.leds', objectField(controls, 'leds')],
    ['panel.inputs', objectField(panel, 'inputs')],
    ['panel.outputs', objectField(panel, 'outputs')],
  ];
  for (const [listPath, rows] of lists) {
    if (!Array.isArray(rows)) continue;
    rows.forEach((row, index) => {
      const when = objectField(row, 'when');
      const panelId = objectField(when, 'panel');
      if (panelId === undefined) return;
      if (!customPanels) {
        diagnostics.push(diagnostic('error', `${listPath}[${index}].when.panel`, 'when.panel requires a panels/manifest.yaml custom-panel override.'));
      } else if (typeof panelId === 'string' && !ids.has(panelId)) {
        diagnostics.push(diagnostic('error', `${listPath}[${index}].when.panel`, `Unknown custom panel id "${panelId}".`));
      }
    });
  }
  return diagnostics;
}
