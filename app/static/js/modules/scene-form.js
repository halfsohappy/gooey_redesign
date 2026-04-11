/* ── Scene form — apply, clear, ops tabs, add/remove/move, clone/rename, preview ── */

import { devices, activeDevice, $ } from './state.js';
import { toast, showConfirm } from './toast.js';
import { sendCmd, addr } from './command.js';
import { getActiveDev } from './device-manager.js';
import { sceneGatePicker, renderSceneTable } from './scene-manager.js';
import { refreshAllDropdowns } from './dropdown-coordinator.js';
import { previewPair } from './message-form.js';

/* ── Apply scene config ── */

export function applySceneConfig(thenStart) {
  const name = ($("#sceneName").value || "").trim();
  if (!name) { toast("Scene name required", "error"); return; }
  const period   = ($("#scenePeriod").value || "").trim();
  const mode     = ($("#sceneAdrMode").value || "").trim();
  const ip       = ($("#sceneIP").value || "").trim();
  const port     = ($("#scenePort").value || "").trim();
  const sceneAdr = ($("#sceneAdr").value || "").trim();
  const low      = ($("#sceneLow").value || "").trim();
  const high     = ($("#sceneHigh").value || "").trim();

  const ovParts = [];
  if ($("#ovIP").checked)   ovParts.push("ip");
  if ($("#ovPort").checked) ovParts.push("port");
  if ($("#ovAdr").checked)  ovParts.push("adr");
  if ($("#ovLow").checked)  ovParts.push("low");
  if ($("#ovHigh").checked) ovParts.push("high");

  function cfgPairS(key, val) {
    if (val.charAt(0) === "<") return key + "<" + val.substring(1);
    return key + ":" + val;
  }
  const cfgParts = [];
  if (ip)       cfgParts.push(cfgPairS("ip",  ip));
  if (port)     cfgParts.push(cfgPairS("port", port));
  if (sceneAdr) cfgParts.push(cfgPairS("adr",  sceneAdr));
  if (low)      cfgParts.push(cfgPairS("low",  low));
  if (high)     cfgParts.push(cfgPairS("high", high));
  if (period)   cfgParts.push("period:" + period);
  if (mode)     cfgParts.push("adrMode:" + mode);
  cfgParts.push("override:" + (ovParts.length ? ovParts.join("+") : "none"));

  let sceneGateConfig = null;
  if (sceneGatePicker) {
    sceneGateConfig = sceneGatePicker.getConfig();
    if (sceneGateConfig && sceneGateConfig.gate_src) {
      cfgParts.push("gate_src:" + sceneGateConfig.gate_src);
      if (sceneGateConfig.gate_mode) cfgParts.push("gate_mode:" + sceneGateConfig.gate_mode);
      if (sceneGateConfig.gate_lo)   cfgParts.push("gate_lo:"   + sceneGateConfig.gate_lo);
      if (sceneGateConfig.gate_hi)   cfgParts.push("gate_hi:"   + sceneGateConfig.gate_hi);
    }
  }

  const cfg = cfgParts.join(", ");
  const startStop = thenStart
    ? addr("/annieData/{device}/scene/{name}/start", name)
    : addr("/annieData/{device}/scene/{name}/stop",  name);

  sendCmd(addr("/annieData/{device}/scene/{name}", name), cfg).then(function () {
    sendCmd(startStop, null).then(function () {
      toast("Scene " + (thenStart ? "started" : "applied") + ": " + name, "success");
      const dev = getActiveDev();
      if (dev) {
        dev.scenes[name] = Object.assign(dev.scenes[name] || {}, {
          ip: ip, port: port, adr: sceneAdr, low: low, high: high,
          period: period, adrMode: mode, override: ovParts.join(", "),
          running: thenStart,
          gate_src:  sceneGateConfig ? (sceneGateConfig.gate_src  || "") : "",
          gate_mode: sceneGateConfig ? (sceneGateConfig.gate_mode || "") : "",
          gate_lo:   sceneGateConfig ? (sceneGateConfig.gate_lo   || "") : "",
          gate_hi:   sceneGateConfig ? (sceneGateConfig.gate_hi   || "") : ""
        });
        renderSceneTable();
        refreshAllDropdowns();
      }
    });
  });
}

$("#btnSceneApplyActive").addEventListener("click",   function () { applySceneConfig(true);  });
$("#btnSceneApplyStopped").addEventListener("click",  function () { applySceneConfig(false); });

/* ── Clear scene form ── */

$("#btnSceneClear").addEventListener("click", function () {
  ["sceneName", "sceneIP", "sceneAdr", "sceneLow", "sceneHigh"].forEach(function (id) {
    $("#" + id).value = "";
  });
  $("#scenePeriod").value = "50";
  $("#sceneAdrMode").value = "fallback";
  ["ovIP", "ovPort", "ovAdr", "ovLow", "ovHigh"].forEach(function (id) {
    $("#" + id).checked = false;
  });
  if (sceneGatePicker) sceneGatePicker.clear();
  updateScenePreview();
});

/* ── Scene ops tabs ── */

document.querySelectorAll(".scene-ops-tab-btn").forEach(function (btn) {
  btn.addEventListener("click", function () {
    document.querySelectorAll(".scene-ops-tab-btn").forEach(function (b) { b.classList.remove("active"); });
    document.querySelectorAll(".scene-ops-tab-pane").forEach(function (p) { p.classList.remove("active"); });
    btn.classList.add("active");
    const pane = document.getElementById("sceneOpsPane-" + btn.dataset.tab);
    if (pane) pane.classList.add("active");
  });
});

/* ── Add / Remove / Solo / Move messages ── */

$("#btnSceneAddMsg").addEventListener("click", function () {
  const pname = ($("#sceneMsgScene").value || "").trim();
  const mnames = ($("#sceneMsgNames").value || "").trim();
  if (!pname || !mnames) { toast("Scene and message name(s) required", "error"); return; }
  sendCmd(addr("/annieData/{device}/scene/{name}/addMsg", pname), mnames).then(function (res) {
    if (res.status === "ok") toast("Added to " + pname, "success");
  });
});

$("#btnSceneRemoveMsg").addEventListener("click", function () {
  const pname = ($("#sceneMsgScene").value || "").trim();
  const mnames = ($("#sceneMsgNames").value || "").trim();
  if (!pname || !mnames) { toast("Scene and message name required", "error"); return; }
  sendCmd(addr("/annieData/{device}/scene/{name}/removeMsg", pname), mnames).then(function (res) {
    if (res.status === "ok") toast("Removed from " + pname, "success");
  });
});

$("#btnSceneMove").addEventListener("click", function () {
  const mname = ($("#sceneMsgNames").value || "").trim();
  const pname = ($("#sceneMsgScene").value || "").trim();
  if (!pname || !mname) { toast("Message and scene name required", "error"); return; }
  sendCmd(addr("/annieData/{device}/move"), mname + ", " + pname).then(function (res) {
    if (res.status === "ok") toast("Moved: " + mname + " → " + pname, "success");
  });
});

$("#btnSceneSetAll").addEventListener("click", function () {
  const pname = ($("#sceneSetAllScene").value || "").trim();
  const cfg = ($("#sceneSetAllCfg").value || "").trim();
  if (!pname || !cfg) { toast("Scene and config string required", "error"); return; }
  sendCmd(addr("/annieData/{device}/scene/{name}/setAll", pname), cfg).then(function (res) {
    if (res.status === "ok") toast("setAll applied: " + pname, "success");
  });
});

/* ── Clone / Rename ── */

$("#btnSceneClone").addEventListener("click", function () {
  const src = ($("#sceneSrcName").value || "").trim();
  const dest = ($("#sceneDestName").value || "").trim();
  if (!src || !dest) { toast("Both names required", "error"); return; }
  sendCmd(addr("/annieData/{device}/scene/clone"), src + ", " + dest).then(function (res) {
    if (res.status === "ok") toast("Cloned: " + src + " → " + dest, "success");
  });
});

$("#btnSceneRename").addEventListener("click", function () {
  const src = ($("#sceneSrcName").value || "").trim();
  const dest = ($("#sceneDestName").value || "").trim();
  if (!src || !dest) { toast("Both names required", "error"); return; }
  sendCmd(addr("/annieData/{device}/scene/rename"), src + ", " + dest).then(function (res) {
    if (res.status === "ok") {
      toast("Renamed: " + src + " → " + dest, "success");
      const dev = getActiveDev();
      if (dev && dev.scenes[src]) {
        dev.scenes[dest] = dev.scenes[src];
        delete dev.scenes[src];
        renderSceneTable();
        refreshAllDropdowns();
      }
    }
  });
});

/* ── Scene preview ── */

export function updateScenePreview() {
  const name = ($("#sceneName") ? $("#sceneName").value.trim() : "");
  const adrEl = $("#scenePreviewAdr");
  const cfgEl = $("#scenePreviewCfg");
  if (adrEl) adrEl.textContent = name ? "scene: " + name : "(no scene name)";
  const parts = [];
  let ip = ($("#sceneIP").value || "").trim(); if (ip) parts.push(previewPair("ip", ip));
  let port = ($("#scenePort").value || "").trim(); if (port) parts.push(previewPair("port", port));
  let sceneAdr = ($("#sceneAdr").value || "").trim(); if (sceneAdr) parts.push(previewPair("adr", sceneAdr));
  let low = ($("#sceneLow").value || "").trim(); if (low) parts.push(previewPair("low", low));
  let high = ($("#sceneHigh").value || "").trim(); if (high) parts.push(previewPair("high", high));
  let period = ($("#scenePeriod").value || "").trim(); if (period) parts.push(previewPair("period", period));
  let mode = ($("#sceneAdrMode").value || "").trim(); if (mode) parts.push("adrMode:" + mode);
  const ovParts = [];
  if ($("#ovIP").checked) ovParts.push("ip");
  if ($("#ovPort").checked) ovParts.push("port");
  if ($("#ovAdr").checked) ovParts.push("adr");
  if ($("#ovLow").checked) ovParts.push("low");
  if ($("#ovHigh").checked) ovParts.push("high");
  if (ovParts.length > 0) parts.push("override:" + ovParts.join(", "));
  if (sceneGatePicker) {
    const gc = sceneGatePicker.getConfig();
    if (gc) {
      parts.push(previewPair("gate_src", gc.gate_src));
      parts.push(previewPair("gate_mode", gc.gate_mode));
      if (gc.gate_lo) parts.push(previewPair("gate_lo", gc.gate_lo));
      if (gc.gate_hi) parts.push(previewPair("gate_hi", gc.gate_hi));
    }
  }
  if (cfgEl) cfgEl.textContent = parts.join(", ");
}

/* Bind input / change listeners for live preview */
["sceneName", "sceneIP", "scenePort", "sceneAdr", "sceneLow", "sceneHigh", "scenePeriod", "sceneAdrMode"].forEach(function (id) {
  const el = $("#" + id);
  if (el) el.addEventListener("input", updateScenePreview);
  if (el) el.addEventListener("change", updateScenePreview);
});
["ovIP", "ovPort", "ovAdr", "ovLow", "ovHigh"].forEach(function (id) {
  const el = $("#" + id);
  if (el) el.addEventListener("change", updateScenePreview);
});
["sceneGateSource", "sceneGateOri", "sceneGateMode", "sceneGateLo", "sceneGateHi"].forEach(function (id) {
  const el = $("#" + id);
  if (el) el.addEventListener("input", updateScenePreview);
  if (el) el.addEventListener("change", updateScenePreview);
});
updateScenePreview();
