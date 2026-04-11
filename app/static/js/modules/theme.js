/* ── Theme toggle ── */

import { $ } from "./state.js";

const THEME_KEY = "gooey-theme";
const btn = $("#btnThemeToggle");
const icon = btn ? btn.querySelector("i") : null;

function isDark() {
  return document.documentElement.classList.contains("dark");
}

function applyTheme(dark) {
  const html = document.documentElement;
  html.classList.add("transitioning");
  if (dark) html.classList.add("dark");
  else html.classList.remove("dark");
  if (icon) icon.className = dark ? "bi bi-sun-fill" : "bi bi-moon-fill";
  setTimeout(function () { html.classList.remove("transitioning"); }, 250);
}

// Sync button state with class set by inline <script> in <head>
applyTheme(isDark());

if (btn) {
  btn.addEventListener("click", function () {
    const dark = !isDark();
    try { localStorage.setItem(THEME_KEY, dark ? "dark" : "light"); } catch (e) {}
    applyTheme(dark);
  });
}

// Follow system preference when user hasn't manually chosen
try {
  window.matchMedia("(prefers-color-scheme: dark)").addEventListener("change", function (e) {
    if (!localStorage.getItem(THEME_KEY)) applyTheme(e.matches);
  });
} catch (e) {}
