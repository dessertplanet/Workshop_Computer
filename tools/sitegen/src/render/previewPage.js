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
    .author-tool{max-width:1400px;margin:0 auto;padding:16px;height:100vh;box-sizing:border-box;display:flex;flex-direction:column}
    .author-head{margin-bottom:12px}
    .author-head h1{margin:0 0 4px}
    .author-note{color:var(--muted,#666);font-size:14px;margin:0}
    .author-grid{display:grid;grid-template-columns:minmax(360px,1fr) minmax(360px,1.2fr);gap:20px;align-items:stretch;flex:1 1 auto;min-height:0}
    .author-col{min-width:0;min-height:0;overflow:auto;padding-right:4px}
    .author-bar{display:flex;gap:8px;align-items:center;margin-bottom:8px}
    .author-bar label{font-weight:600}
    #card-select{flex:0 0 auto;padding:10px 40px 10px 14px;border:1px solid var(--border,#ccc);border-radius:8px;color:inherit;font:inherit;line-height:1.2;box-sizing:border-box;appearance:none;-webkit-appearance:none;-moz-appearance:none;background:transparent url("data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%23666' stroke-width='2.5' stroke-linecap='round' stroke-linejoin='round'><path d='M6 9l6 6 6-6'/></svg>") no-repeat right 13px center;background-size:16px}
    #editor-status{margin-left:auto;font-size:13px;color:var(--muted,#666)}
    .editor-wrap{display:flex;margin-top:8px;height:60vh;border:1px solid var(--border,#ccc);border-radius:8px;overflow:hidden;resize:vertical;background:#fff}
    .editor-gutter{flex:0 0 auto;overflow:hidden;padding:10px 6px 10px 10px;background:var(--gutter-bg,#f6f8fa);border-right:1px solid var(--border,#eee);color:var(--muted,#adb5bd);font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:13px;line-height:1.5;text-align:right;user-select:none}
    .editor-gutter__inner{white-space:pre}
    #yaml-source{flex:1 1 auto;height:100%;box-sizing:border-box;font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:13px;line-height:1.5;padding:10px;border:none;border-radius:0;resize:none;white-space:pre;tab-size:2;background:transparent}
    .panel-title{font-weight:600;margin:16px 0 6px}
    #diagnostics{border:1px solid var(--border,#ccc);border-radius:8px;padding:10px;font-size:13px}
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
    #card-preview .program-card-panel-rail{display:none}
    #card-preview .program-card-use__panel{display:block;grid-area:auto;justify-self:start;margin:0 0 22px;position:static;width:100%}
    #card-preview .program-card-use__reference{grid-area:auto}
    @keyframes author-spin{to{transform:rotate(360deg)}}
    .author-loading{display:none;align-items:center;justify-content:center;gap:10px;padding:64px 0;color:var(--muted,#666);font-size:14px}
    .author-tool.is-loading .author-loading{display:flex}
    .author-tool.is-loading .author-grid{display:none}
    .author-spinner{width:22px;height:22px;border:3px solid var(--border,#ccc);border-top-color:var(--accent,#0969da);border-radius:50%;animation:author-spin .8s linear infinite}
    #uf2-suggest{position:absolute;z-index:1000;margin:0;padding:4px;list-style:none;background:var(--bg,#fff);border:1px solid var(--border,#ccc);border-radius:6px;box-shadow:0 6px 20px rgba(0,0,0,.15);max-height:200px;overflow:auto;font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:12px;min-width:220px}
    #uf2-suggest li{padding:4px 8px;border-radius:4px;cursor:pointer;white-space:nowrap}
    #uf2-suggest li.active,#uf2-suggest li:hover{background:var(--accent,#0969da);color:#fff}
  </style>
</head>
<body class="theme-light">
  <div class="author-tool is-loading">
    <header class="author-head">
      <h1>Card author preview</h1>
      <p class="author-note">Edit the raw <code>info.yaml</code> (the author schema in <code>documentation/info.yaml.md</code>). Diagnostics and the card preview update live from your source. The preview is generated for display only — your <code>info.yaml</code> is the source of truth.</p>
    </header>
    <div class="author-loading" role="status" aria-live="polite">
      <span class="author-spinner" aria-hidden="true"></span>
      <span>Loading cards…</span>
    </div>
    <div class="author-grid">
      <section class="author-col">
        <div class="author-bar">
          <label for="card-select">Card</label>
          <select id="card-select"></select>
          <button id="download-source" class="btn secondary" type="button">Download info.yaml</button>
          <span id="editor-status"></span>
        </div>
        <div class="panel-title">Diagnostics</div>
        <div id="diagnostics"></div>
        <div class="editor-wrap">
          <div class="editor-gutter" aria-hidden="true"><div id="gutter-inner" class="editor-gutter__inner">1</div></div>
          <textarea id="yaml-source" spellcheck="false" aria-label="Raw info.yaml source" tabindex="2"></textarea>
        </div>
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
