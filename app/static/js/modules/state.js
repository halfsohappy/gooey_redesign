/* ── Shared state & helpers ── */

export const MAX_LOG_ENTRIES = 500;

/* ── Socket.IO ── */
export const socket = io({ transports: ["websocket", "polling"] });

/* ── DOM helpers ── */
export const $ = function (sel) { return document.querySelector(sel); };
export const $$ = function (sel) { return document.querySelectorAll(sel); };

/* ── Shared mutable state ── */
export const devices = {};
export const activeDevice = { id: "" };

/* ── withLoading helper ── */
export function withLoading(btn, fn) {
  if (!btn) return fn();
  btn.disabled = true;
  btn.classList.add("loading");
  return Promise.resolve(fn()).finally(function () {
    btn.disabled = false;
    btn.classList.remove("loading");
  });
}
