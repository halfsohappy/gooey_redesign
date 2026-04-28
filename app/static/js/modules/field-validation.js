/* ── Inline Field Validation — IP expansion, destination lookup, error hints ── */

import { $ } from './state.js';

/* ═══════════════════════════════════════════
   ASSUME-IP PREFIX
   ═══════════════════════════════════════════ */

let _assumeIpPrefix = "";

export function setAssumeIpPrefix(prefix) {
  _assumeIpPrefix = prefix;
}

export function expandIp(val) {
  if (!_assumeIpPrefix) return val;
  const trimmed = val.trim();
  /* Count dots in prefix to know how many octets the user must supply */
  const prefixDots = (_assumeIpPrefix.match(/\./g) || []).length;
  const needed = 4 - prefixDots;
  if (needed < 1 || needed > 3) return val;
  /* Build pattern for `needed` dot-separated octet groups */
  const octetGroup = "\\d{1,3}";
  const groups = [];
  for (let i = 0; i < needed; i++) groups.push(octetGroup);
  const re = new RegExp("^" + groups.join("\\.") + "$");
  if (re.test(trimmed)) {
    const octets = trimmed.split(".");
    const valid = octets.every(function (o) { const n = parseInt(o, 10); return n >= 0 && n <= 255; });
    if (valid) return _assumeIpPrefix + trimmed;
  }
  return val;
}

/* ═══════════════════════════════════════════
   NETWORK DESTINATIONS
   ═══════════════════════════════════════════ */

let _networkDests = [];

export function setNetworkDests(dests) {
  _networkDests = Array.isArray(dests) ? dests : [];
}

/* Map each IP field to its paired port field (null = no auto-fill port) */
const IP_PORT_PAIRS = {
  msgIP:          "msgPort",
  sceneIP:        "scenePort",
  rawHost:        "rawPort",
  bridgeOutHost:  "bridgeOutPort",
  statusIP:       "statusPort",
  deviceConfigIP: null,
  netDestIp:      null,   /* modal add-form: port is a separate adjacent field */
};

/* ═══════════════════════════════════════════
   VALIDATION HELPER
   ═══════════════════════════════════════════ */

export function validateField(input, isValid, msg) {
  let hint;
  if (!isValid) {
    input.classList.add("field-error");
    hint = input.parentElement.querySelector(".field-hint");
    if (!hint) { hint = document.createElement("div"); hint.className = "field-hint"; input.parentElement.appendChild(hint); }
    hint.textContent = msg;
    return false;
  } else {
    input.classList.remove("field-error");
    hint = input.parentElement.querySelector(".field-hint");
    if (hint) hint.remove();
    return true;
  }
}

/* ═══════════════════════════════════════════
   BLUR HANDLERS FOR IP FIELDS
   Resolution order:
     1. Destination name  → fill IP + port
     2. Assume-IP prefix  → expand short octet
   ═══════════════════════════════════════════ */

Object.keys(IP_PORT_PAIRS).forEach(function (fieldId) {
  const el = $("#" + fieldId);
  if (!el) return;

  el.addEventListener("blur", function () {
    const v = el.value.trim();
    if (!v) return;

    /* 1 — Destination name match */
    const dest = _networkDests.find(function (d) {
      return d.name.toLowerCase() === v.toLowerCase();
    });
    if (dest) {
      el.value = dest.ip;
      const portId = IP_PORT_PAIRS[fieldId];
      if (portId && dest.port) {
        const portEl = $("#" + portId);
        if (portEl) portEl.value = dest.port;
      }
      return;
    }

    /* 2 — Assume-IP expansion */
    const expanded = expandIp(v);
    if (expanded !== v) { el.value = expanded; }
  });
});
