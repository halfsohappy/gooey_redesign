/* ── Column Picker — show/hide message table columns with localStorage ── */

import { $, $$ } from './state.js';

/* ═══════════════════════════════════════════
   COLUMN PICKER (message table)
   ═══════════════════════════════════════════ */

const COL_PREF_KEY = "gooey_col_prefs";

export function loadColPrefs() {
  try {
    const raw = localStorage.getItem(COL_PREF_KEY);
    return raw ? JSON.parse(raw) : null;
  } catch (e) { return null; }
}

function saveColPrefs() {
  const prefs = {};
  $$('#colPickerMenu input[data-col]').forEach(function (cb) {
    prefs[cb.dataset.col] = cb.checked;
  });
  try { localStorage.setItem(COL_PREF_KEY, JSON.stringify(prefs)); } catch (e) {}
}

export function applyColVisibility() {
  const prefs = loadColPrefs();
  if (!prefs) return;
  let hiddenCount = 0;
  Object.keys(prefs).forEach(function (col) {
    const visible = prefs[col];
    if (!visible) hiddenCount++;
    $$('[data-col="' + col + '"]').forEach(function (el) {
      el.style.display = visible ? "" : "none";
    });
  });
  /* Update caret indicator when columns are hidden */
  const btn = $("#btnColPicker");
  if (btn) {
    btn.classList.toggle("has-hidden", hiddenCount > 0);
    btn.textContent = hiddenCount > 0 ? "▾ " + hiddenCount : "▾";
    btn.title = hiddenCount > 0 ? hiddenCount + " column(s) hidden" : "Choose visible columns";
  }
}

/* Init column picker checkboxes from localStorage */
const savedPrefs = loadColPrefs();
if (savedPrefs) {
  $$('#colPickerMenu input[data-col]').forEach(function (cb) {
    if (savedPrefs[cb.dataset.col] !== undefined) {
      cb.checked = savedPrefs[cb.dataset.col];
    }
  });
}
applyColVisibility();

/* Toggle column picker menu */
const btnColPicker = $("#btnColPicker");
const colPickerMenu = $("#colPickerMenu");
if (btnColPicker && colPickerMenu) {
  btnColPicker.addEventListener("click", function (e) {
    e.stopPropagation();
    colPickerMenu.classList.toggle("hidden");
  });
  colPickerMenu.querySelectorAll("input[type='checkbox']").forEach(function (cb) {
    cb.addEventListener("change", function () {
      saveColPrefs();
      applyColVisibility();
    });
  });
  document.addEventListener("click", function (e) {
    if (!e.target.closest(".col-picker-wrap")) {
      colPickerMenu.classList.add("hidden");
    }
  });
}
