/* ── Dropdown / datalist refresh coordinator ── */

import { $  } from './state.js';
import { getActiveDev } from './device-manager.js';

/**
 * Rebuild every name-datalist and gate-source option list
 * so that autocomplete menus stay in sync with device state.
 */
export function refreshAllDropdowns() {
  const dev = getActiveDev();
  if (!dev) return;

  /* Message name datalists */
  const msgNames = Object.keys(dev.messages);
  ["#msgNameList", "#msgNameList2"].forEach(function (sel) {
    const dl = $(sel);
    if (!dl) return;
    dl.innerHTML = "";
    msgNames.forEach(function (n) {
      const o = document.createElement("option");
      o.value = n;
      dl.appendChild(o);
    });
  });

  /* Scene name datalists */
  const sceneNames = Object.keys(dev.scenes);
  ["#sceneNameList", "#sceneNameList2", "#sceneNameList3", "#sceneNameList4", "#sceneNameListSetAll"].forEach(function (sel) {
    const dl = $(sel);
    if (!dl) return;
    dl.innerHTML = "";
    sceneNames.forEach(function (n) {
      const o = document.createElement("option");
      o.value = n;
      dl.appendChild(o);
    });
  });

  /* Ori name datalist */
  const oriNames = Object.keys(dev.oris || {});
  const oriDl = $("#oriNameList");
  if (oriDl) {
    oriDl.innerHTML = "";
    oriNames.forEach(function (n) {
      const o = document.createElement("option");
      o.value = n;
      oriDl.appendChild(o);
    });
  }
}
