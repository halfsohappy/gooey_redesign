/* ── Panel resize & view toggling ── */

import { $ } from "./state.js";
import { socket } from "./state.js";
import { renderNotifHistory, clearNotifHistory } from "./toast.js";

const activeViews = {};
const _viewOrder = ["feed", "serial", "notifications", "reference"];
const _viewElements = {
  feed:          "viewFeed",
  serial:        "viewSerial",
  notifications: "viewNotifications",
  reference:     "viewReference"
};
const _viewToggleButtons = {
  feed:          "btnFeedToggle",
  serial:        "btnSerialToggle",
  reference:     "btnRefToggle",
  notifications: "btnNotifToggle"
};
const VSIZE_KEY = "gooey_panel_vsizes";
const _viewFlexSizes = { feed: 1, serial: 1, notifications: 1, reference: 1 };
try {
  const _savedVSizes = JSON.parse(localStorage.getItem(VSIZE_KEY) || "{}");
  Object.keys(_savedVSizes).forEach(function (k) {
    if (_viewFlexSizes.hasOwnProperty(k) && _savedVSizes[k] > 0) {
      _viewFlexSizes[k] = _savedVSizes[k];
    }
  });
} catch (e) {}

function updateVResizeHandles() {
  /* Hide all handles first */
  document.querySelectorAll(".panel-vresize-handle").forEach(function (h) {
    h.style.display = "none";
    h.dataset.activeAbove = "";
    h.dataset.activeBelow = "";
  });
  /* Walk _viewOrder; whenever two consecutive active views are found, show
     the physical handle that sits between them in the DOM (which may span
     several inactive views) and record which views to actually resize. */
  let prevActive = null;
  for (let i = 0; i < _viewOrder.length; i++) {
    const k = _viewOrder[i];
    if (!activeViews[k]) continue;
    if (prevActive !== null) {
      /* Physical handles exist only at adjacent DOM positions.
         Scan inward from k to find the first existing handle above it. */
      let handle = null;
      let ki = i;
      while (ki > 0 && !handle) {
        handle = document.getElementById("vresize-" + _viewOrder[ki - 1] + "-" + _viewOrder[ki]);
        ki--;
      }
      if (handle) {
        handle.style.display = "block";
        handle.dataset.activeAbove = prevActive;
        handle.dataset.activeBelow = k;
      }
    }
    prevActive = k;
  }
}

export function updatePanelLayout() {
  const panel = $("#panelRight");
  let activeCount = 0;
  _viewOrder.forEach(function (k) { if (activeViews[k]) activeCount++; });

  if (activeCount === 0) {
    panel.classList.add("panel-hidden");
  } else {
    panel.classList.remove("panel-hidden");
  }

  /* When a newly-activated view has no prior size, give it the average of active peers */
  _viewOrder.forEach(function (k) {
    if (activeViews[k] && _viewFlexSizes[k] === 1) {
      let sum = 0, cnt = 0;
      _viewOrder.forEach(function (j) { if (activeViews[j] && j !== k) { sum += _viewFlexSizes[j]; cnt++; } });
      if (cnt > 0) _viewFlexSizes[k] = sum / cnt;
    }
  });

  _viewOrder.forEach(function (k) {
    const el = $("#" + _viewElements[k]);
    if (!el) return;
    if (activeViews[k]) {
      el.style.display = "flex";
      el.style.flex = _viewFlexSizes[k] + " 1 0";
      el.style.minHeight = "80px";
      el.style.height = "";
    } else {
      el.style.display = "none";
      el.style.flex = "";
      el.style.minHeight = "";
    }
  });

  updateVResizeHandles();

  /* Update toggle button active states */
  Object.keys(_viewToggleButtons).forEach(function (k) {
    const btn = $("#" + _viewToggleButtons[k]);
    if (btn) {
      if (activeViews[k]) btn.classList.add("panel-active");
      else btn.classList.remove("panel-active");
    }
  });
}

export function toggleView(name) {
  if (activeViews[name]) {
    delete activeViews[name];
  } else {
    activeViews[name] = true;
    /* Auto-refresh serial ports when showing serial */
    if (name === "serial") {
      socket.emit("serial_list_ports");
    }
  }
  updatePanelLayout();
}

/* ── Vertical panel resize ── */
(function () {
  const MIN_H = 80;
  document.querySelectorAll(".panel-vresize-handle").forEach(function (handle) {
    handle.addEventListener("mousedown", function (e) {
      e.preventDefault();
      const above = handle.dataset.activeAbove || handle.dataset.above;
      const below = handle.dataset.activeBelow || handle.dataset.below;
      const elA = $("#" + _viewElements[above]);
      const elB = $("#" + _viewElements[below]);
      if (!elA || !elB) return;
      const startY = e.clientY;
      const startHA = elA.offsetHeight;
      const startHB = elB.offsetHeight;
      const totalH = startHA + startHB;
      const totalFlex = _viewFlexSizes[above] + _viewFlexSizes[below];

      function onMove(ev) {
        const delta = ev.clientY - startY;
        const newHA = Math.max(MIN_H, Math.min(totalH - MIN_H, startHA + delta));
        const frac = newHA / totalH;
        _viewFlexSizes[above] = frac * totalFlex;
        _viewFlexSizes[below] = (1 - frac) * totalFlex;
        elA.style.flex = _viewFlexSizes[above] + " 1 0";
        elB.style.flex = _viewFlexSizes[below] + " 1 0";
      }

      function onUp() {
        document.removeEventListener("mousemove", onMove);
        document.removeEventListener("mouseup", onUp);
        try { localStorage.setItem(VSIZE_KEY, JSON.stringify(_viewFlexSizes)); } catch (e2) {}
      }

      document.addEventListener("mousemove", onMove);
      document.addEventListener("mouseup", onUp);
    });
  });
}());

/* Feed toggle button */
$("#btnFeedToggle").addEventListener("click", function () { toggleView("feed"); });

/* Serial toggle button */
$("#btnSerialToggle").addEventListener("click", function () { toggleView("serial"); });

/* Reference toggle button */
$("#btnRefToggle").addEventListener("click", function () { toggleView("reference"); });

/* Notification history toggle — bell button */
$("#btnNotifToggle").addEventListener("click", function () {
  toggleView("notifications");
});

/* Reference collapse is handled natively by daisyUI collapse <input type="checkbox"> */

/* Click on latest notif text also toggles notifications panel */
const notifLatestEl = $("#notifLatest");
if (notifLatestEl) {
  notifLatestEl.addEventListener("click", function () {
    toggleView("notifications");
  });
}

/* Clear button inside notifications panel */
const btnNotifClearPanel = $("#btnNotifClear");
if (btnNotifClearPanel) {
  btnNotifClearPanel.addEventListener("click", function () {
    clearNotifHistory();
    const latest = $("#notifLatest");
    if (latest) latest.textContent = "No notifications";
  });
}

/* ═══════════════════════════════════════════
   HORIZONTAL PANEL RESIZE
   ═══════════════════════════════════════════ */
(function () {
  const handle = $("#panelResizeHandle");
  const panel  = $("#panelRight");
  if (!handle || !panel) return;

  const STORAGE_KEY = "gooey_panel_width";
  const MIN_PX = 200;

  /* Panel width is locked to 50% via CSS — clear any stale saved width */
  try { localStorage.removeItem(STORAGE_KEY); } catch (e) {}

  handle.addEventListener("mousedown", function (e) {
    e.preventDefault();
    const startX = e.clientX;
    const startW = panel.offsetWidth;

    function onMove(ev) {
      const delta = startX - ev.clientX;
      let newW = Math.max(MIN_PX, startW + delta);
      const maxW = window.innerWidth * 0.7;
      newW = Math.min(newW, maxW);
      panel.style.width = newW + "px";
    }

    function onUp() {
      document.removeEventListener("mousemove", onMove);
      document.removeEventListener("mouseup", onUp);
      try { localStorage.setItem(STORAGE_KEY, panel.offsetWidth); } catch (e) {}
    }

    document.addEventListener("mousemove", onMove);
    document.addEventListener("mouseup", onUp);
  });
}());

export { activeViews };
