// Program-card detail page renderer.
//
// Port of the MTM program-card detail layout (TomWhitwell/MTM_Newsite_2022
// _layouts/program_card.html and the renderCardPage function in
// assets/program_cards/preview-tools.js), adapted to consume the canonical
// card model produced by src/model/card.js and to render inside the local
// sitegen layout.

import { marked } from 'marked';
import { panelPositions } from './panelPositions.js';

const DEFAULT_DISCUSSION = 'https://discord.com/channels/1210238368898879569/1484219323039092938';

function esc(value) {
  return String(value == null ? '' : value)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

function stripTags(value) {
  return String(value == null ? '' : value).replace(/<[^>]*>/g, '');
}

function inline(value) {
  // Panel labels may carry newlines for two-line panel rendering; collapse them
  // when the label is used in flowing text (controls, sockets, headings).
  return stripTags(value).replace(/\s*\n\s*/g, ' ').trim();
}

function truncate(value, length) {
  const text = stripTags(value).trim();
  if (text.length <= length) return text;
  return text.slice(0, Math.max(0, length - 1)).trimEnd() + '\u2026';
}

function markdownBlock(text) {
  const val = String(text ?? '').trim();
  if (!val) return '';
  return marked.parse(val);
}

function cardNumber(card) {
  const release = card.release || card.id || '';
  return String(release).split('/')[0].split('_')[0].trim();
}

function renderTags(card) {
  const tags = Array.isArray(card.tags) ? card.tags.filter(Boolean) : [];
  if (!tags.length) return '';
  return `<span class="program-card-tags">${tags
    .map(tag => `<span class="program-card-tag program-card-tag--${esc(String(tag).toLowerCase())}">${esc(tag)}</span>`)
    .join('')}</span>`;
}

function renderPanelLabel(kind, pos, item) {
  if (!item || !item.label) return '';
  const label = esc(stripTags(item.label)).replace(/\n/g, '<br>');
  return `<span class="program-card-panel__label program-card-panel__label--${kind}" style="left: ${esc(pos.left)}%; top: ${esc(pos.top)}%;">${label}</span>`;
}

function renderPanel(card, panelImg) {
  const panel = card.panel || {};
  const labels = [];
  for (const pos of panelPositions.controls) labels.push(renderPanelLabel('control', pos, (panel.controls || {})[pos.key]));
  for (const pos of panelPositions.inputs) labels.push(renderPanelLabel('input', pos, (panel.inputs || {})[pos.key]));
  for (const pos of panelPositions.outputs) labels.push(renderPanelLabel('output', pos, (panel.outputs || {})[pos.key]));
  return `<figure class="program-card-panel" aria-label="Workshop Computer panel"><img src="${esc(panelImg)}" alt="Workshop Computer panel">${labels.join('')}</figure>`;
}

function renderSocketList(title, sockets, positions) {
  if (!sockets) return '';
  const items = (positions || []).map(pos => {
    const socket = sockets[pos.key];
    if (!socket || (!socket.description && !socket.label)) return '';
    return `<div class="program-card-socket"><strong>${esc(inline(socket.label || pos.name || pos.key))}</strong><p>${esc(truncate(socket.description || socket.label || '', 220))}</p></div>`;
  }).join('');
  if (!items.trim()) return '';
  return `<section class="program-card-section"><h3>${esc(title)}</h3><div class="program-card-socket-list">${items}</div></section>`;
}

function renderDocumentation(card, extraSections = '') {
  const documentation = card.documentation || {};
  const blocks = [];
  if (String(documentation.intro || '').trim()) {
    blocks.push(`<div class="program-card-section"><h3>Documentation</h3>${markdownBlock(documentation.intro)}</div>`);
  }
  for (const section of (Array.isArray(documentation.sections) ? documentation.sections : [])) {
    const title = inline(section?.title || '');
    const body = String(section?.body || '').trim();
    if (!body) continue;
    const heading = title ? title.replace(/\b\w/g, c => c.toUpperCase()) : 'Documentation';
    blocks.push(`<div class="program-card-section"><h3>${esc(heading)}</h3>${markdownBlock(body)}</div>`);
  }
  const joined = blocks.join('') + (extraSections || '');
  if (!joined.trim()) return '';
  return `<section class="program-card-documentation">${joined}</section>`;
}

/**
 * Render the full program-card detail article from a canonical card model.
 *
 * @param {object} opts
 * @param {object} opts.card         canonical card model
 * @param {string} opts.panelImg     href to the panel SVG (relative to page)
 * @param {string} opts.yamlUrl      GitHub URL to the source info.yaml
 * @param {string} [opts.uf2Url]     direct .uf2 download URL (enables WebUSB)
 * @param {string} [opts.extraDocs]  extra HTML appended into the documentation area (README/PDFs)
 */
export function renderCardArticle({ card, panelImg, yamlUrl, uf2Url, extraDocs = '' }) {
  const metadata = card.metadata || {};
  const summary = card.summary || card.description || '';
  const sourceUrl = card.source_url || '';
  const readmeUrl = card.readme_url || '';
  const discussionUrl = metadata.discussion_url || DEFAULT_DISCUSSION;
  const firstVideo = Array.isArray(card.videos) && card.videos[0];
  const panel = card.panel || {};
  const controls = panel.controls || {};

  const controlsMarkup = ['main', 'x', 'y', 'z'].map(key => {
    const control = controls[key];
    if (!control || !control.description) return '';
    return `<p><strong>${esc(inline(control.label || key).toUpperCase())}</strong> ${esc(truncate(control.description, 220))}</p>`;
  }).join('');

  const switchEntries = card.switch_modes ? Object.entries(card.switch_modes).filter(entry => entry[1]) : [];
  const switchMarkup = switchEntries
    .map(([key, value]) => `<p><strong>${esc(key.charAt(0).toUpperCase() + key.slice(1))}</strong> ${esc(truncate(value, 240))}</p>`)
    .join('');

  const ledsMarkup = (card.leds || []).map(led => `<p>${esc(truncate(led, 260))}</p>`).join('');

  const dataSources = Array.isArray(card.source) && card.source.length
    ? `<div class="program-card-data-sources"><details><summary>Data sources</summary><p>${card.source.map(item => `<code title="${esc(item)}">${esc(truncate(item, 56))}</code>`).join(', ')}</p></details></div>`
    : '';

  const draftBar = card.draft
    ? `<aside class="program-card-draft-bar" aria-label="Draft documentation notice"><div class="program-card-draft-bar__message"><strong>Draft:</strong> <span>This documentation is work in progress and has not yet been approved by the card designer.</span></div><details class="program-card-draft-bar__details"><summary>I'm the designer. How should I fix this?</summary><p>Check the generated documentation against the card, then edit <a href="${esc(yamlUrl)}">the relevant <code>info.yaml</code> file</a> in the Workshop Computer repo. When everything is accurate, set <code>draft: false</code> in that YAML and submit the change.</p></details></aside>`
    : '';

  const uf2Downloads = Array.isArray(card.uf2_downloads) ? card.uf2_downloads : [];
  const downloadActions = uf2Downloads.length
    ? uf2Downloads.map(d => {
        const hashAttr = d.sha256 ? ` data-sha256="${esc(d.sha256)}"` : '';
        const desc = d.description ? `<small class="program-card-action__desc">${esc(d.description)}</small>` : '';
        return `<a class="program-card-action program-card-action--download" href="${esc(d.url)}" download data-uf2-url="${esc(d.url)}"${hashAttr}><span>Download</span><small>${esc(d.name)}</small>${desc}</a>`;
      }).join('')
    : (() => {
        const downloadHref = uf2Url || sourceUrl;
        const downloadAttrs = uf2Url ? ` download data-uf2-url="${esc(uf2Url)}"` : '';
        return `<a class="program-card-action program-card-action--download" href="${esc(downloadHref)}"${downloadAttrs}><span>Download</span>${metadata.version ? `<small>Firmware ${esc(metadata.version)}</small>` : ''}</a>`;
      })();
  const editorAction = metadata.editor_url
    ? `<a class="program-card-action program-card-action--editor" href="${esc(metadata.editor_url)}"><span>Launch web editor</span><small>${esc(metadata.editor_note || 'Configure this card in your browser')}</small></a>`
    : '';

  const memoryMarkup = card.memory && card.memory.size
    ? `<span>${esc(String(card.memory.size).toUpperCase())} card ${esc(card.memory.requirement || 'supported')}</span>`
    : '';

  const hero = `<header class="program-card-hero">
    <div class="program-card-hero__main">
      ${renderTags(card)}
      <h1><span class="program-card-page__number">${esc(cardNumber(card))}</span> ${esc(inline(card.title || card.id || 'Untitled card'))}</h1>
      ${summary ? `<p>${esc(truncate(summary, 240))}</p>` : ''}
      <div class="program-card-hero__meta">${metadata.creator ? `<span>By ${esc(metadata.creator)}</span>` : ''}${memoryMarkup}</div>
      <div class="program-card-actions" aria-label="Card actions">${downloadActions}${editorAction}</div>
      <div class="program-card-sha" data-sha-display role="status" aria-live="polite" hidden></div>
      <div class="program-card-hero__links" aria-label="Further card links">${readmeUrl ? `<a href="${esc(readmeUrl)}">Read more</a>` : ''}<a href="${esc(discussionUrl)}">Support &amp; questions</a></div>
    </div>
  </header>`;

  const demo = firstVideo
    ? `<section class="program-card-demo"><a href="${esc(firstVideo.url)}"><span class="program-card-demo__media" aria-hidden="true"><img src="https://img.youtube.com/vi/${esc(firstVideo.id)}/hqdefault.jpg" alt="" loading="lazy"></span><span class="program-card-demo__text"><span>Watch</span><strong>${esc(firstVideo.title || 'Demo video')}</strong></span></a></section>`
    : '';

  const quickStart = Array.isArray(card.quick_start) && card.quick_start.length
    ? `<section class="program-card-quick-start"><h2>Quick start</h2><ol>${card.quick_start.map(step => `<li>${esc(stripTags(step))}</li>`).join('')}</ol></section>`
    : '';

  const use = `<div class="program-card-use">
    <div class="program-card-use__panel">${renderPanel(card, panelImg)}</div>
    <div class="program-card-use__reference">
      ${controlsMarkup ? `<section class="program-card-section"><h3>Controls</h3>${controlsMarkup}</section>` : ''}
      ${switchMarkup ? `<section class="program-card-section"><h3>Switch</h3>${switchMarkup}</section>` : ''}
      ${renderSocketList('Inputs', panel.inputs, panelPositions.inputs)}
      ${renderSocketList('Outputs', panel.outputs, panelPositions.outputs)}
      ${ledsMarkup ? `<section class="program-card-section"><h3>LEDs</h3>${ledsMarkup}</section>` : ''}
    </div>
  </div>`;

  const documentation = renderDocumentation(card, extraDocs);

  const notesMarkup = Array.isArray(card.notes) && card.notes.length
    ? `<section class="program-card-section"><h3>Notes</h3>${card.notes.map(note => `<p>${esc(truncate(note, 220))}</p>`).join('')}</section>`
    : '';

  const about = `<footer class="program-card-about">
    <section class="program-card-section"><h3>About this card</h3><dl>
      ${metadata.creator ? `<div><dt>Creator</dt><dd>${esc(metadata.creator)}</dd></div>` : ''}
      ${metadata.language ? `<div><dt>Language</dt><dd>${esc(metadata.language)}</dd></div>` : ''}
      ${metadata.version ? `<div><dt>Version</dt><dd>${esc(metadata.version)}</dd></div>` : ''}
      ${metadata.status ? `<div><dt>Status</dt><dd>${esc(metadata.status)}</dd></div>` : ''}
      ${metadata.license ? `<div><dt>License</dt><dd>${esc(metadata.license)}</dd></div>` : ''}
      ${metadata.updated && metadata.updated !== 'n/a' ? `<div><dt>Updated</dt><dd>${esc(metadata.updated)}</dd></div>` : ''}
      ${metadata.created && metadata.created !== 'n/a' ? `<div><dt>Created</dt><dd>${esc(metadata.created)}</dd></div>` : ''}
      ${card.memory && card.memory.size ? `<div><dt>Card memory</dt><dd>${esc(String(card.memory.size).toUpperCase())} ${esc(card.memory.requirement || 'supported')}</dd></div>` : ''}
      ${readmeUrl ? `<div><dt>Read more</dt><dd><a href="${esc(readmeUrl)}">README in the Workshop Computer repo</a></dd></div>` : ''}
      ${sourceUrl ? `<div><dt>Source</dt><dd><a href="${esc(sourceUrl)}">Release folder on GitHub</a></dd></div>` : ''}
      <div><dt>Support</dt><dd><a href="${esc(discussionUrl)}">Ask questions, contact the designer, or share feedback</a></dd></div>
    </dl></section>
    ${notesMarkup}
    ${dataSources}
  </footer>`;

  return `<article class="program-cards program-card-page">
    ${draftBar}
    ${hero}
    ${demo}
    ${quickStart}
    ${use}
    ${documentation}
    ${about}
  </article>`;
}
