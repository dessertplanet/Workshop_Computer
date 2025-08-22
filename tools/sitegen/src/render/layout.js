export function renderLayout({ title, content, relativeRoot = '.', repoUrl = 'https://github.com/TomWhitwell/Workshop_Computer' }) {
  return `<!doctype html>
<html lang="en" class="theme-dark" data-theme="dark">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>${title ? String(title).replace(/</g, '&lt;') : 'Workshop Computer'}</title>
  <script>(function(){try{var k='wc-theme';var d=document.documentElement;var prefersDark=window.matchMedia&&window.matchMedia('(prefers-color-scheme: dark)').matches;var t=localStorage.getItem(k);if(t!=='dark'&&t!=='light'){t=prefersDark?'dark':'light';}d.classList.remove('theme-dark','theme-light');d.classList.add('theme-'+t);d.setAttribute('data-theme',t);}catch(e){}})();</script>
  <link rel="stylesheet" href="${relativeRoot}/assets/github-markdown.css" />
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;600;700&display=swap" rel="stylesheet">
  <link rel="stylesheet" href="${relativeRoot}/assets/style.css" />
  <style>
    .filter-bar{margin:0 0 16px 0}
    .filter-row{display:flex;flex-wrap:wrap;gap:10px;align-items:center}
    .filter-row label{font-weight:600;color:var(--muted)}
    .filter-row select{background:transparent;border:1px solid var(--border);color:var(--text);padding:8px;border-radius:8px}
  </style>
</head>
<body>
  <header class="site-header">
    <div class="container header-bar">
      <h1 class="site-title"><a href="${relativeRoot}/index.html">Workshop Computer Program Cards</a></h1>
      <button id="themeToggle" class="theme-toggle" type="button" role="switch" aria-checked="true" aria-label="Toggle color scheme">
        <span class="track"><span class="thumb"></span><span class="icons" aria-hidden="true">☀️<span class="gap"></span>🌙</span></span>
      </button>
    </div>
  </header>
  <main class="container">
    ${content}
  </main>
  <footer class="site-footer">
    <div class="container">
      <p>
        <a href="${repoUrl}" target="_blank" rel="noopener">View on GitHub</a>
        <span aria-hidden="true">•</span>
        <a href="https://www.musicthing.co.uk/workshopsystem/" target="_blank" rel="noopener">Music Thing Workshop System</a>
      </p>
    </div>
  </footer>
  <script>(function(){
    // Theme toggle
    var btn=document.getElementById('themeToggle');if(btn){var k='wc-theme';function cur(){return document.documentElement.getAttribute('data-theme')||'dark';}function set(t){try{localStorage.setItem(k,t);}catch(e){}var d=document.documentElement;d.classList.remove('theme-dark','theme-light');d.classList.add('theme-'+t);d.setAttribute('data-theme',t);update();}function update(){var t=cur();btn.setAttribute('aria-checked',String(t==='dark'));}btn.addEventListener('click',function(){set(cur()==='dark'?'light':'dark');});update();}

    // Filters (index only)
    var typeSel=document.getElementById('filter-type');
    var creatorSel=document.getElementById('filter-creator');
    var langSel=document.getElementById('filter-language');
  var countEl=document.getElementById('cards-count');
    function normTypeKey(v){
      return String(v||'').toLowerCase().replace(/\s+/g,' ').replace(/[^a-z0-9\s]+/g,' ').replace(/\s+/g,' ').trim();
    }
    function applyFilters(){
      var t=typeSel&&typeSel.value?normTypeKey(typeSel.value):'';
      var c=creatorSel&&creatorSel.value?creatorSel.value.toLowerCase():'';
      var l=langSel&&langSel.value?langSel.value.toLowerCase():'';
      var cards=document.querySelectorAll('.grid .card');
      var shown=0;
      cards.forEach(function(card){
        var ct=normTypeKey(card.getAttribute('data-type')||'');
        var cr=(card.getAttribute('data-creator')||'').toLowerCase();
        var lg=(card.getAttribute('data-language')||'').toLowerCase();
        var ok=true;
        if(t && ct!==t) ok=false;
        if(c && cr!==c) ok=false;
        if(l && lg!==l) ok=false;
        card.style.display=ok?'':'none';
        if(ok) shown++;
      });
      if(countEl) countEl.textContent=String(shown);
    }
    function wire(sel){if(!sel) return; sel.addEventListener('change',applyFilters);} 
    wire(typeSel); wire(creatorSel); wire(langSel);
    // Initialize count on load
    if(typeSel||creatorSel||langSel) applyFilters();
  })();</script>
</body>
</html>`;
}

// CSS moved to tools/sitegen/assets/style.css and referenced via link tag in renderLayout
