/* ── Ori manager — orientation table, bulk actions, recording UI ── */

import { $ } from './state.js';
import { toast } from './toast.js';
import { sendCmd, addr } from './command.js';
import { getActiveDev } from './device-manager.js';
import { esc } from './feed.js';
import { renderSceneTable, highlightBulkMatches, updatePatternHint, oscPatternMatch } from './scene-manager.js';
import { renderMsgTable } from './message-manager.js';
import { refreshAllDropdowns } from './dropdown-coordinator.js';

/* ── Callback for expandIp (still lives in app.js IIFE) ── */
let _expandIp = function (val) { return val; };

export function setOriManagerCallbacks({ expandIp }) {
  if (expandIp) _expandIp = expandIp;
}

/* ═══════════════════════════════════════════
   MESSAGE BULK ACTION
   ═══════════════════════════════════════════ */

(function () {
  const inp    = $("#msgBulkPattern");
  const sel    = $("#msgBulkAction");
  const setVal = $("#msgBulkSetVal");
  const btn    = $("#btnMsgBulk");
  const hint   = $("#msgBulkHint");
  if (!inp || !btn) return;

  const MSG_SET = {
    "set-ip":    { key: "ip",    ph: "e.g. 192.168.1.50" },
    "set-port":  { key: "port",  ph: "e.g. 9000" },
    "set-adr":   { key: "adr",   ph: "e.g. /sensor/x" },
    "set-scene": { key: "scene", ph: "scene name" }
  };

  sel.addEventListener("change", function () {
    const info = MSG_SET[sel.value];
    if (info) { setVal.style.display = "inline-block"; setVal.placeholder = info.ph; setVal.value = ""; }
    else { setVal.style.display = "none"; setVal.value = ""; }
  });

  inp.addEventListener("input", function () {
    const dev = getActiveDev();
    updatePatternHint(inp, hint, dev ? dev.messages : null, btn);
    highlightBulkMatches(inp.value.trim(), ".msg-data-row", "msgName");
  });

  btn.addEventListener("click", function () {
    const pattern = inp.value.trim();
    if (!pattern) { toast("Enter a pattern", "warn"); return; }
    const act  = sel.value;
    const info = MSG_SET[act];

    if (info) {
      let val = setVal.value.trim();
      if (!val) { toast("Enter a value to set", "warn"); return; }
      if (info.key === "ip") val = _expandIp(val);
      const dev = getActiveDev();
      if (!dev) return;
      const matches = Object.keys(dev.messages).filter(function (n) { return oscPatternMatch(pattern, n); });
      if (!matches.length) { toast("No matches for pattern", "warn"); return; }
      const promises = matches.map(function (name) {
        return sendCmd(addr("/annieData/{device}/msg/{name}", name), info.key + ":" + val);
      });
      Promise.all(promises).then(function () {
        toast("Set " + info.key + " on " + matches.length + " message(s)", "success");
        matches.forEach(function (name) { if (dev.messages[name]) dev.messages[name][info.key] = val; });
        renderMsgTable();
      });
    } else {
      if (act === "delete" && !confirm("Delete all messages matching '" + pattern + "'?")) return;
      const template = "/annieData/{device}/msg/{name}/" + act;
      sendCmd(addr(template, pattern), null).then(function (res) {
        if (res.status === "ok") toast("Bulk " + act + ": " + pattern, "success");
      });
    }
  });
}());

/* ═══════════════════════════════════════════
   SCENE BULK ACTION
   ═══════════════════════════════════════════ */

(function () {
  const inp    = $("#sceneBulkPattern");
  const sel    = $("#sceneBulkAction");
  const setVal = $("#sceneBulkSetVal");
  const btn    = $("#btnSceneBulk");
  const hint   = $("#sceneBulkHint");
  if (!inp || !btn) return;

  const SCENE_SET = {
    "set-ip":     { key: "ip",     ph: "e.g. 192.168.1.50" },
    "set-port":   { key: "port",   ph: "e.g. 9000" },
    "set-adr":    { key: "adr",    ph: "e.g. /scene/addr" },
    "set-period": { key: "period", ph: "e.g. 50" }
  };

  sel.addEventListener("change", function () {
    const info = SCENE_SET[sel.value];
    if (info) { setVal.style.display = "inline-block"; setVal.placeholder = info.ph; setVal.value = ""; }
    else { setVal.style.display = "none"; setVal.value = ""; }
  });

  inp.addEventListener("input", function () {
    const dev = getActiveDev();
    updatePatternHint(inp, hint, dev ? dev.scenes : null, btn);
    highlightBulkMatches(inp.value.trim(), ".scene-data-row", "sceneName");
  });

  btn.addEventListener("click", function () {
    const pattern = inp.value.trim();
    if (!pattern) { toast("Enter a pattern", "warn"); return; }
    const act  = sel.value;
    const info = SCENE_SET[act];

    if (info) {
      let val = setVal.value.trim();
      if (!val) { toast("Enter a value to set", "warn"); return; }
      if (info.key === "ip") val = _expandIp(val);
      const dev = getActiveDev();
      if (!dev) return;
      const matches = Object.keys(dev.scenes).filter(function (n) { return oscPatternMatch(pattern, n); });
      if (!matches.length) { toast("No matches for pattern", "warn"); return; }
      let promises;
      if (act === "set-period") {
        promises = matches.map(function (name) {
          return sendCmd(addr("/annieData/{device}/scene/{name}/period", name), '"' + val + '"');
        });
      } else {
        promises = matches.map(function (name) {
          return sendCmd(addr("/annieData/{device}/scene/{name}", name), info.key + ":" + val);
        });
      }
      Promise.all(promises).then(function () {
        toast("Set " + info.key + " on " + matches.length + " scene(s)", "success");
        matches.forEach(function (name) { if (dev.scenes[name]) dev.scenes[name][info.key] = val; });
        renderSceneTable();
      });
    } else {
      if (act === "delete" && !confirm("Delete all scenes matching '" + pattern + "'?")) return;
      const template = "/annieData/{device}/scene/{name}/" + act;
      sendCmd(addr(template, pattern), null).then(function (res) {
        if (res.status === "ok") toast("Bulk " + act + ": " + pattern, "success");
      });
    }
  });
}());

/* ═══════════════════════════════════════════
   ORI EXPLAINER DISMISS
   ═══════════════════════════════════════════ */

(function () {
  const card = $("#oriExplainerCard");
  const btn  = $("#oriExplainerDismiss");
  if (!card || !btn) return;
  if (localStorage.getItem("oriExplainerDismissed") === "1") {
    card.style.display = "none";
  }
  btn.addEventListener("click", function () {
    card.style.display = "none";
    localStorage.setItem("oriExplainerDismissed", "1");
  });
}());

/* ═══════════════════════════════════════════
   ORI REGISTRATION (immediate send)
   Sends /ori/register/{name} with color directly to the device.
   No local pending list — same pattern as messages and scenes.
   ═══════════════════════════════════════════ */

(function () {
  const nameInput = $("#oriName");
  const btnReg    = $("#btnRegisterOri");

  function doRegister() {
    const name = (nameInput ? nameInput.value : "").trim();
    if (!name) { toast("Ori name required", "error"); return; }
    if (!getActiveDev()) { toast("Select a device first", "error"); return; }
    sendCmd(addr("/annieData/{device}/ori/register/" + name), null).then(function (res) {
      if (res && res.status === "ok") {
        toast("Registered: " + name, "success");
        const dev = getActiveDev();
        if (dev && !dev.oris[name]) {
          dev.oris[name] = { samples: 0, active: false };
          renderOriTable();
          refreshAllDropdowns();
        }
        sendCmd(addr("/annieData/{device}/ori/list"), null);
      } else {
        toast("Register failed: " + (res && res.message ? res.message : "unknown"), "error");
      }
    });
  }

  if (btnReg) btnReg.addEventListener("click", doRegister);
  if (nameInput) nameInput.addEventListener("keydown", function (e) {
    if (e.key === "Enter") { e.preventDefault(); doRegister(); }
  });
}());

/* ═══════════════════════════════════════════
   ORI RECORDING UI
   Start/Stop/Cancel recording session with live sample counter.
   Polls /ori/record/status every 500ms while active.
   ═══════════════════════════════════════════ */

(function () {
  const nameInput = $("#oriName");
  const recBtn    = $("#btnRecordStart");
  let _recording  = false;

  function setRecording(active) {
    _recording = active;
    if (!recBtn) return;
    if (active) {
      recBtn.textContent = "■ Stop Recording";
      recBtn.classList.remove("btn-primary");
      recBtn.classList.add("btn-stop");
      if (nameInput) nameInput.disabled = true;
    } else {
      recBtn.textContent = "▶ Start Recording";
      recBtn.classList.remove("btn-stop");
      recBtn.classList.add("btn-primary");
      if (nameInput) nameInput.disabled = false;
    }
  }

  if (recBtn) recBtn.addEventListener("click", function () {
    if (!getActiveDev()) { toast("Select a device first", "error"); return; }
    if (!_recording) {
      const name = (nameInput ? nameInput.value : "").trim();
      if (!name) { toast("Ori name required", "error"); return; }
      sendCmd(addr("/annieData/{device}/ori/record/start/" + name), null).then(function (res) {
        if (res && res.status === "ok") {
          toast("Recording: " + name, "info");
          setRecording(true);
        } else {
          toast("Start failed: " + (res && res.message ? res.message : ""), "error");
        }
      });
    } else {
      sendCmd(addr("/annieData/{device}/ori/record/stop"), null).then(function (res) {
        setRecording(false);
        if (res && res.status === "ok") {
          toast("Saved: " + (res.message || ""), "success");
        }
        sendCmd(addr("/annieData/{device}/ori/list"), null);
      });
    }
  });
}());

/* ═══════════════════════════════════════════
   ORI TABLE
   ═══════════════════════════════════════════ */

export function renderOriTable() {
  const dev   = getActiveDev();
  const tbody = $("#oriTableBody");
  if (!tbody) return;
  tbody.innerHTML = "";

  const devOriNames = dev ? Object.keys(dev.oris) : [];

  if (devOriNames.length === 0) {
    tbody.innerHTML = '<tr><td colspan="3"><div class="empty-state empty-state-inline">No orientations tracked yet — Query device or register one below.</div></td></tr>';
    return;
  }

  devOriNames.forEach(function (name) {
    const o = dev.oris[name];
    const isUnsampled = (o.samples === 0);
    const isActive    = !!o.active;

    /* Status pill */
    let statusHtml;
    if (isActive) {
      statusHtml = '<span class="ori-pill ori-pill-active"><span class="ori-dot"></span>ACTIVE</span>';
    } else if (isUnsampled) {
      statusHtml = '<span class="ori-pill ori-pill-pending">PENDING</span>';
    } else {
      statusHtml = '<span class="ori-pill ori-pill-idle">—</span>';
    }

    const tr = document.createElement("tr");
    tr.className = "ori-data-row" + (isActive ? " active" : "") + (isUnsampled ? " pending" : "");
    tr.dataset.oriName = name;
    tr.style.cursor = "pointer";
    tr.innerHTML =
      '<td class="ori-status-cell">' + statusHtml + '</td>' +
      '<td class="ori-name-cell">' + esc(name) + '</td>';

    tr.addEventListener("click", function () {
      const oriNameEl = $("#oriName");
      if (oriNameEl) oriNameEl.value = name;
    });
    tbody.appendChild(tr);
  });
}

export function oriAction(act, name) {
  switch (act) {
    case "reset":
      sendCmd(addr("/annieData/{device}/ori/reset/" + name), null).then(function (res) {
        if (res && res.status === "ok") {
          toast("Samples cleared: " + name, "success");
          const dev = getActiveDev();
          if (dev && dev.oris[name]) {
            dev.oris[name].samples = 0;
            dev.oris[name].useAxis = false;
            renderOriTable();
          }
        }
      });
      break;
    case "delete":
      sendCmd(addr("/annieData/{device}/ori/delete/" + name), null).then(function (res) {
        if (res.status === "ok") {
          toast("Deleted: " + name, "success");
          const dev = getActiveDev();
          if (dev) { delete dev.oris[name]; renderOriTable(); refreshAllDropdowns(); }
        }
      });
      break;
  }
}

export function showOriDetails(text) {
  const card    = $("#oriDetailsCard");
  const content = $("#oriDetailsContent");
  if (!card || !content) return;
  card.style.display = "block";
  /* Parse v2 format:
     "name: samples=N axis=(x,y,z) tol=10.0deg q0=(...) ... color=(r,g,b) (ACTIVE)"
     or unsampled: "name: samples=0 (unsampled) color=(r,g,b)" */
  let html = "";
  const nm = text.match(/^(\S+):/);
  if (nm) html += '<div class="ori-detail-row"><span class="ori-detail-label">Name</span><span>' + esc(nm[1]) + '</span></div>';
  const sm = text.match(/samples=(\d+)/);
  if (sm) html += '<div class="ori-detail-row"><span class="ori-detail-label">Samples</span><span>' + esc(sm[1]) + '</span></div>';
  const axM = text.match(/axis=\(([^)]+)\)/);
  if (axM) html += '<div class="ori-detail-row"><span class="ori-detail-label">Axis</span><span class="cell-mono">(' + esc(axM[1]) + ')</span></div>';
  if (/axis=fullQ/.test(text)) html += '<div class="ori-detail-row"><span class="ori-detail-label">Axis</span><span class="cell-mono">full quaternion</span></div>';
  const tolM = text.match(/tol=([\d.]+)deg/);
  if (tolM) html += '<div class="ori-detail-row"><span class="ori-detail-label">Tolerance</span><span>' + esc(tolM[1]) + '°</span></div>';
  /* Show first sample quaternion if present */
  const q0 = text.match(/q0=\(([^)]+)\)/);
  if (q0) html += '<div class="ori-detail-row"><span class="ori-detail-label">Sample q0</span><span class="cell-mono">(' + esc(q0[1]) + ')</span></div>';
  if (/\(ACTIVE\)/i.test(text)) html += '<div class="ori-detail-row"><span class="ori-detail-label">Status</span><span class="ori-badge ori-badge-active">Active</span></div>';
  if (!html) html = '<p class="cell-mono">' + esc(text) + '</p>';
  content.innerHTML = html;
  /* Switch to ori tab */
  $(".nav-btn[data-section='ori']").click();
}
