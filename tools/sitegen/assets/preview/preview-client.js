// Author preview/editor client.
//
// Enforces the authoring boundary from documentation/site-migration.md:
//   - the editable source is the raw info.yaml (author schema), never the
//     normalized card model;
//   - edits are validated + previewed live from that source;
//   - there is no "Normalize" button — normalization happens implicitly to
//     render the preview and is never presented as the editable form.
//
// It reuses the exact same shared modules the site build uses (copied to
// ./lib/ at build time), so validation and rendering here match production.

import { parseSource } from './lib/validate/parseSource.js';
import { validateInfoYaml } from './lib/validate/validateInfoYaml.js';
import { buildCanonicalCardModel } from './lib/model/card.js';
import { renderCardArticle } from './lib/render/cardPage.js';
import { resolveAudioSamples, getAudioField } from './lib/utils/audio.js';

const els = {
  select: document.getElementById('card-select'),
  editor: document.getElementById('yaml-source'),
  status: document.getElementById('editor-status'),
  diagnostics: document.getElementById('diagnostics'),
  preview: document.getElementById('card-preview'),
  download: document.getElementById('download-source'),
  tool: document.querySelector('.author-tool'),
  gutter: document.getElementById('gutter-inner'),
};

// Reveal the editor once the index + first source have loaded, hiding the
// initial loading spinner.
function clearLoading() {
  if (els.tool) els.tool.classList.remove('is-loading');
}

// Keep the line-number gutter in sync with the editor content and scroll.
function updateGutter() {
  if (!els.gutter) return;
  const lines = els.editor.value.split('\n').length;
  let s = '';
  for (let i = 1; i <= lines; i++) s += (i > 1 ? '\n' : '') + i;
  els.gutter.textContent = s;
  syncGutter();
}

function syncGutter() {
  if (els.gutter) els.gutter.style.transform = `translateY(${-els.editor.scrollTop}px)`;
}

let index = [];
let current = null; // the selected raw-info index entry

// Read a string property by a case-insensitive key, trimmed.
function readCi(obj, name) {
  if (!obj || typeof obj !== 'object') return '';
  const t = String(name).toLowerCase();
  for (const [k, v] of Object.entries(obj)) {
    if (k.toLowerCase() === t && typeof v === 'string') return v.trim();
  }
  return '';
}

// A sensible label for an external URL: its filename, else host, else the URL.
function nameFromUrl(u) {
  try {
    const parsed = new URL(u);
    return parsed.pathname.split('/').filter(Boolean).pop() || parsed.hostname || u;
  } catch {
    return u;
  }
}

// The bare host of a URL (without a leading www.), for display on the tile.
function hostFromUrl(u) {
  try {
    return new URL(u).hostname.replace(/^www\./i, '');
  } catch {
    return '';
  }
}

function debounce(fn, ms) {
  let t;
  return (...args) => { clearTimeout(t); t = setTimeout(() => fn(...args), ms); };
}

// On Enter, carry over the current line's leading whitespace so YAML nesting is
// preserved. Tab is deliberately left alone to avoid a keyboard trap (WCAG
// 2.1.2). Uses execCommand where available to keep native undo history.
function autoIndentOnEnter(e) {
  if (e.key !== 'Enter' || e.shiftKey || e.ctrlKey || e.metaKey || e.altKey) return;
  const ta = els.editor;
  const start = ta.selectionStart;
  const lineStart = ta.value.lastIndexOf('\n', start - 1) + 1;
  const indent = (ta.value.slice(lineStart, start).match(/^[ \t]*/) || [''])[0];
  if (!indent) return; // no indentation to carry; let the default newline happen
  e.preventDefault();
  const insertion = '\n' + indent;
  if (!(document.execCommand && document.execCommand('insertText', false, insertion))) {
    ta.setRangeText(insertion, ta.selectionStart, ta.selectionEnd, 'end');
    ta.dispatchEvent(new Event('input', { bubbles: true }));
  }
}

// --- uf2 path typeahead ---------------------------------------------------
// While the caret is in a `path:` value, suggest the card's known tracked .uf2
// paths (shipped in the index as uf2Files) and let the author pick one.
const suggest = { el: null, items: [], index: -1, valueStart: 0, valueEnd: 0 };

function ensureSuggestEl() {
  if (suggest.el) return suggest.el;
  const el = document.createElement('ul');
  el.id = 'uf2-suggest';
  el.setAttribute('role', 'listbox');
  el.hidden = true;
  el.addEventListener('mousedown', (e) => {
    const li = e.target.closest('li[data-i]');
    if (!li) return;
    e.preventDefault(); // keep textarea focus
    acceptSuggestion(Number(li.dataset.i));
  });
  document.body.appendChild(el);
  suggest.el = el;
  return el;
}

function hideSuggest() {
  suggest.items = [];
  suggest.index = -1;
  if (suggest.el) suggest.el.hidden = true;
}

function suggestOpen() {
  return suggest.el && !suggest.el.hidden && suggest.items.length > 0;
}

// If the caret sits in a `path:` value, return its value span + typed text.
function pathContext() {
  const ta = els.editor;
  const caret = ta.selectionStart;
  if (caret !== ta.selectionEnd) return null;
  const value = ta.value;
  const lineStart = value.lastIndexOf('\n', caret - 1) + 1;
  let lineEnd = value.indexOf('\n', caret);
  if (lineEnd === -1) lineEnd = value.length;
  const line = value.slice(lineStart, lineEnd);
  const m = line.match(/^(\s*(?:-\s*)?path\s*:\s*)(.*)$/i);
  if (!m) return null;
  const valueStart = lineStart + m[1].length;
  if (caret < valueStart) return null;
  const typed = value.slice(valueStart, caret).replace(/^['"]/, '');
  return { valueStart, valueEnd: lineEnd, typed };
}

function updateSuggest() {
  const files = current && Array.isArray(current.uf2Files) ? current.uf2Files : [];
  if (!files.length) return hideSuggest();
  const ctx = pathContext();
  if (!ctx) return hideSuggest();
  const q = ctx.typed.trim().toLowerCase();
  const matches = files
    .filter(f => f.toLowerCase().includes(q))
    .sort((a, b) => a.toLowerCase().indexOf(q) - b.toLowerCase().indexOf(q) || a.localeCompare(b))
    .slice(0, 8);
  if (!matches.length || (matches.length === 1 && matches[0].toLowerCase() === q)) return hideSuggest();
  suggest.items = matches;
  suggest.index = 0;
  suggest.valueStart = ctx.valueStart;
  suggest.valueEnd = ctx.valueEnd;
  renderSuggest();
}

function renderSuggest() {
  const el = ensureSuggestEl();
  el.innerHTML = suggest.items
    .map((f, i) => `<li role="option" data-i="${i}" class="${i === suggest.index ? 'active' : ''}">${esc(f)}</li>`)
    .join('');
  positionSuggest();
  el.hidden = false;
}

function positionSuggest() {
  const c = caretCoordinates(els.editor, suggest.valueStart);
  const r = els.editor.getBoundingClientRect();
  const el = suggest.el;
  el.style.left = `${window.scrollX + r.left + c.left}px`;
  el.style.top = `${window.scrollY + r.top + c.top + c.height - els.editor.scrollTop}px`;
}

function acceptSuggestion(i) {
  const file = suggest.items[i];
  if (!file) return;
  const ta = els.editor;
  ta.focus();
  ta.setSelectionRange(suggest.valueStart, suggest.valueEnd);
  if (!(document.execCommand && document.execCommand('insertText', false, file))) {
    ta.setRangeText(file, suggest.valueStart, suggest.valueEnd, 'end');
    ta.dispatchEvent(new Event('input', { bubbles: true }));
  }
  const pos = suggest.valueStart + file.length;
  ta.setSelectionRange(pos, pos);
  hideSuggest();
  run();
}

// Pixel position of a character offset within a textarea (mirror-div method).
function caretCoordinates(ta, position) {
  const div = document.createElement('div');
  const s = getComputedStyle(ta);
  const props = ['boxSizing', 'width', 'paddingTop', 'paddingRight', 'paddingBottom', 'paddingLeft',
    'borderTopWidth', 'borderRightWidth', 'borderBottomWidth', 'borderLeftWidth', 'fontStyle',
    'fontVariant', 'fontWeight', 'fontStretch', 'fontSize', 'fontFamily', 'lineHeight',
    'letterSpacing', 'textTransform', 'wordSpacing', 'tabSize'];
  for (const p of props) div.style[p] = s[p];
  div.style.position = 'absolute';
  div.style.visibility = 'hidden';
  div.style.whiteSpace = 'pre-wrap';
  div.style.wordWrap = 'break-word';
  div.style.overflow = 'hidden';
  div.textContent = ta.value.slice(0, position);
  const span = document.createElement('span');
  span.textContent = ta.value.slice(position) || '.';
  div.appendChild(span);
  document.body.appendChild(div);
  const top = span.offsetTop + parseInt(s.borderTopWidth || '0', 10);
  const left = span.offsetLeft + parseInt(s.borderLeftWidth || '0', 10);
  const height = parseInt(s.lineHeight, 10) || parseInt(s.fontSize, 10);
  document.body.removeChild(div);
  return { top, left, height };
}

// Combined keydown: drive the suggestion list when open, else auto-indent.
function onEditorKeydown(e) {
  if (suggestOpen()) {
    if (e.key === 'ArrowDown') { e.preventDefault(); suggest.index = (suggest.index + 1) % suggest.items.length; renderSuggest(); return; }
    if (e.key === 'ArrowUp') { e.preventDefault(); suggest.index = (suggest.index - 1 + suggest.items.length) % suggest.items.length; renderSuggest(); return; }
    if (e.key === 'Enter' || e.key === 'Tab') { e.preventDefault(); acceptSuggestion(suggest.index); return; }
    if (e.key === 'Escape') { e.preventDefault(); hideSuggest(); return; }
  }
  autoIndentOnEnter(e);
}

// Read the top-level `uf2` field (case-insensitive) from parsed YAML.
function readUf2Field(data) {
  if (!data || typeof data !== 'object') return undefined;
  for (const k of Object.keys(data)) if (k.toLowerCase() === 'uf2') return data[k];
  return undefined;
}

// Derive the raw-content base URL (…/releases/<id>/) from a known build URL so
// the preview can build plausible links for author-edited uf2 paths.
function rawBaseForCurrent() {
  if (!current) return '';
  const sample = current.uf2Url
    || (Array.isArray(current.uf2Downloads) && current.uf2Downloads[0] && current.uf2Downloads[0].url)
    || '';
  const marker = `/releases/${current.id}/`;
  const i = sample.indexOf(marker);
  return i >= 0 ? sample.slice(0, i + marker.length) : '';
}

// Mirror the build's curation from the *edited* source so the preview updates
// live: an author `uf2:` list replaces auto-discovery. The browser can't verify
// paths on disk, so this is best-effort (links are approximate). Returns null
// when there's no usable `uf2:` (so we fall back to the build-discovered list).
function curatedUf2FromSource(data) {
  const field = readUf2Field(data);
  if (field == null) return null;
  const arr = Array.isArray(field) ? field : [field];
  if (!arr.length) return null;
  const base = rawBaseForCurrent();
  const out = [];
  for (const e of arr) {
    if (!e || typeof e !== 'object' || Array.isArray(e)) continue;
    const dl = e.download && typeof e.download === 'object' && !Array.isArray(e.download) ? e.download : null;
    const ext = dl ? readCi(dl, 'url') : '';
    const p = typeof e.path === 'string' ? e.path.trim() : '';
    let item;
    if (ext) {
      // External link (store/mirror): no repo path needed.
      item = { name: (typeof e.name === 'string' && e.name.trim()) || nameFromUrl(ext), url: ext, host: hostFromUrl(ext), external: true };
      const sha = dl ? readCi(dl, 'sha256') : '';
      if (sha) item.sha256 = sha;
    } else if (p) {
      const url = base ? base + p.split('/').filter(Boolean).map(encodeURIComponent).join('/') : '';
      item = { name: (typeof e.name === 'string' && e.name.trim()) || p.split('/').pop(), url };
      const sha = dl ? readCi(dl, 'sha256') : '';
      if (sha) item.sha256 = sha;
    } else {
      continue; // neither path nor url — validation flags this
    }
    out.push(item);
  }
  return out.length ? out : null;
}

function esc(s) {
  return String(s == null ? '' : s)
    .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

// Read the deeplinked card id from the URL hash. Supports both "#<id>" and
// "#card=<id>" forms so links stay readable.
function idFromHash() {
  const h = decodeURIComponent(String(location.hash || '').replace(/^#/, '')).trim();
  if (!h) return '';
  const m = h.match(/^card=(.*)$/);
  return (m ? m[1] : h).trim();
}

function findEntryIndexById(id) {
  if (!id) return -1;
  return index.findIndex((e) => String(e.id) === id);
}

// Load the card named by the URL hash (falling back to the first card), and
// keep the dropdown in sync. This is the single place selection is applied, so
// pasted links, back/forward navigation, and dropdown changes all flow through
// the hash.
function applyHash() {
  if (!index.length) return;
  const id = idFromHash();
  const i = id ? findEntryIndexById(id) : -1;
  const entry = i >= 0 ? index[i] : index[0];
  // Normalize the URL so a bare /preview (or an unknown hash) reflects the card
  // that actually loads, e.g. /preview/#00_Simple_MIDI. replaceState avoids a
  // spurious history entry and does not re-trigger hashchange.
  const canonical = `#${encodeURIComponent(String(entry.id))}`;
  if (location.hash !== canonical) {
    history.replaceState(null, '', canonical);
  }
  els.select.value = String(index.indexOf(entry));
  loadCard(entry);
}

function renderDiagnostics(result) {
  const { diagnostics, errorCount, warningCount } = result;
  if (!diagnostics.length) {
    els.diagnostics.innerHTML =
      '<p class="diag-clean">✓ Conforms to documentation/info.yaml.md — no issues.</p>';
    return;
  }
  const summary = `<p class="diag-summary">${errorCount} error(s), ${warningCount} warning(s)</p>`;
  const rows = diagnostics.map((d) => {
    const where = d.path ? `<code>${esc(d.path)}</code>` : '';
    const line = d.line != null ? `<span class="diag-line">line ${d.line}</span>` : '';
    return `<li class="diag diag--${esc(d.severity)}">
      <span class="diag-sev">${d.severity === 'error' ? 'ERROR' : 'WARN'}</span>
      <span class="diag-body">${where} ${esc(d.message)} ${line}</span>
      <span class="diag-rule">${esc(d.ruleId)}</span>
    </li>`;
  }).join('');
  els.diagnostics.innerHTML = summary + `<ul class="diag-list">${rows}</ul>`;
}

function renderPreview(source) {
  // Re-rendering replaces #card-preview wholesale, which would reset the
  // preview column's scroll on every keystroke. Capture and restore it so the
  // reader stays where they were while editing.
  const scroller = els.preview.closest('.author-col');
  const prevScroll = scroller ? scroller.scrollTop : 0;
  let html;
  if (source.error || !source.data) {
    html = '<p class="preview-note">Fix the YAML syntax error above to preview the card.</p>';
  } else {
    try {
      // Normalize implicitly (behind the scenes) purely to render the preview.
      // Firmware/source/readme links come from the build-discovered fields in the
      // raw-info index (the browser can't walk the release folder), so the preview
      // shows the same download/source/readme links a full build would produce.
      // Remaining ancillary inputs (docs, git dates) are still absent here.
      const uf2Url = current ? (current.uf2Url || '') : '';
      const uf2Downloads = current && Array.isArray(current.uf2Downloads) ? current.uf2Downloads : [];
      const editedUf2 = curatedUf2FromSource(source.data);
      const effectiveUf2Downloads = editedUf2 !== null ? editedUf2 : uf2Downloads;
      const effectiveUf2Url = (editedUf2 && editedUf2[0] && editedUf2[0].url) || uf2Url;
      const base = rawBaseForCurrent();
      const audioSamples = resolveAudioSamples(
        getAudioField(source.data),
        (rel) => (base ? base + rel.split('/').filter(Boolean).map(encodeURIComponent).join('/') : ''),
      );
      const card = buildCanonicalCardModel({
        folderName: current ? current.id : 'preview',
        slug: current ? current.slug : 'preview',
        info: {},
        rawYaml: source.data,
        docs: [],
        downloads: [],
        latestUf2: effectiveUf2Url ? { url: effectiveUf2Url } : null,
        uf2Downloads: effectiveUf2Downloads,
        web: {},
        audioSamples,
        readmePath: '',
        sourceFile: current ? current.sourceFile : 'info.yaml',
        sourceUrl: current ? (current.sourceUrl || '') : '',
        readmeUrl: current ? (current.readmeUrl || '') : '',
        gitFirstDate: '',
        gitLastDate: '',
      });
      html = renderCardArticle({
        card,
        panelImg: '../assets/program_cards/Standalone_computer_rev1.svg',
        yamlUrl: current ? (current.yamlUrl || '') : '',
        uf2Url: effectiveUf2Url,
        extraDocs: '',
      });
    } catch (err) {
      html = `<p class="preview-note">Preview could not render: ${esc(err.message)}</p>`;
    }
  }
  els.preview.innerHTML = html;
  if (scroller) scroller.scrollTop = prevScroll;
}

function run() {
  const raw = els.editor.value;
  const source = parseSource(raw, current ? current.sourceFile : 'info.yaml');
  const result = validateInfoYaml(source);
  // Supplement the shared validator with a preview-only check that each curated
  // uf2 path actually exists in the repo, using the tracked-file list the build
  // shipped in the index (the browser can't run git itself).
  const extra = current && Array.isArray(current.uf2Files)
    ? uf2PathDiagnostics(source, current.uf2Files) : [];
  const diagnostics = extra.length ? result.diagnostics.concat(extra) : result.diagnostics;
  const errorCount = diagnostics.filter(d => d.severity === 'error').length;
  const warningCount = diagnostics.filter(d => d.severity === 'warning').length;
  const merged = { ...result, diagnostics, errorCount, warningCount };
  renderDiagnostics(merged);
  renderPreview(source);
  els.status.textContent = source.error
    ? 'YAML syntax error'
    : `${errorCount} error(s), ${warningCount} warning(s)`;
}

// Flag curated uf2 paths that don't match any tracked .uf2 in the repo. Paths
// are compared case-insensitively (mirroring the build's resolution).
function uf2PathDiagnostics(source, files) {
  if (source.error || !source.data) return [];
  const field = readUf2Field(source.data);
  if (field == null) return [];
  const arr = Array.isArray(field) ? field : [field];
  const known = new Set(files.map(f => String(f).toLowerCase()));
  const out = [];
  arr.forEach((e, i) => {
    if (!e || typeof e !== 'object' || Array.isArray(e)) return;
    const p = typeof e.path === 'string' ? e.path.trim() : '';
    if (!p) return; // missing path handled by the shared rule
    const norm = p.replace(/^\.?\/*/, '').toLowerCase();
    if (!known.has(norm)) {
      out.push({ severity: 'error', ruleId: 'uf2-path', path: `uf2[${i}].path`,
        message: `uf2[${i}] path not found in the repo: ${p}` });
    }
  });
  return out;
}

// Save the current editor contents as an info.yaml file. This lets an author
// download their edited source (the source of truth) to commit into the repo.
function downloadSource() {
  const blob = new Blob([els.editor.value], { type: 'text/yaml' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = 'info.yaml';
  document.body.appendChild(a);
  a.click();
  a.remove();
  URL.revokeObjectURL(url);
}

async function loadCard(entry) {
  current = entry;
  els.status.textContent = 'Loading…';
  try {
    const raw = await fetch(`../${entry.path}`, { cache: 'no-store' }).then((r) => {
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      return r.text();
    });
    els.editor.value = raw;
    run();
    updateGutter();
  } catch (err) {
    els.editor.value = '';
    els.diagnostics.innerHTML = `<p class="diag--error">Could not load ${esc(entry.path)}: ${esc(err.message)}</p>`;
    els.preview.innerHTML = '';
    els.status.textContent = 'Load failed';
  } finally {
    clearLoading();
  }
}

async function init() {
  try {
    index = await fetch('../raw-info/index.json', { cache: 'no-store' }).then((r) => r.json());
  } catch (err) {
    els.status.textContent = 'Could not load card index';
    clearLoading();
    return;
  }
  index.sort((a, b) => String(a.id).localeCompare(String(b.id), undefined, { numeric: true }));
  els.select.innerHTML = index
    .map((e, i) => `<option value="${i}">${esc(e.id)}</option>`)
    .join('');
  els.select.addEventListener('change', () => {
    const entry = index[Number(els.select.value)];
    if (entry) location.hash = encodeURIComponent(String(entry.id));
  });
  els.editor.addEventListener('input', debounce(run, 250));
  els.editor.addEventListener('input', updateSuggest);
  els.editor.addEventListener('input', updateGutter);
  els.editor.addEventListener('keyup', (e) => {
    if (['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(e.key)) updateSuggest();
  });
  els.editor.addEventListener('click', updateSuggest);
  els.editor.addEventListener('keydown', onEditorKeydown);
  els.editor.addEventListener('blur', () => setTimeout(hideSuggest, 120));
  els.editor.addEventListener('scroll', () => { hideSuggest(); syncGutter(); });
  els.download.addEventListener('click', downloadSource);
  els.preview.addEventListener('click', (e) => {
    const demo = e.target.closest('.program-card-demo a[data-youtube-id]');
    if (demo) {
      e.preventDefault();
      const id = demo.getAttribute('data-youtube-id');
      const wrap = document.createElement('div');
      wrap.className = 'video-embed';
      wrap.innerHTML = `<iframe src="https://www.youtube.com/embed/${encodeURIComponent(id)}?rel=0&autoplay=1" allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share" allowfullscreen title="YouTube video"></iframe>`;
      demo.replaceWith(wrap);
      return;
    }
    const a = e.target.closest('a.program-card-action--download[data-sha256]');
    if (!a) return;
    const href = a.getAttribute('href') || '';
    if (!href || href === '#') e.preventDefault(); // nothing to download in preview
    const main = a.closest('.program-card-hero__main');
    const box = main && main.querySelector('[data-sha-display]');
    if (box) { box.textContent = 'SHA256: ' + a.getAttribute('data-sha256'); box.hidden = false; }
  });
  window.addEventListener('hashchange', applyHash);
  if (index.length) applyHash();
  else clearLoading();
}

init();
