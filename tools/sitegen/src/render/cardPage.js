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
  const raw = String(release).split('/')[0].split('_')[0].trim();
  const number = Number.parseInt(raw, 10);
  return Number.isNaN(number) ? raw : String(number);
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

function renderDocumentation(card, extraSections = '', includeStructured = true) {
  const documentation = card.documentation || {};
  const blocks = [];
  if (includeStructured) {
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
  }
  const joined = blocks.join('') + (extraSections || '');
  if (!joined.trim()) return '';
  return `<section class="program-card-documentation" id="card-documentation">${joined}</section>`;
}

/**
 * Build the "extra" documentation HTML appended into the Documentation section:
 * the rendered README followed by any PDF docs. Shared by the site build and the
 * author preview so both show the same thing. `docs[].url` is whatever URL the
 * caller resolved (relative for the built detail page, raw GitHub for preview).
 * `inlinePdf` embeds an inline `<object>` preview of the first PDF; disable it
 * for the preview, where raw-GitHub PDF URLs would trigger a download.
 */
export function renderReadmeAndDocs({ readmeHtml = '', docs = [], inlinePdf = true } = {}) {
  const readmeSection = readmeHtml
    ? `<div class="program-card-section" id="card-readme"><h3>README</h3><div class="markdown-body">${readmeHtml}</div></div>`
    : '';
  const list = Array.isArray(docs) ? docs : [];
  let pdfSection = '';
  if (list.length && inlinePdf) {
    pdfSection = `
    <div class="program-card-section docs-section">
      <h3>Documentation PDF</h3>
      <object data="${esc(list[0].url)}" type="application/pdf" width="100%" height="700px">
        <p>PDF preview not available.</p>
      </object>
      <div style="margin-top:16px;text-align:center">
        <a class="btn download" href="${esc(list[0].url)}" download>Download ${esc(list[0].name)}</a>
      </div>
      ${list.length > 1 ? `<ul class="docs-list">${list.slice(1).map(d => `<li><a class="btn download" href="${esc(d.url)}" download>${esc(d.name)}</a></li>`).join('')}</ul>` : ''}
    </div>`;
  } else if (list.length) {
    pdfSection = `
    <div class="program-card-section docs-section">
      <h3>Documentation PDF</h3>
      <ul class="docs-list">${list.map(d => `<li><a class="btn download" href="${esc(d.url)}" target="_blank" rel="noopener noreferrer">${esc(d.name)}</a></li>`).join('')}</ul>
      <p class="preview-note">An inline PDF preview appears on the published card page.</p>
    </div>`;
  }
  return readmeSection + pdfSection;
}

/** Render the Audio section from classified audio-sample items. */
function renderAudio(samples) {
  if (!Array.isArray(samples) || !samples.length) return '';
  const items = samples.map(s => {
    const title = s.title ? `<p class="program-card-audio__title">${esc(s.title)}</p>` : '';
    if (s.kind === 'file') {
      return `<div class="program-card-audio__item">${title}<audio class="program-card-audio__player" controls preload="none" src="${esc(s.url)}"></audio></div>`;
    }
    if ((s.kind === 'soundcloud' || s.kind === 'bandcamp') && s.embedUrl) {
      const height = s.height || (s.kind === 'soundcloud' ? 166 : 120);
      return `<div class="program-card-audio__item">${title}<iframe class="program-card-audio__embed program-card-audio__embed--${esc(s.kind)}" src="${esc(s.embedUrl)}" width="100%" height="${height}" loading="lazy" frameborder="0" allow="autoplay" title="${esc(s.title || (s.kind + ' player'))}"></iframe></div>`;
    }
    return `<div class="program-card-audio__item">${title}<a class="program-card-audio__link" href="${esc(s.url)}" target="_blank" rel="noopener noreferrer">${esc(s.host || s.url)} ↗</a></div>`;
  }).join('');
  return `<section class="program-card-audio"><h2>Audio samples</h2>${items}</section>`;
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
 * @param {boolean} [opts.basic]    draft mode: render only basic fields + README/PDFs (skip the generated model sections)
 */
export function renderCardArticle({ card, panelImg, yamlUrl, uf2Url, extraDocs = '', basic = false }) {
  const metadata = card.metadata || {};
  const summary = card.summary || card.description || '';
  const sourceUrl = card.source_url || '';
  const readmeUrl = card.readme_url || '';
  const documentation = renderDocumentation(card, extraDocs, !basic);
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

  const dataSources = !basic && Array.isArray(card.source) && card.source.length
    ? `<div class="program-card-data-sources"><details><summary>Data sources</summary><p>${card.source.map(item => `<code title="${esc(item)}">${esc(truncate(item, 56))}</code>`).join(', ')}</p></details></div>`
    : '';

  const draftBar = card.draft
    ? `<aside class="program-card-draft-bar" aria-label="Draft documentation notice"><div class="program-card-draft-bar__message"><strong>Draft:</strong> <span>This documentation is work in progress and has not yet been approved by the card designer.</span></div><details class="program-card-draft-bar__details"><summary>I'm the designer. How should I fix this?</summary><p>Check the generated documentation against the card, then edit <a href="${esc(yamlUrl)}">the relevant <code>info.yaml</code> file</a> in the Workshop Computer repo. When everything is accurate, set <code>draft: false</code> in that YAML and submit the change.</p></details></aside>`
    : '';

  const uf2Downloads = Array.isArray(card.uf2_downloads) ? card.uf2_downloads : [];
  const downloadActions = uf2Downloads.length
    ? uf2Downloads.map(d => {
        // An external (mirror/store) link opens in a new tab, shows its host and
        // an External tag, and is never flashed or given a SHA readout.
        if (d.external) {
          const host = d.host ? `<small class="program-card-action__host">${esc(d.host)}</small>` : '';
          const tag = '<small class="program-card-action__tag">External \u2197</small>';
          return `<a class="program-card-action program-card-action--download program-card-action--external" href="${esc(d.url)}" target="_blank" rel="noopener noreferrer"><span>Download</span><small>${esc(d.name)}</small>${host}${tag}</a>`;
        }
        // A repo file downloads directly, enables WebUSB, and exposes its SHA256.
        const hashAttr = d.sha256 ? ` data-sha256="${esc(d.sha256)}"` : '';
        return `<a class="program-card-action program-card-action--download" href="${esc(d.url)}" download data-uf2-url="${esc(d.url)}"${hashAttr}><span>Download</span><small>${esc(d.name)}</small></a>`;
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
      ${basic ? '' : renderTags(card)}
      <h1><span class="program-card-page__number">${esc(cardNumber(card))}</span> ${esc(inline(card.title || card.id || 'Untitled card'))}</h1>
      ${summary ? `<p>${esc(truncate(summary, 240))}</p>` : ''}
      <div class="program-card-hero__meta">${metadata.creator ? `<span>By ${esc(metadata.creator)}</span>` : ''}${basic ? '' : memoryMarkup}</div>
      <div class="program-card-actions" aria-label="Card actions">${downloadActions}${editorAction}</div>
      <div class="program-card-sha" data-sha-display role="status" aria-live="polite" hidden>SHA256: <code class="program-card-sha__value" data-sha-value></code> <button type="button" class="program-card-sha__verify" data-verify-open>How to verify</button></div>
      <div class="program-card-hero__links" aria-label="Further card links">${documentation ? `<a href="#card-documentation">Read more</a>` : ''}<a href="${esc(discussionUrl)}">Support &amp; questions</a><button id="connectToggle" class="connect-toggle" type="button" role="switch" aria-checked="false" aria-label="Connect to RP2040 via WebUSB" title="Reboot computer into programming mode before connecting"><span class="c-status" aria-hidden="true"></span><span class="c-label">Connect workshop computer</span></button></div>
    </div>
  </header>`;

  const demo = !basic && firstVideo
    ? `<section class="program-card-demo"><a href="${esc(firstVideo.url)}" data-youtube-id="${esc(firstVideo.id)}"><span class="program-card-demo__media" aria-hidden="true"><img src="https://img.youtube.com/vi/${esc(firstVideo.id)}/hqdefault.jpg" alt="" loading="lazy"></span><span class="program-card-demo__text"><span>Watch</span><strong>${esc(firstVideo.title || 'Demo video')}</strong></span></a></section>`
    : '';

  const audio = basic ? '' : renderAudio(card.audio_samples);

  const quickStart = !basic && Array.isArray(card.quick_start) && card.quick_start.length
    ? `<section class="program-card-quick-start"><h2>Quick start</h2><ol>${card.quick_start.map(step => `<li>${esc(stripTags(step))}</li>`).join('')}</ol></section>`
    : '';

  const use = basic ? '' : `<section class="program-card-use-section">
    <h2 class="program-card-use__title">Panel</h2>
    <div class="program-card-use">
    <div class="program-card-use__panel">${renderPanel(card, panelImg)}</div>
    <div class="program-card-use__reference">
      ${controlsMarkup ? `<section class="program-card-section"><h3>Controls</h3>${controlsMarkup}</section>` : ''}
      ${switchMarkup ? `<section class="program-card-section"><h3>Switch</h3>${switchMarkup}</section>` : ''}
      ${renderSocketList('Inputs', panel.inputs, panelPositions.inputs)}
      ${renderSocketList('Outputs', panel.outputs, panelPositions.outputs)}
      ${ledsMarkup ? `<section class="program-card-section"><h3>LEDs</h3>${ledsMarkup}</section>` : ''}
    </div>
  </div>
  </section>`;

  const notesMarkup = !basic && Array.isArray(card.notes) && card.notes.length
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

  const verifyModal = `<dialog class="verify-modal" data-verify-modal aria-label="How to verify your download">
    <div class="verify-modal__body">
      <h2>Verify your download</h2>
      <p>Confirm the file you downloaded really is that new firmware.</p>
      <h3>macOS / Linux (Terminal)</h3>
      <pre><code>shasum -a 256 firmware.uf2</code></pre>
      <p class="verify-modal__note">Linux also has <code>sha256sum firmware.uf2</code>.</p>
      <h3>Windows (PowerShell)</h3>
      <pre><code>Get-FileHash firmware.uf2 -Algorithm SHA256</code></pre>
      <p>Compare the result to the SHA256 on the website — it should match exactly.</p>
      <button type="button" class="btn verify-modal__close" data-verify-close>Close</button>
    </div>
  </dialog>`;

  return `<article class="program-cards program-card-page">
    ${draftBar}
    ${hero}
    ${demo}
    ${audio}
    ${quickStart}
    ${use}
    ${documentation}
    ${about}
    ${verifyModal}
  </article>`;
}
