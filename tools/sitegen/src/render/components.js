// UI rendering helpers and small components

// Seven-segment program number
export function sevenSegmentSvg(numStr) {
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
  const W = 42, H = 72, T = 7, PAD = 3, GAP = 6, DIGIT_GAP = 9;
  const half = H / 2;
  const vertLenTop = half - T - (PAD + GAP);
  const vertLenBot = vertLenTop;
  function segRects(xOffset, onSet) {
    const rx = 3, ry = 3;
    const rects = [];
    rects.push({ id:'a', x: xOffset + 7.5, y: PAD, w: W - 15, h: T });
    rects.push({ id:'d', x: xOffset + 7.5, y: H - T - PAD, w: W - 15, h: T });
    rects.push({ id:'g', x: xOffset + 7.5, y: half - T/2, w: W - 15, h: T });
    rects.push({ id:'f', x: xOffset + PAD, y: PAD + T + (GAP - T/2), w: T, h: vertLenTop });
    rects.push({ id:'e', x: xOffset + PAD, y: half + GAP, w: T, h: vertLenBot });
    rects.push({ id:'b', x: xOffset + W - T - PAD, y: PAD + T + (GAP - T/2), w: T, h: vertLenTop });
    rects.push({ id:'c', x: xOffset + W - T - PAD, y: half + GAP, w: T, h: vertLenBot });
    return rects.map(r => {
      const on = onSet.has(r.id);
      const fill = '#c2ab13';
      const opacity = on ? '1' : '0.3';
      return `<rect class="seg ${on ? 'on' : 'off'} ${r.id}" x="${r.x}" y="${r.y}" width="${r.w}" height="${r.h}" rx="${rx}" ry="${ry}" fill="${fill}" fill-opacity="${opacity}"/>`;
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
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${totalWidth}" height="${H}" viewBox="0 0 ${totalWidth} ${H}" aria-hidden="true" focusable="false" role="img">${parts.join('')}</svg>`;
}

// Status mapping to CSS classes
export function mapStatusToClass(raw) {
  const t = String(raw || '').toLowerCase().trim();
  if (/(released|ready|stable|production)/.test(t)) return 'status-stable';
  if (/beta/.test(t)) return 'status-beta';
  if (/alpha/.test(t)) return 'status-alpha';
  if (/(\brc\b|release candidate)/.test(t)) return 'status-rc';
  if (/(proof of concept|poc|experimental)/.test(t)) return 'status-experimental';
  if (/(wip|work in progress|functional.*wip)/.test(t)) return 'status-wip';
  if (/(working but simple|simple)/.test(t)) return 'status-draft';
  if (/(mostly complete)/.test(t)) return 'status-rc';
  if (/(deprecated|obsolete)/.test(t)) return 'status-deprecated';
  if (/maintenance/.test(t)) return 'status-maintenance';
  if (/archived/.test(t)) return 'status-archived';
  if (/(none|n\/a|unknown|^$)/.test(t)) return 'status-unknown';
  return 'status-unknown';
}

// Meta list renderer
export function renderMetaList({ creator, version, language, statusRaw, statusClass }) {
  return [
    `<li><span class="label">Creator</span><span class="value">${creator}</span></li>`,
    `<li><span class="label">Version</span><span class="value">${version}</span></li>`,
    `<li><span class="label">Language</span><span class="value">${language}</span></li>`,
    `<li><span class="label">Status</span><span class="value"><span class="status-pill ${statusClass}">${statusRaw}</span></span></li>`,
  ].join('');
}

// Common action buttons (Back + UF2 downloads)
export function renderActionButtons(uf2Downloads, editorURL) {
  const back = `<a class="btn" href="../../index.html">⬅️ Back to All Programs</a>`;
	const dl = (uf2Downloads || []).map(d => `<a class="btn download" href="${d.url}" download>💾 Download ${d.name}</a>`).join(' ');
	const ed = `${editorURL!='' ? `<a class="btn editor" href="${editorURL}">⚙ Web editor</a>` : ''}`;
  return `${back}${dl ? ' ' + dl : ''}${ed}`;
}
