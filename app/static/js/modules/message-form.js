/* ── Message form — preview, apply, clear, clone, rename, direct send ── */

import { devices, activeDevice, $ } from './state.js';
import { toast, showConfirm } from './toast.js';
import { sendCmd, addr } from './command.js';
import { getActiveDev } from './device-manager.js';
import { msgGatePicker, registerString, updateStringMode, msgPicker, renderMsgTable, populateMsgForm } from './message-manager.js';
import { renderOriTable } from './ori-manager.js';
import { renderSceneTable } from './scene-manager.js';
import { refreshAllDropdowns } from './dropdown-coordinator.js';
import { parseConfigString } from './registry-parser.js';

/* ── Config preview ── */

export function previewPair(key, val) {
  if (val.charAt(0) === "<") return key + "<" + val.substring(1);
  return key + ":" + val;
}

export function updateMsgPreview() {
  const a = ($("#msgAdr") ? $("#msgAdr").value.trim() : "");
  const adrEl = $("#msgPreviewAdr");
  const cfgEl = $("#msgPreviewCfg");
  if (adrEl) adrEl.textContent = a ? previewPair("adr", a) : "(no address)";
  const parts = [];
  let v = $("#msgValue").value; if (v) parts.push(previewPair("value", v));
  let ip = $("#msgIP").value.trim(); if (ip) parts.push(previewPair("ip", ip));
  let port = $("#msgPort").value; if (port) parts.push(previewPair("port", port));
  let lo = $("#msgLow").value.trim(); if (lo) parts.push(previewPair("low", lo));
  let hi = $("#msgHigh").value.trim(); if (hi) parts.push(previewPair("high", hi));
  let pa = $("#msgScene").value.trim(); if (pa) parts.push(previewPair("scene", pa));
  if (msgGatePicker) {
    const gc = msgGatePicker.getConfig();
    if (gc) {
      parts.push(previewPair("gate_src", gc.gate_src));
      parts.push(previewPair("gate_mode", gc.gate_mode));
      if (gc.gate_lo) parts.push(previewPair("gate_lo", gc.gate_lo));
      if (gc.gate_hi) parts.push(previewPair("gate_hi", gc.gate_hi));
    }
  }
  if (cfgEl) cfgEl.textContent = parts.join(", ");
}

/* Bind input listeners for live preview */
["msgCategory", "msgValue", "msgIP", "msgPort", "msgAdr", "msgLow", "msgHigh", "msgScene", "msgStringVal", "msgGateSource", "msgGateOri", "msgGateMode", "msgGateLo", "msgGateHi"].forEach(function (id) {
  const el = $("#" + id);
  if (el) el.addEventListener("input", updateMsgPreview);
});
updateMsgPreview();

/* ── Apply message (create / update) ── */

$("#btnMsgApply").addEventListener("click", function () {
  const name = ($("#msgName").value || "").trim();
  if (!name) { toast("Message name required", "error"); return; }

  function resolveName(val, ori) {
    return val.toLowerCase() === "name" ? (ori ? "ori_" + name : "/" + name) : val;
  }

  function cfgPair(key, val) {
    if (val.charAt(0) === "<") return key + "<" + val.substring(1);
    return key + ":" + val;
  }

  const isStringType = ($("#msgValue").value === "string");
  const strValRaw    = isStringType ? ($("#msgStringVal").value || "").trim() : "";
  if (isStringType && !strValRaw) { toast("String value required", "error"); return; }

  function doMsgApply(resolvedStrName) {
    const parts = [];
    let a = ($("#msgAdr") ? $("#msgAdr").value.trim() : ""); if (a) parts.push(cfgPair("adr", resolveName(a, false)));
    let v = isStringType ? resolvedStrName : $("#msgValue").value; if (v) parts.push(cfgPair("value", v));
    let ip = $("#msgIP").value.trim(); if (ip) parts.push(cfgPair("ip", ip));
    let port = $("#msgPort").value; if (port) parts.push(cfgPair("port", port));
    if (!isStringType) {
      let lo = $("#msgLow").value.trim(); if (lo) parts.push(cfgPair("low", lo));
      let hi = $("#msgHigh").value.trim(); if (hi) parts.push(cfgPair("high", hi));
    }
    let pa = $("#msgScene").value.trim(); if (pa) parts.push(cfgPair("scene", pa));
    if (msgGatePicker) {
      const gc = msgGatePicker.getConfig();
      if (gc) {
        let gSrc = gc.gate_src;
        if (gSrc.indexOf("ori:") === 0) {
          const oriPart = gSrc.substring(4);
          gSrc = "ori:" + resolveName(oriPart, true);
        }
        parts.push(cfgPair("gate_src", gSrc));
        parts.push(cfgPair("gate_mode", gc.gate_mode));
        if (gc.gate_lo) parts.push(cfgPair("gate_lo", gc.gate_lo));
        if (gc.gate_hi) parts.push(cfgPair("gate_hi", gc.gate_hi));
      }
    }
    const cfg = parts.join(", ");
    const address = addr("/annieData/{device}/msg/{name}", name);
    sendCmd(address, cfg || null).then(function (res) {
      if (res.status === "ok") {
        toast("Applied: " + name, "success");
        const dev = getActiveDev();
        if (dev) {
          const parsed = parseConfigString(cfg);
          if (isStringType) { parsed.string_val = strValRaw; }
          dev.messages[name] = parsed;
          /* Auto-register ori names from gate */
          if (msgGatePicker) {
            const gc2 = msgGatePicker.getConfig();
            if (gc2 && gc2.gate_src.indexOf("ori:") === 0) {
              let oriName = gc2.gate_src.substring(4);
              oriName = resolveName(oriName, true);
              if (oriName && !dev.oris[oriName]) {
                dev.oris[oriName] = { color: [255, 255, 255], samples: 0, pre_reg: true };
              }
            }
          }
          renderMsgTable();
          renderOriTable();
          refreshAllDropdowns();
        }
      }
    });
  }

  if (isStringType && !/^str\d+$/i.test(strValRaw)) {
    registerString(strValRaw)
      .then(function (strName) { doMsgApply(strName); })
      .catch(function (e) { toast("String registration failed: " + e.message, "error"); });
  } else {
    doMsgApply(isStringType ? strValRaw : "");
  }
});

/* ── Clear form ── */

$("#btnMsgClear").addEventListener("click", function () {
  ["msgName", "msgIP", "msgAdr", "msgLow", "msgHigh", "msgScene", "msgStringVal"].forEach(function (id) {
    const el = $("#" + id); if (el) el.value = "";
  });
  $("#msgValue").value = "";
  if (msgPicker) msgPicker.clear();
  if (msgGatePicker) msgGatePicker.clear();
  updateStringMode();
  updateMsgPreview();
});

/* ── Clone / Rename ── */

$("#btnMsgClone").addEventListener("click", function () {
  const src = ($("#msgSrcName").value || "").trim();
  const dest = ($("#msgDestName").value || "").trim();
  if (!src || !dest) { toast("Both names required", "error"); return; }
  sendCmd(addr("/annieData/{device}/msg/clone"), src + ", " + dest).then(function (res) {
    if (res.status === "ok") toast("Cloned: " + src + " → " + dest, "success");
  });
});

$("#btnMsgRename").addEventListener("click", function () {
  const src = ($("#msgSrcName").value || "").trim();
  const dest = ($("#msgDestName").value || "").trim();
  if (!src || !dest) { toast("Both names required", "error"); return; }
  sendCmd(addr("/annieData/{device}/msg/rename"), src + ", " + dest).then(function (res) {
    if (res.status === "ok") {
      toast("Renamed: " + src + " → " + dest, "success");
      const dev = getActiveDev();
      if (dev && dev.messages[src]) {
        dev.messages[dest] = dev.messages[src];
        delete dev.messages[src];
        renderMsgTable();
        refreshAllDropdowns();
      }
    }
  });
});

/* ── Send Direct (from message editor) ── */

$("#btnMsgDirect").addEventListener("click", function () {
  const name = ($("#msgName").value || "").trim();
  if (!name) { toast("Message name required", "error"); return; }

  function cfgPairD(key, val) {
    if (val.charAt(0) === "<") return key + "<" + val.substring(1);
    return key + ":" + val;
  }

  const isDirStrType = ($("#msgValue").value === "string");
  const dirStrRaw    = isDirStrType ? ($("#msgStringVal").value || "").trim() : "";
  if (isDirStrType && !dirStrRaw) { toast("String value required", "error"); return; }

  function doDirect(resolvedStrName) {
    const parts = [];
    let v = isDirStrType ? resolvedStrName : $("#msgValue").value; if (v) parts.push(cfgPairD("value", v));
    let ip = $("#msgIP").value.trim(); if (ip) parts.push(cfgPairD("ip", ip));
    let port = $("#msgPort").value; if (port) parts.push(cfgPairD("port", port));
    let a = ($("#msgAdr") ? $("#msgAdr").value.trim() : "");
    if (a) parts.push(cfgPairD("adr", a.toLowerCase() === "name" ? "/" + name : a));
    if (!isDirStrType) {
      let lo = $("#msgLow").value.trim(); if (lo) parts.push(cfgPairD("low", lo));
      let hi = $("#msgHigh").value.trim(); if (hi) parts.push(cfgPairD("high", hi));
    }
    /* Gate config — applied to the message only (not the scene) */
    if (msgGatePicker) {
      const gc = msgGatePicker.getConfig();
      if (gc) {
        parts.push(cfgPairD("gate_src", gc.gate_src));
        parts.push(cfgPairD("gate_mode", gc.gate_mode));
        if (gc.gate_lo) parts.push(cfgPairD("gate_lo", gc.gate_lo));
        if (gc.gate_hi) parts.push(cfgPairD("gate_hi", gc.gate_hi));
      }
    }
    parts.push("period:50");
    const cfg = parts.join(", ");

    const sceneName = ($("#msgScene").value || "").trim();
    const autoStart = !($("#chkDirectAutoStart") && !$("#chkDirectAutoStart").checked);

    sendCmd(addr("/annieData/{device}/direct/{name}", name), cfg).then(function (res) {
      if (res.status !== "ok") return;
      const dev = getActiveDev();
      if (!dev) { toast("Direct: " + name, "success"); return; }

      /* Update message tracker locally */
      dev.messages[name] = parseConfigString(cfg);

      /* Direct always creates a same-named scene and starts it.
         Register it locally, then rename if user specified a scene name. */
      const finalSceneName = (sceneName && sceneName !== name) ? sceneName : name;
      dev.scenes[name] = Object.assign(dev.scenes[name] || {}, {
        period: "50", running: autoStart
      });

      const finish = function () {
        if (!autoStart) {
          sendCmd(addr("/annieData/{device}/scene/{name}/stop", name), null);
          dev.scenes[finalSceneName] = Object.assign(dev.scenes[finalSceneName] || {}, { running: false });
        }
        renderMsgTable();
        renderSceneTable();
        refreshAllDropdowns();
        toast("Direct: " + name + (finalSceneName !== name ? " → scene: " + finalSceneName : ""), "success");
      };

      if (finalSceneName !== name) {
        sendCmd(addr("/annieData/{device}/scene/rename"), name + ", " + finalSceneName).then(function () {
          dev.scenes[finalSceneName] = Object.assign({}, dev.scenes[name], { running: autoStart });
          delete dev.scenes[name];
          finish();
        });
      } else {
        finish();
      }
    });
  }

  if (isDirStrType && !/^str\d+$/i.test(dirStrRaw)) {
    registerString(dirStrRaw)
      .then(function (strName) { doDirect(strName); })
      .catch(function (e) { toast("String registration failed: " + e.message, "error"); });
  } else {
    doDirect(isDirStrType ? dirStrRaw : "");
  }
});
