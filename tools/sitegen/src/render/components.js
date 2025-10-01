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

  // Exact outlines from normalized SVG
  const SEG_SHAPES = {
    a: `<path d="m 48.628112,52.7705 h 18.40177 L 72.56232,47.238063 C 71.892924,46.568667 71.199716,45.875459 70.355695,45.031438 H 45.302299 c -0.844021,0.844021 -1.537229,1.537229 -2.206625,2.206625 l 5.529792,5.529792 z"/>`,
    b: `<path d="m 75.570632,50.246375 c -0.844021,-0.84402 -1.537229,-1.537229 -2.206625,-2.206625 l -5.529791,5.529792 v 18.515542 l 3.868208,3.680354 3.868208,-3.680354 V 50.24373 Z"/>`,
    c: `<path d="m 71.699778,77.331771 -3.868208,3.680354 v 19.081745 l 5.532437,5.5298 c 0.669396,-0.6694 1.362604,-1.36261 2.206625,-2.20663 V 81.009479 l -3.868208,-3.680354 z"/>`,
    d: `<path d="m 67.032528,100.89556 h -18.40177 l -5.12498,5.12498 2.611438,2.61144 h 24.238479 c 0.844021,-0.84402 1.537229,-1.53723 2.206625,-2.20663 l -5.532438,-5.52979 z"/>`,
    e: `<polygon points="173.38,811.84 183.26,821.72 202.63,802.35 202.63,730.23 188,716.32 173.38,730.23" transform="matrix(0.26458333,0,0,0.26458333,-5.7834501,-112.19456)"/>`,
    f: `<path d="m 47.82907,53.572188 -5.529792,-5.529792 c -0.669395,0.669396 -1.362604,1.362604 -2.206625,2.206625 v 21.841354 l 3.868209,3.680354 3.868208,-3.680354 V 53.574834 Z"/>`,
    g: `<polygon points="275.13,727.27 289.75,713.36 275.13,699.46 205.73,699.46 191.11,713.36 205.73,727.27" transform="matrix(0.26458333,0,0,0.26458333,-5.7834501,-112.19456)"/>`,
  };

  // Measured digit width from outlines
  const DIGIT_WIDTH = 36;  // adjust to taste
  const DIGIT_GAP = 6;
  const color = '#c2ab13';

  function makeDigit(xOffset, onSet) {
    const parts = [];
    for (const id of Object.keys(SEG_SHAPES)) {
      const on = onSet.has(id);
      parts.push(
        `<g transform="translate(${xOffset},0)">` +
          SEG_SHAPES[id].replace(/\/>$/, ` fill="${color}" fill-opacity="${on ? 1 : 0.25}"/>`) +
        `</g>`
      );
    }
    return parts.join('');
  }

  let x = 0;
  const pieces = [];
  for (const ch of digits) {
    const onSet = new Set(DIGIT_MAP[ch] || []);
    pieces.push(makeDigit(x, onSet));
    x += DIGIT_WIDTH + DIGIT_GAP;
  }

  const totalWidth = digits.length * DIGIT_WIDTH + (digits.length - 1) * DIGIT_GAP;
  const totalHeight = 65; // fits y range ~45–110

  return `<svg xmlns="http://www.w3.org/2000/svg" width="${totalWidth}" height="${totalHeight}" viewBox="40 41 ${totalWidth} 70" aria-hidden="true" focusable="false" role="img">${pieces.join('')}</svg>`;
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
	const ed = `${editorURL!='' ? `<a class="btn editor" href="${editorURL}">🛠️ Web Editor</a>` : ''}`;
  return `${back}${dl ? ' ' + dl : ''}${ed}`;
}
