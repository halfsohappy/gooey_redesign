/* ── Toast history + notification system ── */

import { $ } from "./state.js";

const _toastHistory = [];

export function renderNotifHistory() {
  const container = $("#notifHistory");
  if (!container) return;
  container.innerHTML = "";
  if (_toastHistory.length === 0) {
    container.innerHTML = '<div class="notif-history-item"><span class="notif-history-msg" style="color:var(--text-light)">No notifications yet</span></div>';
    return;
  }
  _toastHistory.slice().reverse().forEach(function (item) {
    const div = document.createElement("div");
    div.className = "notif-history-item";
    div.innerHTML = '<span class="notif-history-time">' + item.time + '</span><span class="notif-history-msg notif-type-' + item.type + '">' + item.msg + '</span>';
    container.appendChild(div);
  });
}

// Render on load
renderNotifHistory();

/* ── showToast (public alias: toast) ── */
export function showToast(msg, type) {
  type = type || "info";
  /* Add to history (max 100) */
  const now = new Date();
  const timeStr = now.getHours().toString().padStart(2, "0") + ":" + now.getMinutes().toString().padStart(2, "0") + ":" + now.getSeconds().toString().padStart(2, "0");
  _toastHistory.push({ msg: msg, type: type, time: timeStr });
  if (_toastHistory.length > 100) _toastHistory.shift();
  renderNotifHistory();
  /* Update + flash latest notification in header */
  const latest = $("#notifLatest");
  if (latest) {
    latest.textContent = msg;
    latest.classList.remove("notif-flash");
    void latest.offsetWidth; // force reflow to restart animation
    latest.classList.add("notif-flash");
  }
}

/* Backward-compatible alias */
export function toast(msg, type) { showToast(msg, type); }

/* ── Confirm modal ── */
export function showConfirm(title, body, onConfirm, okLabel, danger) {
  if (okLabel === undefined) okLabel = "Confirm";
  if (danger === undefined) danger = true;
  const modal = document.getElementById("confirmModal");
  if (!modal) { if (onConfirm) onConfirm(); return; }
  const box = modal.querySelector(".modal-box");
  const isVerbose = title.indexOf("Verbose") !== -1;
  if (box) {
    box.classList.toggle("modal-box--verbose", isVerbose);
  }
  document.getElementById("confirmTitle").textContent = title;
  document.getElementById("confirmBody").textContent = body;
  const okBtn = document.getElementById("confirmOk");
  okBtn.textContent = okLabel;
  okBtn.className = "btn " + (danger ? "btn-danger" : "btn-primary");
  modal.classList.remove("hidden");
  const cancel = document.getElementById("confirmCancel");
  cancel.focus();
  const cleanup = function () { modal.classList.add("hidden"); };
  okBtn.onclick = function () { cleanup(); onConfirm(); };
  cancel.onclick = cleanup;
  modal.onclick = function (e) { if (e.target === modal) cleanup(); };
}

/* ── toggleHelp for inline help boxes (exposed globally for onclick attrs) ── */
window.toggleHelp = function (id) {
  const el = document.getElementById(id);
  if (el) el.classList.toggle("hidden");
};
