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

const els = {
  select: document.getElementById('card-select'),
  editor: document.getElementById('yaml-source'),
  status: document.getElementById('editor-status'),
  diagnostics: document.getElementById('diagnostics'),
  preview: document.getElementById('card-preview'),
  download: document.getElementById('download-source'),
  tool: document.querySelector('.author-tool'),
};

// Reveal the editor once the index + first source have loaded, hiding the
// initial loading spinner.
function clearLoading() {
  if (els.tool) els.tool.classList.remove('is-loading');
}

let index = [];
let current = null; // the selected raw-info index entry

function debounce(fn, ms) {
  let t;
  return (...args) => { clearTimeout(t); t = setTimeout(() => fn(...args), ms); };
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
  if (source.error || !source.data) {
    els.preview.innerHTML = '<p class="preview-note">Fix the YAML syntax error above to preview the card.</p>';
    return;
  }
  try {
    // Normalize implicitly (behind the scenes) purely to render the preview.
    // Ancillary build inputs (docs, downloads, firmware, git dates) are absent
    // in the author tool, so pass empty stand-ins — the model derives panel,
    // controls, tags and metadata from the edited source.
    const card = buildCanonicalCardModel({
      folderName: current ? current.id : 'preview',
      slug: current ? current.slug : 'preview',
      info: {},
      rawYaml: source.data,
      docs: [],
      downloads: [],
      latestUf2: null,
      web: {},
      readmePath: '',
      sourceFile: current ? current.sourceFile : 'info.yaml',
      sourceUrl: '',
      readmeUrl: '',
      gitFirstDate: '',
      gitLastDate: '',
    });
    els.preview.innerHTML = renderCardArticle({
      card,
      panelImg: '../assets/program_cards/Standalone_computer_rev1.svg',
      yamlUrl: '',
      uf2Url: '',
      extraDocs: '',
    });
  } catch (err) {
    els.preview.innerHTML = `<p class="preview-note">Preview could not render: ${esc(err.message)}</p>`;
  }
}

function run() {
  const raw = els.editor.value;
  const source = parseSource(raw, current ? current.sourceFile : 'info.yaml');
  const result = validateInfoYaml(source);
  renderDiagnostics(result);
  renderPreview(source);
  els.status.textContent = source.error
    ? 'YAML syntax error'
    : `${result.errorCount} error(s), ${result.warningCount} warning(s)`;
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
  els.download.addEventListener('click', downloadSource);
  window.addEventListener('hashchange', applyHash);
  if (index.length) applyHash();
  else clearLoading();
}

init();
