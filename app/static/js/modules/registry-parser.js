/* ── Reply parsing — auto-populate device registry ── */

import { devices, activeDevice, $ } from "./state.js";
import { CMD_ADDRESSES } from "./command.js";

/* ── Late-binding callbacks (set by other modules to avoid circular deps) ── */
let _renderMsgTable, _renderSceneTable, _renderOriTable, _refreshAllDropdowns,
    _renderShowDeviceTable, _showOriDetails;

export function setParserCallbacks({ renderMsgTable, renderSceneTable, renderOriTable, refreshAllDropdowns, renderShowDeviceTable, showOriDetails }) {
  _renderMsgTable = renderMsgTable;
  _renderSceneTable = renderSceneTable;
  _renderOriTable = renderOriTable;
  _refreshAllDropdowns = refreshAllDropdowns;
  _renderShowDeviceTable = renderShowDeviceTable;
  _showOriDetails = showOriDetails;
}

/* Local HTML-escape helper (avoids circular dep with feed.js) */
function _esc(s) {
  const d = document.createElement("div");
  d.textContent = s;
  return d.innerHTML;
}

/**
 * Parse incoming OSC messages from the device. The device sends status
 * replies that include message and scene info. We try to extract names
 * and parameters from them to keep the local registry in sync.
 *
 * Typical reply patterns:
 *   /annieData/{device}/status   "msg: accelX | value:accelX ip:192.168.1.50 ..."
 *   /annieData/{device}/status   "scene: sensors | period:50 adrMode:fallback msgs:accelX+accelY"
 *   /annieData/{device}/status   "[INFO] list/msgs: accelX, accelY, gyroZ"
 */
export function parseReplyIntoRegistry(entry) {
  if (entry.direction !== "recv") return;

  let text = "";
  (entry.args || []).forEach(function (a) {
    if (a.value !== undefined) text += " " + a.value;
  });
  text = text.trim();
  if (!text) return;

  /* Determine which device this reply belongs to.
     Match "/deviceName/" or "/deviceName" at end of path. */
  let matchedId = "";
  Object.keys(devices).forEach(function (id) {
    const d = devices[id];
    if (!entry.address) return;
    const seg = "/" + d.name + "/";
    const tail = "/" + d.name;
    if (entry.address.indexOf(seg) !== -1 || entry.address.slice(-tail.length) === tail) matchedId = id;
  });
  if (!matchedId) matchedId = activeDevice.id;
  if (!matchedId) return;
  const dev = devices[matchedId];
  if (!dev) return;

  /* ── Parse list replies ──
     The device sends replies to /reply/{dev}/list/msgs (or /scenes, /all)
     with a multi-line payload: "Messages (N):\n  name1\n  name2\n..."
     Detect by address; fall back to text-pattern for legacy status messages. */
  const listAddr = entry.address || "";
  const isListReply = /\/list\/(msgs|messages|scenes|all)/i.test(listAddr);
  const isLegacyList = !isListReply && text.match(/list\/(?:msgs|scenes|all):\s*(.+)/i);
  if (isListReply || isLegacyList) {
    if (isLegacyList) {
      const legacyMatch = text.match(/list\/(?:msgs|scenes|all):\s*(.+)/i);
      const names = legacyMatch[1].split(/[,\s]+/).map(function (s) { return s.trim(); }).filter(Boolean);
      const isMsgList = text.match(/list\/msgs/i) || text.match(/list\/all/i);
      const isSceneList = text.match(/list\/scenes/i) || text.match(/list\/all/i);
      names.forEach(function (n) {
        if (isMsgList   && !dev.messages[n]) dev.messages[n] = {};
        if (isSceneList && !dev.scenes[n])  dev.scenes[n]  = {};
      });
    } else {
      /* Multi-line reply format from the device. For /list/all, track which
         block we are in; for /list/msgs or /list/scenes use the address. */
      const isAllList  = /\/list\/all/i.test(listAddr);
      const isMsgList  = /\/list\/msgs/i.test(listAddr);
      let curBlock = isAllList ? "" : (isMsgList ? "msg" : "scene");
      text.split(/\n/).forEach(function (line) {
        const trimmed = line.trim();
        if (!trimmed) return;
        /* Parse device-level state lines (on_change:on/off) */
        const on_changeMatch = trimmed.match(/^on_change:(on|off)$/i);
        if (on_changeMatch) {
          dev.on_change = on_changeMatch[1].toLowerCase() === "on";
          return;
        }
        if (/^messages\s*\(\d+\):/i.test(trimmed)) { curBlock = "msg";   return; }
        if (/^scenes\s*\(\d+\):/i.test(trimmed))  { curBlock = "scene"; return; }
        const n = trimmed.split(/\s+/)[0];
        if (!n) return;
        if (curBlock === "msg") {
          const mParams = parseConfigString(trimmed);
          // Normalize firmware's 'val:' key to 'value' for UI compatibility.
          if (mParams.val !== undefined && mParams.value === undefined) mParams.value = mParams.val;
          // Extract enabled state from [ON]/[OFF] status block.
          if (/\[ON\]/i.test(trimmed))  mParams.enabled = "true";
          if (/\[OFF\]/i.test(trimmed)) mParams.enabled = "false";
          dev.messages[n] = Object.assign(dev.messages[n] || {}, mParams);
        }
        if (curBlock === "scene") {
          const pParams = parseConfigString(trimmed);
          // Extract send period and running state from "[RUNNING, 50ms, …]" or "[STOPPED, 50ms, …]".
          const periodM = trimmed.match(/\[(RUNNING|STOPPED),\s*(\d+)ms/i);
          if (periodM) { pParams.period = periodM[2]; pParams.running = /RUNNING/i.test(periodM[1]); }
          dev.scenes[n] = Object.assign(dev.scenes[n] || {}, pParams);
        }
      });
    }
    if (_renderMsgTable) _renderMsgTable();
    if (_renderSceneTable) _renderSceneTable();
    if (_refreshAllDropdowns) _refreshAllDropdowns();
    return;
  }

  /* ── Parse msg info reply ── */
  const msgMatch = text.match(/msg:\s*(\S+)\s*\|\s*(.*)/i);
  if (msgMatch) {
    const mName = msgMatch[1];
    const mParams = parseConfigString(msgMatch[2]);
    dev.messages[mName] = Object.assign(dev.messages[mName] || {}, mParams);
    if (_renderMsgTable) _renderMsgTable();
    if (_refreshAllDropdowns) _refreshAllDropdowns();
    return;
  }

  /* ── Parse scene info reply ── */
  const sceneMatch = text.match(/scene:\s*(\S+)\s*\|\s*(.*)/i);
  if (sceneMatch) {
    const pName = sceneMatch[1];
    const pParams = parseConfigString(sceneMatch[2]);
    const infoRunM = sceneMatch[2].match(/\[(RUNNING|STOPPED)/i);
    if (infoRunM) pParams.running = /RUNNING/i.test(infoRunM[1]);
    dev.scenes[pName] = Object.assign(dev.scenes[pName] || {}, pParams);
    if (_renderSceneTable) _renderSceneTable();
    if (_refreshAllDropdowns) _refreshAllDropdowns();
    return;
  }

  /* ── Parse ori list reply ──
     Address contains /ori/list.  Payload format (firmware v2):
     "light1 [3] (AX) (*), zone1 [5], pending1 [P]"  or  "(none)"
     [P]    = unsampled slot (registered but no samples captured)
     [N]    = N samples in cloud
     (AX)   = axis-aware matching (pointing mode)
     (*)    = currently active */
  if (/\/ori\/list/i.test(listAddr)) {
    dev.oris = {};
    if (text !== "(none)") {
      const oriParts = text.split(/,\s*/);
      oriParts.forEach(function (part) {
        part = part.trim();
        if (!part) return;
        /* Match: name followed by optional tags */
        const om = part.match(/^(\S+)/);
        if (!om) return;
        const oName = om[1];
        const isPending = /\[P\]/.test(part);
        const sampleMatch = part.match(/\[(\d+)\]/);
        const samples = isPending ? 0 : (sampleMatch ? parseInt(sampleMatch[1], 10) : 1);
        dev.oris[oName] = {
          name: oName,
          samples: samples,
          useAxis: /\(AX\)/.test(part),
          color: null,
          active: /\(\*\)/.test(part)
        };
      });
    }
    if (_renderOriTable) _renderOriTable();
    if (_refreshAllDropdowns) _refreshAllDropdowns();
    return;
  }

  /* ── Parse show list reply ──
     Address contains /show/list. Payload: CSV of show names or "(none)" */
  if (/\/show\/list/i.test(listAddr)) {
    const showNames = (text && text !== "(none)") ? text.split(/,\s*/) : [];
    if (_renderShowDeviceTable) _renderShowDeviceTable(showNames);
    /* Populate the datalist for show name input */
    const showDl = $("#showNameList");
    if (showDl) {
      showDl.innerHTML = showNames.map(function (n) {
        return '<option value="' + _esc(n.trim()) + '">';
      }).join("");
    }
    return;
  }

  /* ── Parse ori info reply ──
     Address contains /ori/info.  Payload format:
     "name: samples=N center=[x, y, z] half_w=[x, y, z]"  or
     "name: samples=1 point q=(qi,qj,qk,qr) euler=[x, y, z]" */
  if (/\/ori\/info/i.test(listAddr)) {
    if (_showOriDetails) _showOriDetails(text);
    return;
  }

  /* ── Parse ori active reply ──
     Address contains /ori/active. Payload: ori name or "(none)" */
  if (/\/ori\/active/i.test(listAddr)) {
    Object.keys(dev.oris).forEach(function (k) { dev.oris[k].active = false; });
    if (text !== "(none)" && dev.oris[text]) {
      dev.oris[text].active = true;
    }
    if (_renderOriTable) _renderOriTable();
    return;
  }

  /* ── Parse on_change reply ── */
  const onChangeReply = text.match(/^on_change\s+(ON|OFF)$/i);
  if (onChangeReply) {
    dev.on_change = onChangeReply[1].toUpperCase() === "ON";
    return;
  }

  /* ── Parse verbose list lines (key:val pairs with a leading name) ── */
  const verboseMatch = text.match(/^\[(?:INFO|DEBUG)\]\s+(\S+)\s*[:=]\s*(.*)/i);
  if (verboseMatch) {
    const vName = verboseMatch[1];
    const vRest = verboseMatch[2];
    if (vRest.indexOf("value:") !== -1 || vRest.indexOf("val:") !== -1 || vRest.indexOf("ip:") !== -1 || vRest.indexOf("adr:") !== -1) {
      const vParams = parseConfigString(vRest);
      if (vParams.val !== undefined && vParams.value === undefined) vParams.value = vParams.val;
      dev.messages[vName] = Object.assign(dev.messages[vName] || {}, vParams);
      if (_renderMsgTable) _renderMsgTable();
      if (_refreshAllDropdowns) _refreshAllDropdowns();
    } else if (vRest.indexOf("period:") !== -1 || vRest.indexOf("adrMode:") !== -1 || vRest.indexOf("adr_mode:") !== -1 || vRest.indexOf("msgs:") !== -1) {
      const vpParams = parseConfigString(vRest);
      dev.scenes[vName] = Object.assign(dev.scenes[vName] || {}, vpParams);
      if (_renderSceneTable) _renderSceneTable();
      if (_refreshAllDropdowns) _refreshAllDropdowns();
    }
  }
}

/** Parse key:value pairs separated by commas/whitespace; values may contain spaces until the next key:. */
export function parseConfigString(str) {
  const result = {};
  // Group 1 captures the key; group 2 captures the value.
  // The lookahead stops value capture at the next "key:" token (optionally comma-separated) or end of string.
  const re = /([a-zA-Z_][a-zA-Z0-9_]*)\s*:\s*(.*?)(?=(?:\s*,?\s*[a-zA-Z_][a-zA-Z0-9_]*\s*:)|$)/g;
  let match;
  while ((match = re.exec(str)) !== null) {
    result[match[1].trim()] = match[2].trim();
  }
  return result;
}
