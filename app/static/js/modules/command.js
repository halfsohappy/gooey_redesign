/* ── Command sending & address helpers ── */

import { devices, activeDevice, $ } from "./state.js";
import { toast, showConfirm } from "./toast.js";
import { api } from "./api.js";
import { getActiveDev, devHost, devPort, devName } from "./device-manager.js";
import { sendToDevice } from "./device-config.js";

/* ── Status config modal helpers ── */

/* Pre-fill statusIP with the machine's LAN IP once per session */
let _myIpFetched = false;
function prefillStatusIp() {
  const ipEl = $("#statusIP");
  if (!ipEl || ipEl.value.trim()) return;   /* already has a value — don't overwrite */
  if (_myIpFetched) return;
  _myIpFetched = true;
  fetch("/api/my-ip")
    .then(function (r) { return r.json(); })
    .then(function (d) { if (d.ip && ipEl && !ipEl.value.trim()) ipEl.value = d.ip; })
    .catch(function () { /* ignore */ });
}

/* Run on page load so the field is populated before the modal opens */
prefillStatusIp();

export function openDevSettingsModal() {
  /* Populate target device dropdown */
  const sel = $("#statusConfigTarget");
  if (sel) {
    sel.innerHTML = '<option value="__all__">All devices</option>';
    Object.keys(devices).forEach(function (id) {
      const d = devices[id];
      const opt = document.createElement("option");
      opt.value = id;
      opt.textContent = d.name;
      if (id === activeDevice.id) opt.selected = true;
      sel.appendChild(opt);
    });
  }
  prefillStatusIp();
  $("#networkSettingsModal").classList.remove("hidden");
}

export function getStatusConfigTargets() {
  const sel = $("#statusConfigTarget");
  if (!sel) return activeDevice.id ? [activeDevice.id] : [];
  const val = sel.value;
  if (val === "__all__") return Object.keys(devices);
  return val ? [val] : [];
}

/* ═══════════════════════════════════════════
   SEND HELPERS
   ═══════════════════════════════════════════ */

/** Send a TheaterGWD command for the active device. */
export function sendCmd(address, payload) {
  const data = {
    host: devHost(),
    port: devPort(),
    address: address,
  };
  /* Config-string payloads must be sent as single-element array
     so the backend does NOT split them by spaces. */
  if (payload) {
    data.args = [payload];
  }
  return api("send", data);
}

/* ── Flush: send /flush and wait for the device's reply in the feed ── */
const _flushResolvers = [];

export function _onFlushReply(entry) {
  if (entry.direction !== "recv") return;
  if (!entry.address || !/\/flush$/i.test(entry.address)) return;
  const resolvers = _flushResolvers.splice(0);
  resolvers.forEach(function (r) { r(); });
}

/**
 * Send /flush and return a promise that resolves when the device replies.
 * Falls back to a 2s timeout if no reply is received (e.g. old firmware).
 */
export function sendFlush() {
  return new Promise(function (resolve) {
    const timer = setTimeout(function () {
      const idx = _flushResolvers.indexOf(resolve);
      if (idx >= 0) _flushResolvers.splice(idx, 1);
      resolve();
    }, 2000);
    _flushResolvers.push(function () {
      clearTimeout(timer);
      resolve();
    });
    sendCmd(addr("/annieData/{device}/flush"), null);
  });
}

/** Build an OSC address from a template, substituting {device} and {name}. */
export function addr(template, name) {
  let a = template.replace("{device}", devName());
  if (name !== undefined) a = a.replace("{name}", name);
  return a;
}

/* ── Command address map ── */
export const CMD_ADDRESSES = {
  blackout:       "/annieData/{device}/blackout",
  restore:        "/annieData/{device}/restore",
  on_change:      "/annieData/{device}/on_change",
  save:           "/annieData/{device}/save",
  load:           "/annieData/{device}/load",
  nvs_clear:      "/annieData/{device}/nvs/clear",
  list_messages:  "/annieData/{device}/list/msgs",
  list_scenes:    "/annieData/{device}/list/scenes",
  list_all:       "/annieData/{device}/list/all",
};
