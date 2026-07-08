// Author preview/editor page (static HTML).
//
// A card author selects a release, edits its raw info.yaml, and sees live
// schema diagnostics + a rendered card preview. The page loads the shared
// validator and card renderer (copied to ./lib/ at build time) as browser ES
// modules, with an import map for the `yaml` and `marked` vendor builds.

export function renderPreviewPage() {
  return `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Card author preview – Workshop Computer</title>
  <link rel="stylesheet" href="../assets/style.css">
  <link rel="stylesheet" href="../assets/program-cards.css">
  <link rel="stylesheet" href="../assets/github-markdown.css">
  <script type="importmap">
  {
    "imports": {
      "yaml": "./vendor/yaml/index.js",
      "marked": "./vendor/marked.esm.js"
    }
  }
  </script>
  <style>
    .author-tool{max-width:1400px;margin:0 auto;padding:16px}
    .author-head{margin-bottom:12px}
    .author-head h1{margin:0 0 4px}
    .author-note{color:var(--muted,#666);font-size:14px;margin:0}
    .author-grid{display:grid;grid-template-columns:minmax(360px,1fr) minmax(360px,1.2fr);gap:20px;align-items:start}
    .author-col{min-width:0}
    .author-bar{display:flex;gap:8px;align-items:center;margin-bottom:8px}
    .author-bar label{font-weight:600}
    #card-select{flex:0 0 auto}
    #editor-status{margin-left:auto;font-size:13px;color:var(--muted,#666)}
    #yaml-source{width:100%;height:60vh;box-sizing:border-box;font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:13px;line-height:1.5;padding:10px;border:1px solid var(--border,#ccc);border-radius:8px;resize:vertical;white-space:pre;tab-size:2}
    .panel-title{font-weight:600;margin:16px 0 6px}
    #diagnostics{border:1px solid var(--border,#ccc);border-radius:8px;padding:10px;max-height:32vh;overflow:auto;font-size:13px}
    .diag-clean{color:#1a7f37;margin:0}
    .diag-summary{margin:0 0 8px;font-weight:600}
    .diag-list{list-style:none;margin:0;padding:0;display:flex;flex-direction:column;gap:6px}
    .diag{display:grid;grid-template-columns:auto 1fr auto;gap:8px;align-items:baseline;padding:6px 8px;border-radius:6px}
    .diag--error{background:rgba(207,34,46,0.08)}
    .diag--warning{background:rgba(154,103,0,0.08)}
    .diag-sev{font-weight:700;font-size:11px;letter-spacing:.04em}
    .diag--error .diag-sev{color:#cf222e}
    .diag--warning .diag-sev{color:#9a6700}
    .diag-rule{font-size:11px;color:var(--muted,#888);font-family:ui-monospace,monospace}
    .diag-line{color:var(--muted,#888);font-size:12px}
    .preview-note{color:var(--muted,#666);padding:12px;border:1px dashed var(--border,#ccc);border-radius:8px}
    #card-preview{border:1px solid var(--border,#eee);border-radius:10px;padding:8px;overflow:auto}
  </style>
</head>
<body class="theme-light">
  <div class="author-tool">
    <header class="author-head">
      <h1>Card author preview</h1>
      <p class="author-note">Edit the raw <code>info.yaml</code> (the author schema in <code>documentation/info.yaml.md</code>). Diagnostics and the card preview update live from your source. The preview is generated for display only — your <code>info.yaml</code> is the source of truth.</p>
    </header>
    <div class="author-grid">
      <section class="author-col">
        <div class="author-bar">
          <label for="card-select">Card</label>
          <select id="card-select"></select>
          <button id="reload-source" class="btn secondary" type="button">Reload source</button>
          <span id="editor-status"></span>
        </div>
        <textarea id="yaml-source" spellcheck="false" aria-label="Raw info.yaml source"></textarea>
        <div class="panel-title">Diagnostics</div>
        <div id="diagnostics"></div>
      </section>
      <section class="author-col">
        <div class="panel-title">Preview</div>
        <div id="card-preview"></div>
      </section>
    </div>
  </div>
  <script type="module" src="./preview-client.js"></script>
</body>
</html>`;
}
