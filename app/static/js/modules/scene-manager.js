/* ── Scene manager — scene table, expand/collapse, pattern matching ── */

import { devices, activeDevice, $, $$ } from './state.js';
import { toast, showConfirm } from './toast.js';
import { sendCmd, addr, sendFlush } from './command.js';
import { getActiveDev, saveDevicesToStorage } from './device-manager.js';
import { esc } from './feed.js';
import { initGatePicker } from './message-manager.js';

/* ── Scene gate picker instance (uses shared initGatePicker factory) ── */

export const sceneGatePicker = initGatePicker(
  "sceneGateSource", "sceneGateOri", "sceneGateMode",
  "sceneGateLo", "sceneGateHi", "sceneGateHint",
  "sceneGateOriGroup", "sceneGateLoGroup", "sceneGateHiGroup",
  { scene: true }
);

/* ── Render scene table ── */

export function renderSceneTable() {
  const dev = getActiveDev();
  const tbody = $("#sceneTableBody");
  tbody.innerHTML = "";
  if (!dev || Object.keys(dev.scenes).length === 0) {
    tbody.innerHTML = '<tr><td colspan="4"><div class="empty-state"><div class="empty-icon">○</div><div class="empty-text">No scenes tracked yet.</div><div class="empty-sub">Click the device tab → Query to load from device, or create one below.</div></div></td></tr>';
    return;
  }
  Object.keys(dev.scenes).forEach(function (name) {
    const p = dev.scenes[name];
    let msgsStr = p.msgs || "";
    if (typeof msgsStr === "string") msgsStr = msgsStr.replace(/\+/g, ", ");
    const isRunning = !!p.running;
    const rowState  = isRunning ? "run" : "stop";
    const expState  = isRunning ? "run-exp" : "stop-exp";
    const pillClass = isRunning ? "scene-pill-run" : "scene-pill-stop";
    const pillLabel = isRunning ? "ACTIVE" : "STOPPED";
    const overrides = (p.override || "—").replace(/\+/g, ", ");
    const adrMode   = esc(p.adrMode || p.adrmode || p.adr_mode || "fallback");
    const ip        = esc(p.ip || "—");
    const port      = esc(p.port || "9000");
    const adr       = esc(p.adr || "—");
    const period    = esc(p.period || "50") + "ms";

    /* ── data row ── */
    const tr = document.createElement("tr");
    tr.className = "scene-data-row " + rowState;
    tr.id = "sr-" + name;
    tr.dataset.sceneName = name;
    tr.innerHTML =
      '<td><span class="scene-pill ' + pillClass + '"><span class="scene-dot"></span>' + pillLabel + '</span></td>' +
      '<td><span class="scene-name" title="' + esc(name) + '">' + esc(name.length > 10 ? name.slice(0, 10) + '…' : name) + '</span></td>' +
      '<td><span class="scene-msg-cell" id="smsg-' + esc(name) + '">' + esc(msgsStr || "—") + '</span></td>' +
      '<td><div class="scene-acts" onclick="event.stopPropagation()">' +
        '<button class="scene-btn scene-btn-go" data-act="start" title="Start scene">▶</button>' +
        '<button class="scene-btn scene-btn-stp" data-act="stop"  title="Stop scene">■</button>' +
      '</div></td>';
    tr.addEventListener("click", function (e) {
      if (e.metaKey || e.ctrlKey) { populateSceneForm(name, p); return; }
      toggleSceneExp(name);
    });
    tr.querySelectorAll(".scene-btn").forEach(function (btn) {
      btn.addEventListener("click", function () { sceneAction(btn.dataset.act, name); });
    });

    /* ── expand row ── */
    const expTr = document.createElement("tr");
    expTr.className = "scene-exp-row " + expState;
    expTr.id = "se-" + name;
    let gateExpItem = "";
    if (p.gate_src && p.gate_mode) {
      const gateIsEdge = (p.gate_mode === "rising" || p.gate_mode === "falling");
      let gateSuffix = "";
      if (p.gate_lo != null && p.gate_lo !== "") gateSuffix += (gateIsEdge ? " trigger:" : " \u2265") + esc(String(p.gate_lo));
      if (p.gate_hi != null && p.gate_hi !== "") gateSuffix += (gateIsEdge ? " delta:" : " \u2264") + esc(String(p.gate_hi));
      gateExpItem = '<span class="scene-exp-item"><span class="scene-exp-label">scene gate</span><span class="scene-exp-val">' + esc(p.gate_src) + ' ' + esc((p.gate_mode || "").toUpperCase()) + gateSuffix + '</span></span>';
    }
    expTr.innerHTML =
      '<td colspan="4"><div class="scene-exp-inner" id="sei-' + esc(name) + '">' +
        '<span class="scene-exp-item"><span class="scene-exp-label">overrides</span><span class="scene-exp-val">' + esc(overrides) + '</span></span>' +
        '<span class="scene-exp-item"><span class="scene-exp-label">address</span><span class="scene-exp-val">' + adr + '</span></span>' +
        '<span class="scene-exp-item"><span class="scene-exp-label">mode</span><span class="scene-exp-val">' + adrMode + '</span></span>' +
        '<span class="scene-exp-item"><span class="scene-exp-label">ip</span><span class="scene-exp-val">' + ip + '</span></span>' +
        '<span class="scene-exp-item"><span class="scene-exp-label">port</span><span class="scene-exp-val">' + port + '</span></span>' +
        '<span class="scene-exp-item"><span class="scene-exp-label">period</span><span class="scene-exp-val">' + period + '</span></span>' +
        gateExpItem +
        '<span class="scene-exp-item scene-exp-actions" onclick="event.stopPropagation()">' +
          '<button class="scene-exp-act"   data-act="edit"       title="Edit in form">EDIT</button>' +
          '<span class="scene-exp-sep">·</span>' +
          '<button class="scene-exp-resync" data-act="info"      title="Re-query scene info">RESYNC?</button>' +
          '<span class="scene-exp-sep">·</span>' +
          '<button class="scene-exp-act"   data-act="enableAll"  title="Enable all messages">ENABLE ALL</button>' +
          '<span class="scene-exp-sep">·</span>' +
          '<button class="scene-exp-act"   data-act="unsolo"     title="Unsolo all messages">UNSOLO</button>' +
          '<span class="scene-exp-sep">·</span>' +
          '<button class="scene-exp-act"   data-act="save"       title="Save to device NVS">SAVE</button>' +
          '<span class="scene-exp-sep">·</span>' +
          '<button class="scene-exp-act danger" data-act="delete" title="Delete scene">DELETE</button>' +
        '</span>' +
      '</div></td>';
    expTr.querySelectorAll("[data-act]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        const act = btn.dataset.act;
        if (act === "edit") { populateSceneForm(name, p); }
        else { sceneAction(act, name); }
      });
    });

    tbody.appendChild(tr);
    tbody.appendChild(expTr);
  });
}

/* ── Toggle scene expand row ── */

export function toggleSceneExp(name) {
  const dataRow  = document.getElementById("sr-" + name);
  const expRow   = document.getElementById("se-" + name);
  const msgEl    = document.getElementById("smsg-" + name);
  const expInner = document.getElementById("sei-" + name);
  if (!dataRow || !expRow) return;
  const isOpen = expRow.classList.contains("visible");
  if (!isOpen) {
    const clipped = msgEl && msgEl.scrollWidth > msgEl.clientWidth;
    const existing = expInner.querySelector(".scene-exp-all");
    if (existing) existing.remove();
    if (clipped) {
      const allMsgs = document.createElement("span");
      allMsgs.className = "scene-exp-all";
      allMsgs.innerHTML =
        '<span class="scene-exp-label">all messages</span>' +
        '<span class="scene-exp-val">' + esc(msgEl.textContent) + '</span>';
      expInner.appendChild(allMsgs);
    }
  }
  expRow.classList.toggle("visible", !isOpen);
  dataRow.classList.toggle("open", !isOpen);
}

/* ── Populate scene form for editing ── */

export function populateSceneForm(name, p) {
  $("#sceneName").value = name;
  $("#scenePeriod").value = p.period || "50";
  $("#sceneAdrMode").value = p.adrMode || p.adrmode || p.adr_mode || "fallback";
  $("#sceneIP").value = p.ip || "";
  $("#scenePort").value = p.port || "9000";
  $("#sceneAdr").value = p.adr || "";
  $("#sceneLow").value = p.low || "";
  $("#sceneHigh").value = p.high || "";
  /* Accept both legacy "+" and canonical comma-separated override replies. */
  const ov = (p.override || "").split(/[+,]/).map(function (s) { return s.trim(); }).filter(Boolean);
  $("#ovIP").checked = ov.indexOf("ip") !== -1;
  $("#ovPort").checked = ov.indexOf("port") !== -1;
  $("#ovAdr").checked = ov.indexOf("adr") !== -1;
  $("#ovLow").checked = ov.indexOf("low") !== -1;
  $("#ovHigh").checked = ov.indexOf("high") !== -1;
  /* Scene gate */
  if (sceneGatePicker) {
    const gs = p.gate_src || p.gate_source || "";
    const gm = p.gate_mode || "";
    const gl = p.gate_lo != null ? p.gate_lo : "";
    const gh = p.gate_hi != null ? p.gate_hi : "";
    if (gs && gm) {
      sceneGatePicker.setValue(gs, gm, gl, gh);
      const sec = $("#sceneGateSection"); if (sec) sec.style.display = "";
      const chk = $("#chkShowGate"); if (chk) chk.checked = true;
    } else {
      sceneGatePicker.clear();
    }
  }
  /* scroll to form — switch to scenes tab */
  $(".nav-btn[data-section='scenes']").click();
  $("#sceneName").focus();
}

/* ── Scene actions (start, stop, save, delete, etc.) ── */

export function sceneAction(act, name) {
  let template;
  switch (act) {
    case "start":     template = "/annieData/{device}/scene/{name}/start"; break;
    case "stop":      template = "/annieData/{device}/scene/{name}/stop"; break;
    case "info":      template = "/annieData/{device}/scene/{name}/info"; break;
    case "enableAll": template = "/annieData/{device}/scene/{name}/enableAll"; break;
    case "unsolo":    template = "/annieData/{device}/scene/{name}/unsolo"; break;
    case "save":      sendCmd(addr("/annieData/{device}/save/scene"), name); return;
    case "delete":    template = "/annieData/{device}/scene/{name}/delete"; break;
    default: return;
  }
  sendCmd(addr(template, name), null).then(function (res) {
    if (res.status === "ok") {
      toast(act + ": " + name, "success");
      const dev = getActiveDev();
      if (act === "start" && dev && dev.scenes[name]) { dev.scenes[name].running = true;  renderSceneTable(); }
      if (act === "stop"  && dev && dev.scenes[name]) { dev.scenes[name].running = false; renderSceneTable(); }
      if (act === "delete") {
        if (dev) { delete dev.scenes[name]; renderSceneTable(); if (typeof window.refreshAllDropdowns === "function") window.refreshAllDropdowns(); }
      }
    }
  });
}

/* ═══════════════════════════════════════════
   BULK ACTIONS — OSC pattern matching
   ═══════════════════════════════════════════ */

/** Returns true if s contains OSC pattern metacharacters. */
export function hasPattern(s) { return /[*?\[{]/.test(s); }

/** Add/remove .bulk-match class on table rows based on OSC pattern. */
export function highlightBulkMatches(pattern, rowSelector, nameAttr) {
  document.querySelectorAll(rowSelector).forEach(function (row) {
    const name = row.dataset[nameAttr];
    if (!name) return;
    row.classList.toggle("bulk-match", !!(pattern && oscPatternMatch(pattern, name)));
  });
}

/** Highlight the input when it contains a pattern. */
export function updatePatternHint(inputEl, hintEl, registry, applyBtn) {
  const val = inputEl.value.trim();
  if (hasPattern(val)) {
    inputEl.classList.add("has-pattern");
    let count = 0;
    if (registry) {
      const names = Object.keys(registry);
      count = names.filter(function (n) {
        return oscPatternMatch(val, n);
      }).length;
      hintEl.textContent = count + " match" + (count !== 1 ? "es" : "");
    }
    if (applyBtn) applyBtn.style.display = count > 0 ? "" : "none";
  } else {
    inputEl.classList.remove("has-pattern");
    hintEl.textContent = "";
    if (applyBtn) applyBtn.style.display = "";
  }
}

/**
 * Simple client-side OSC pattern matcher for preview hints.
 * Supports * ? [charset] {alt1,alt2}. Case-insensitive.
 */
export function oscPatternMatch(pattern, text) {
  // Convert OSC pattern to a JS regex.
  let re = "^";
  let i = 0;
  const p = pattern.toLowerCase();
  while (i < p.length) {
    const c = p[i];
    if (c === "*") { re += ".*"; i++; }
    else if (c === "?") { re += "."; i++; }
    else if (c === "[") {
      const j = p.indexOf("]", i);
      if (j < 0) { re += "\\["; i++; continue; }
      let inner = p.substring(i + 1, j);
      if (inner[0] === "!") inner = "^" + inner.substring(1);
      re += "[" + inner + "]";
      i = j + 1;
    }
    else if (c === "{") {
      const j2 = p.indexOf("}", i);
      if (j2 < 0) { re += "\\{"; i++; continue; }
      const alts = p.substring(i + 1, j2).split(",").map(function (a) {
        return a.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
      });
      re += "(?:" + alts.join("|") + ")";
      i = j2 + 1;
    }
    else {
      re += c.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
      i++;
    }
  }
  re += "$";
  try { return new RegExp(re, "i").test(text); }
  catch (e) { return false; }
}
