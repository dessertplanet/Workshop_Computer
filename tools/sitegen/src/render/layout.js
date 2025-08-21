export function renderLayout({ title, content, relativeRoot = '.' }) {
  return `<!doctype html>
<html lang="en" class="theme-dark" data-theme="dark">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>${title ? String(title).replace(/</g, '&lt;') : 'Workshop Computer'}</title>
  <script>(function(){try{var k='wc-theme';var d=document.documentElement;var prefersDark=window.matchMedia&&window.matchMedia('(prefers-color-scheme: dark)').matches;var t=localStorage.getItem(k);if(t!=='dark'&&t!=='light'){t=prefersDark?'dark':'light';}d.classList.remove('theme-dark','theme-light');d.classList.add('theme-'+t);d.setAttribute('data-theme',t);}catch(e){}})();</script>
  <link rel="stylesheet" href="${relativeRoot}/assets/style.css" />
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
      <p>Built from this repository's releases folder. Deployed via GitHub Pages.</p>
    </div>
  </footer>
  <script>(function(){var btn=document.getElementById('themeToggle');if(!btn)return;var k='wc-theme';function cur(){return document.documentElement.getAttribute('data-theme')||'dark';}function set(t){try{localStorage.setItem(k,t);}catch(e){}var d=document.documentElement;d.classList.remove('theme-dark','theme-light');d.classList.add('theme-'+t);d.setAttribute('data-theme',t);update();}function update(){var t=cur();btn.setAttribute('aria-checked',String(t==='dark'));}btn.addEventListener('click',function(){set(cur()==='dark'?'light':'dark');});update();})();</script>
</body>
</html>`;
}

// CSS moved to tools/sitegen/assets/style.css and referenced via link tag in renderLayout
