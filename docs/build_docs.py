#!/usr/bin/env python3
"""Build static HTML docs from the Markdown source files.

Outputs to docs-site/ at the repository root.  Run from any directory:

    python docs/build_docs.py

Requires:  markdown>=3.7  (pip install markdown)
"""

import os
import sys

try:
    import markdown as md_lib
except ImportError:
    sys.exit("Error: 'markdown' package not found.  Run: pip install markdown")

# ── Paths ────────────────────────────────────────────────────────────────────

DOCS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(DOCS_DIR)
OUT_DIR = os.path.join(REPO_ROOT, "docs-site")

GUIDES = [
    ("user_guide.md",      "user-guide.html",      "User Guide"),
    ("technical_guide.md", "technical-guide.html",  "Technical Guide"),
]

MD_EXTENSIONS = ["toc", "fenced_code", "tables", "attr_list"]
MD_EXT_CONFIG = {
    "toc": {"permalink": True, "toc_depth": "2-3"},
}

# ── Embedded CSS ─────────────────────────────────────────────────────────────
# Mirrors Gooey's design language (fonts/colours/variables) without requiring
# Flask.  Google Fonts are loaded from the network (works on GitHub Pages).

CSS = """
@import url('https://fonts.googleapis.com/css2?family=Martian+Mono:wght@300;400;500&family=Playwrite+DE+Grund:wght@400;500&display=swap');

:root {
  --bg:           #ffffff;
  --bg-alt:       #f4f5f6;
  --bg-card:      #ffffff;
  --bg-hover:     #eef0f3;
  --border:       #dddddd;
  --text-dark:    #2A2F36;
  --text-medium:  #6C7A89;
  --text-light:   #ABB7B7;
  --accent:       #90849c;
  --accent-hover: #7a6f8a;
  --accent-dim:   rgba(144,132,156,0.12);
  --header-bg:    #DAC7FF;
  --header-text:  #2A2F36;
  --radius:       5px;
  --radius-lg:    8px;
  --shadow:       0 1px 4px rgba(0,0,0,0.08);
  --shadow-md:    0 2px 8px rgba(0,0,0,0.1);
  --font:         "Martian Mono", monospace;
  --font-title:   "Playwrite DE Grund", cursive;
}

*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

body {
  font-family: var(--font);
  font-size: 18px;
  font-weight: 300;
  color: var(--text-dark);
  background: var(--bg-alt);
  line-height: 1.6;
  display: flex;
  flex-direction: column;
  min-height: 100vh;
}

/* ── Header ── */
.docs-header {
  background: var(--header-bg);
  box-shadow: 0 2px 6px rgba(0,0,0,0.10);
  position: sticky;
  top: 0;
  z-index: 20;
  display: flex;
  align-items: stretch;
  min-height: 64px;
}

.hdr-logo-box {
  display: flex;
  flex-direction: column;
  justify-content: center;
  padding: 8px 22px;
  border-right: 1px solid rgba(0,0,0,0.10);
  gap: 2px;
  text-decoration: none;
}

.hdr-logo-text {
  font-family: var(--font-title);
  font-size: 22px;
  font-weight: 300;
  color: var(--header-text);
  letter-spacing: -0.02em;
  line-height: 1.1;
}

.hdr-logo-sub {
  font-family: var(--font);
  font-size: 9px;
  font-weight: 400;
  color: rgba(0,0,0,0.42);
  text-transform: uppercase;
  letter-spacing: 0.06em;
  line-height: 1.35;
}

.hdr-nav {
  display: flex;
  align-items: stretch;
}

.hdr-nav a {
  display: flex;
  align-items: center;
  padding: 0 18px;
  font-family: var(--font);
  font-size: 14px;
  font-weight: 500;
  color: var(--header-text);
  text-decoration: none;
  border-right: 1px solid rgba(0,0,0,0.10);
  transition: background 0.12s;
  white-space: nowrap;
}

.hdr-nav a:hover   { background: rgba(0,0,0,0.06); }
.hdr-nav a.current { background: rgba(0,0,0,0.10); font-weight: 600; }

.hdr-title {
  display: flex;
  align-items: center;
  padding: 0 20px;
  font-family: var(--font-title);
  font-size: 16px;
  font-weight: 400;
  color: var(--header-text);
  flex: 1;
}

/* ── Layout ── */
.docs-layout {
  display: flex;
  flex: 1;
  max-width: 1200px;
  margin: 0 auto;
  width: 100%;
  padding: 32px 24px;
  gap: 32px;
  align-items: flex-start;
}

/* ── TOC sidebar ── */
.docs-toc {
  width: 220px;
  flex-shrink: 0;
  position: sticky;
  top: 96px;
  max-height: calc(100vh - 112px);
  overflow-y: auto;
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  padding: 16px 0 20px;
  box-shadow: var(--shadow);
}

.docs-toc-heading {
  font-family: var(--font);
  font-size: 10px;
  font-weight: 500;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  color: var(--text-light);
  padding: 0 16px 8px;
}

.docs-toc .toc { list-style: none; padding: 0; margin: 0; }
.docs-toc .toc li { list-style: none; padding: 0; margin: 0; }

.docs-toc .toc a {
  display: block;
  font-family: var(--font);
  font-size: 12px;
  font-weight: 300;
  color: var(--text-medium);
  text-decoration: none;
  padding: 4px 16px;
  border-left: 2px solid transparent;
  transition: color 0.12s, background 0.12s;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.docs-toc .toc a:hover    { color: var(--accent); background: var(--accent-dim); }
.docs-toc .toc a.toc-active {
  color: var(--accent);
  border-left-color: var(--accent);
  background: var(--accent-dim);
  font-weight: 400;
}

.docs-toc .toc ul          { list-style: none; padding: 0; margin: 0; }
.docs-toc .toc ul a        { padding-left: 28px; font-size: 11px; }
.docs-toc .toc ul ul a     { padding-left: 42px; font-size: 10px; color: var(--text-light); }

/* ── Article ── */
.docs-content { flex: 1; min-width: 0; }

.docs-article {
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  padding: 40px 48px;
  box-shadow: var(--shadow-md);
}

.docs-article h1 {
  font-family: var(--font-title);
  font-size: 28px;
  font-weight: 400;
  color: var(--header-text);
  margin-bottom: 8px;
  line-height: 1.2;
}

.docs-article h2 {
  font-family: var(--font-title);
  font-size: 22px;
  font-weight: 400;
  color: var(--text-dark);
  margin-top: 40px;
  margin-bottom: 12px;
  padding-bottom: 8px;
  border-bottom: 2px solid var(--header-bg);
}

.docs-article h2:first-child { margin-top: 0; }

.docs-article h3 {
  font-family: var(--font-title);
  font-size: 20px;
  font-weight: 700;
  color: var(--accent);
  margin-top: 24px;
  margin-bottom: 8px;
}

.docs-article h4 {
  font-family: var(--font);
  font-size: 19px;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  color: var(--text-medium);
  margin-top: 20px;
  margin-bottom: 6px;
}

.docs-article p {
  font-size: 16px;
  font-weight: 300;
  line-height: 1.75;
  margin-bottom: 14px;
}

.docs-article ul,
.docs-article ol {
  font-size: 16px;
  font-weight: 300;
  line-height: 1.7;
  padding-left: 24px;
  margin-bottom: 14px;
}

.docs-article li          { margin-bottom: 4px; }
.docs-article li > ul,
.docs-article li > ol     { margin-bottom: 0; margin-top: 4px; }

.docs-article strong { font-weight: 500; }
.docs-article em     { font-style: italic; color: var(--text-medium); }

.docs-article hr {
  border: none;
  border-top: 1px solid var(--border);
  margin: 32px 0;
}

.docs-article code {
  font-family: var(--font);
  font-size: 14px;
  background: var(--accent-dim);
  color: var(--accent);
  border-radius: 3px;
  padding: 1px 5px;
}

.docs-article pre {
  background: var(--bg-alt);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  padding: 16px 18px;
  overflow-x: auto;
  margin-bottom: 16px;
}

.docs-article pre code {
  background: none;
  color: var(--text-dark);
  padding: 0;
  font-size: 14px;
  font-weight: 300;
  line-height: 1.6;
}

.docs-article table {
  width: 100%;
  border-collapse: collapse;
  font-size: 15px;
  margin-bottom: 20px;
}

.docs-article thead th {
  text-align: left;
  font-size: 12px;
  font-weight: 500;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  color: var(--text-light);
  padding: 8px 12px;
  border-bottom: 2px solid var(--border);
  background: var(--bg-alt);
}

.docs-article tbody td {
  padding: 8px 12px;
  border-bottom: 1px solid var(--border);
  vertical-align: top;
  line-height: 1.6;
}

.docs-article tbody tr:hover td { background: var(--accent-dim); }

.docs-article blockquote {
  border-left: 3px solid var(--header-bg);
  margin: 16px 0;
  padding: 10px 20px;
  background: var(--accent-dim);
  border-radius: 0 var(--radius) var(--radius) 0;
  font-size: 15px;
  color: var(--text-medium);
}

.docs-article blockquote p { margin-bottom: 0; font-size: 15px; }

.docs-article .headerlink {
  font-size: 14px;
  color: var(--text-light);
  text-decoration: none;
  margin-left: 6px;
  opacity: 0;
  transition: opacity 0.15s;
}

.docs-article h1:hover .headerlink,
.docs-article h2:hover .headerlink,
.docs-article h3:hover .headerlink,
.docs-article h4:hover .headerlink { opacity: 1; }

/* ── Footer ── */
.docs-footer {
  text-align: center;
  padding: 24px;
  font-size: 12px;
  color: var(--text-light);
  border-top: 1px solid var(--border);
  margin-top: 16px;
}

.docs-footer a { color: var(--accent); text-decoration: none; }
.docs-footer a:hover { text-decoration: underline; }

/* ── Index page ── */
.index-hero {
  text-align: center;
  padding: 60px 24px 40px;
}

.index-hero-title {
  font-family: var(--font-title);
  font-size: 44px;
  font-weight: 400;
  color: var(--header-text);
  letter-spacing: -0.02em;
  margin-bottom: 6px;
}

.index-hero-sub {
  font-family: var(--font);
  font-size: 13px;
  font-weight: 400;
  color: var(--text-medium);
  text-transform: uppercase;
  letter-spacing: 0.1em;
  margin-bottom: 40px;
}

.index-cards {
  display: flex;
  gap: 24px;
  justify-content: center;
  flex-wrap: wrap;
  max-width: 700px;
  margin: 0 auto;
}

.index-card {
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  padding: 32px 36px;
  text-decoration: none;
  flex: 1;
  min-width: 240px;
  max-width: 300px;
  box-shadow: var(--shadow);
  transition: box-shadow 0.15s, transform 0.15s, border-color 0.15s;
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 10px;
}

.index-card:hover {
  box-shadow: var(--shadow-md);
  transform: translateY(-2px);
  border-color: var(--accent);
}

.index-card-icon { font-size: 32px; }

.index-card-title {
  font-family: var(--font-title);
  font-size: 20px;
  font-weight: 400;
  color: var(--text-dark);
}

.index-card-desc {
  font-family: var(--font);
  font-size: 14px;
  font-weight: 300;
  color: var(--text-medium);
  line-height: 1.6;
}

/* ── Responsive ── */
@media (max-width: 860px) {
  .docs-toc      { display: none; }
  .docs-article  { padding: 24px 20px; }
  .docs-layout   { padding: 16px 12px; }
}
"""

# ── JS (active-TOC scroll tracking) ──────────────────────────────────────────

JS = """
(function () {
  "use strict";
  var headings = Array.from(document.querySelectorAll(
    ".docs-article h1, .docs-article h2, .docs-article h3"
  ));
  var tocLinks = Array.from(document.querySelectorAll(".docs-toc a"));
  if (!headings.length || !tocLinks.length) { return; }

  function setActive(id) {
    tocLinks.forEach(function (a) {
      a.classList.toggle("toc-active", a.getAttribute("href") === "#" + id);
    });
  }

  window.addEventListener("scroll", function () {
    var scrollY = window.scrollY;
    var active = headings[0];
    headings.forEach(function (h) {
      if (h.offsetTop - 100 <= scrollY) { active = h; }
    });
    if (active) { setActive(active.id); }
  }, { passive: true });
})();
"""

# ── HTML template ─────────────────────────────────────────────────────────────

def _guide_page(title, content_html, toc_html, nav_links):
    nav_html = "\n".join(
        f'        <a href="{href}"{" class=\"current\"" if current else ""}>{label}</a>'
        for href, label, current in nav_links
    )
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>{title} — TheaterGWD</title>
  <style>{CSS}</style>
</head>
<body>

  <header class="docs-header">
    <a class="hdr-logo-box" href="index.html">
      <span class="hdr-logo-text">TheaterGWD</span>
      <span class="hdr-logo-sub">Documentation</span>
    </a>
    <nav class="hdr-nav">
{nav_html}
    </nav>
    <div class="hdr-title">📚 {title}</div>
  </header>

  <div class="docs-layout">
    <nav class="docs-toc">
      <div class="docs-toc-heading">Contents</div>
      {toc_html}
    </nav>
    <main class="docs-content">
      <article class="docs-article">
        {content_html}
      </article>
    </main>
  </div>

  <footer class="docs-footer">
    TheaterGWD &mdash; <a href="https://github.com/halfsohappy/TheaterGWD" target="_blank" rel="noopener">GitHub</a>
  </footer>

  <script>{JS}</script>
</body>
</html>
"""


def _index_page(guides):
    cards = "\n".join(
        f"""    <a class="index-card" href="{href}">
      <div class="index-card-icon">{icon}</div>
      <div class="index-card-title">{title}</div>
      <div class="index-card-desc">{desc}</div>
    </a>"""
        for href, icon, title, desc in guides
    )
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>TheaterGWD Documentation</title>
  <style>{CSS}</style>
</head>
<body>

  <header class="docs-header">
    <a class="hdr-logo-box" href="index.html">
      <span class="hdr-logo-text">TheaterGWD</span>
      <span class="hdr-logo-sub">Documentation</span>
    </a>
  </header>

  <div class="index-hero">
    <div class="index-hero-title">Gooey</div>
    <div class="index-hero-sub">annieData · Documentation</div>
    <div class="index-cards">
{cards}
    </div>
  </div>

  <footer class="docs-footer">
    TheaterGWD &mdash; <a href="https://github.com/halfsohappy/TheaterGWD" target="_blank" rel="noopener">GitHub</a>
  </footer>

</body>
</html>
"""


# ── Build ─────────────────────────────────────────────────────────────────────

def build():
    os.makedirs(OUT_DIR, exist_ok=True)

    built = []
    for src_name, out_name, title in GUIDES:
        src_path = os.path.join(DOCS_DIR, src_name)
        if not os.path.exists(src_path):
            print(f"  skip  {src_name} (not found)")
            continue
        with open(src_path, encoding="utf-8") as fh:
            raw = fh.read()
        md = md_lib.Markdown(extensions=MD_EXTENSIONS, extension_configs=MD_EXT_CONFIG)
        content_html = md.convert(raw)
        toc_html = getattr(md, "toc", "")
        built.append((out_name, title, content_html, toc_html))

    # Build nav links list
    all_guides = [(out_name, title) for out_name, title, _, _ in built]

    for out_name, title, content_html, toc_html in built:
        nav_links = [
            ("index.html", "🏠 Home", False),
        ] + [
            (href, label, href == out_name)
            for href, label in all_guides
        ]
        html = _guide_page(title, content_html, toc_html, nav_links)
        out_path = os.path.join(OUT_DIR, out_name)
        with open(out_path, "w", encoding="utf-8") as fh:
            fh.write(html)
        print(f"  wrote {out_path}")

    # Index page
    index_guides = [
        (
            "user-guide.html",
            "📖",
            "User Guide",
            "Practical setup, key concepts, and the complete command reference for theater technicians.",
        ),
        (
            "technical-guide.html",
            "⚙️",
            "Technical Guide",
            "Firmware architecture, module map, concurrency strategy, and extending the codebase.",
        ),
    ]
    index_html = _index_page(index_guides)
    index_path = os.path.join(OUT_DIR, "index.html")
    with open(index_path, "w", encoding="utf-8") as fh:
        fh.write(index_html)
    print(f"  wrote {index_path}")
    print(f"\nDone — {len(built) + 1} file(s) in {OUT_DIR}/")


if __name__ == "__main__":
    build()
