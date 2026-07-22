import { renderPanelArtwork } from '../render/cardPage.js';

function esc(value) {
  return String(value ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

/** Build the private browser page used by the CLI's shared canvas exporter. */
export function renderPanelExportPage(items) {
  const panels = items.map(item => `<section class="export-item" data-export-panel="${esc(item.id)}">${renderPanelArtwork(item, '/assets/panel.svg')}</section>`).join('');
  return `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Panel image export</title>
  <link rel="stylesheet" href="/assets/style.css">
  <link rel="stylesheet" href="/assets/program-cards.css">
  <style>
    html,body{margin:0;padding:0;background:#fff}.export-item{width:280px}.export-item .program-card-panel{margin:0}
  </style>
</head>
<body>
${panels}
<script type="module">
  import { renderPanelElementToSvg } from '/assets/panel-export.js';
  window.exportPanelSvg = async id => {
    const root = document.querySelector('[data-export-panel="' + CSS.escape(id) + '"]');
    if (!root) throw new Error('Unknown panel id: ' + id);
    return renderPanelElementToSvg(root.querySelector('.program-card-panel'));
  };
  window.panelExporterReady = true;
</script>
</body>
</html>`;
}
