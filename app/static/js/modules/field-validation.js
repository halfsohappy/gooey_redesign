/* ── Inline Field Validation — IP expansion, error hints, blur handlers ── */

import { $ } from './state.js';

/* ═══════════════════════════════════════════
   INLINE FIELD VALIDATION
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

/* IP validation — expand shorthand and show format hint */
["statusIP", "msgIP", "sceneIP", "rawHost", "bridgeOutHost", "deviceConfigIP"].forEach(function (fieldId) {
  const el = $("#" + fieldId);
  if (!el) return;
  el.addEventListener("blur", function () {
    const v = el.value.trim();
    if (!v) return; /* allow empty */
    const expanded = expandIp(v);
    if (expanded !== v) { el.value = expanded; }
  });
});
