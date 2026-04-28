/* ── Reference panel — populate from /api/presets/theater-gwd ── */

import { $ } from "./state.js";
import { esc } from "./feed.js";

function renderCmds(cmds, container, filter) {
  container.innerHTML = "";
  Object.keys(cmds).forEach(function (key) {
    const c = cmds[key];
    if (filter && key.toLowerCase().indexOf(filter) === -1 && (c.description || "").toLowerCase().indexOf(filter) === -1) return;
    const div = document.createElement("div");
    div.className = "ref-item";
    div.innerHTML =
      '<span class="ref-term">' + esc(key) + "</span>" +
      '<span class="ref-addr">"' + esc(c.address) + '"</span>' +
      '<span class="ref-def">' + esc(c.description) + "</span>" +
      (c.payload ? '<span class="ref-payload">payload: ' + esc(c.payload) + "</span>" : "");
    container.appendChild(div);
  });
}

const KW_CATEGORIES = [
  { title: "Sensors — Acceleration", keys: ["accelX","accelY","accelZ","accelLength","gaccelX","gaccelY","gaccelZ","gaccelLength","limbFwd","limbLat","limbVert","twitch"] },
  { title: "Sensors — Orientation",  keys: ["roll","pitch","yaw","twist","azi","tilt","quatI","quatJ","quatK","quatR"] },
  { title: "Sensors — Gyroscope",    keys: ["gyroX","gyroY","gyroZ","gyroLength","twistVel","aziVel","tiltVel"] },
  { title: "Sensors — Barometer",    keys: ["baro"] },
  { title: "Device Commands",        keys: ["blackout","restore","save","load","nvs/clear","list","status/config","status/level"] },
  { title: "Message Commands",       keys: ["msg","enable","disable","delete","info","save/msg","addMsg","removeMsg","clone","rename","move","direct"] },
  { title: "Scene Commands",         keys: ["scene","start","stop","period","override","adrMode","setAll","solo","unsolo","enableAll","save/scene"] },
  { title: "Address Modes",          keys: ["fallback","prepend","append"] },
  { title: "Other",                  keys: ["config string"] },
];

function renderKWs(kws, container, filter) {
  container.innerHTML = "";
  const usedKeys = [];
  KW_CATEGORIES.forEach(function (cat) {
    const filtered = cat.keys.filter(function (key) {
      if (!kws[key]) return false;
      if (filter && key.toLowerCase().indexOf(filter) === -1 && kws[key].toLowerCase().indexOf(filter) === -1) return false;
      return true;
    });
    if (!filtered.length) return;

    const collapse = document.createElement("div");
    collapse.className = "collapse collapse-arrow ref-collapse";
    const checkbox = document.createElement("input");
    checkbox.type = "checkbox";
    checkbox.defaultChecked = true;
    const title = document.createElement("div");
    title.className = "collapse-title";
    title.textContent = cat.title;
    const content = document.createElement("div");
    content.className = "collapse-content";

    filtered.forEach(function (key) {
      usedKeys.push(key);
      const div = document.createElement("div");
      div.className = "ref-item";
      div.innerHTML = '<span class="ref-term">' + esc(key) + "</span> <span class=\"ref-def\">" + esc(kws[key]) + "</span>";
      content.appendChild(div);
    });

    collapse.appendChild(checkbox);
    collapse.appendChild(title);
    collapse.appendChild(content);
    container.appendChild(collapse);
  });
  /* Uncategorised */
  Object.keys(kws).sort().forEach(function (key) {
    if (usedKeys.indexOf(key) !== -1) return;
    if (filter && key.toLowerCase().indexOf(filter) === -1 && kws[key].toLowerCase().indexOf(filter) === -1) return;
    const div = document.createElement("div");
    div.className = "ref-item";
    div.innerHTML = '<span class="ref-term">' + esc(key) + "</span> <span class=\"ref-def\">" + esc(kws[key]) + "</span>";
    container.appendChild(div);
  });
}

fetch("/api/presets/theater-gwd")
  .then(function (r) { return r.json(); })
  .then(function (data) {
    const presets = data.presets;
    if (!presets) return;

    const cmdContainer  = $("#cmdList");
    const kwContainer   = $("#keywordList");
    const ckContainer   = $("#configKeyList");
    const amContainer   = $("#adrModeList");

    const cmds = presets.commands     || {};
    const kws  = presets.keywords     || {};
    const cks  = presets.config_keys  || {};
    const ams  = presets.address_modes || {};

    /* Initial render */
    if (cmdContainer) renderCmds(cmds, cmdContainer, "");
    if (kwContainer)  renderKWs(kws, kwContainer, "");

    if (ckContainer) {
      Object.keys(cks).forEach(function (key) {
        const div = document.createElement("div");
        div.className = "ref-item";
        div.innerHTML = '<span class="ref-term">' + esc(key) + '</span> <span class="ref-def">' + esc(cks[key]) + "</span>";
        ckContainer.appendChild(div);
      });
    }

    if (amContainer) {
      Object.keys(ams).forEach(function (key) {
        const div = document.createElement("div");
        div.className = "ref-item";
        div.innerHTML = '<span class="ref-term">' + esc(key) + '</span> <span class="ref-def">' + esc(ams[key]) + "</span>";
        amContainer.appendChild(div);
      });
    }

    /* Live search */
    const refSearch = $("#refSearch");
    if (refSearch) {
      refSearch.addEventListener("input", function () {
        const f = refSearch.value.trim().toLowerCase();
        if (cmdContainer) renderCmds(cmds, cmdContainer, f);
        if (kwContainer)  renderKWs(kws, kwContainer, f);
        /* Config keys and address modes: simple show/hide */
        if (ckContainer) {
          ckContainer.querySelectorAll(".ref-item").forEach(function (el) {
            const text = el.textContent.toLowerCase();
            el.style.display = (!f || text.indexOf(f) !== -1) ? "" : "none";
          });
        }
        if (amContainer) {
          amContainer.querySelectorAll(".ref-item").forEach(function (el) {
            const text = el.textContent.toLowerCase();
            el.style.display = (!f || text.indexOf(f) !== -1) ? "" : "none";
          });
        }
      });
    }
  })
  .catch(function () { /* silently ignore if presets endpoint is unavailable */ });
