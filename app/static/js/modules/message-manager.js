/* ── Message manager — message table, sensor pickers, gate pickers ── */

import { devices, activeDevice, $, $$ } from './state.js';
import { toast, showConfirm } from './toast.js';
import { sendCmd, addr, sendFlush } from './command.js';
import { getActiveDev, saveDevicesToStorage } from './device-manager.js';
import { esc } from './feed.js';

/* ── Scene lookup for a given message ── */

export function getMsgScenes(dev, msgName) {
  const found = [];
  if (!dev || !dev.scenes) return found;
  Object.keys(dev.scenes).forEach(function (sname) {
    const msgs = (dev.scenes[sname].msgs || "").replace(/\+/g, ",");
    const list = msgs.split(",").map(function (s) { return s.trim(); });
    if (list.indexOf(msgName) !== -1) found.push(sname);
  });
  if (found.length === 0) {
    const sc = dev.messages && dev.messages[msgName] && dev.messages[msgName].scene;
    if (sc) found.push(sc);
  }
  return found;
}

/* ── Format gate config string for display ── */

export function buildGateStr(m) {
  if (m.gate_src || m.gate_source) {
    const gs = m.gate_src || m.gate_source;
    const gm = m.gate_mode || "";
    let s = gm + ":" + gs;
    const isEdge = (gm === "rising" || gm === "falling");
    if (m.gate_lo != null && m.gate_lo !== "") s += (isEdge ? " trigger:" : " \u2265") + m.gate_lo;
    if (m.gate_hi != null && m.gate_hi !== "") s += (isEdge ? " delta:" : " \u2264") + m.gate_hi;
    return s;
  }
  if (m.ori_only || m.orionly) return "only:ori:" + (m.ori_only || m.orionly);
  if (m.ori_not  || m.orinot)  return "not:ori:"  + (m.ori_not  || m.orinot);
  if (m.ternori)                return "toggle:ori:" + m.ternori;
  return "";
}

/* ── Render message table ── */

export function renderMsgTable() {
  const dev = getActiveDev();
  const tbody = $("#msgTableBody");
  tbody.innerHTML = "";
  if (!dev || Object.keys(dev.messages).length === 0) {
    tbody.innerHTML = '<tr><td colspan="4"><div class="empty-state"><div class="empty-icon">○</div><div class="empty-text">No messages tracked yet.</div><div class="empty-sub">Click the device tab → Query to load from device, or create one below.</div></div></td></tr>';
    return;
  }
  Object.keys(dev.messages).forEach(function (name) {
    const m = dev.messages[name];
    const sensor  = esc(m.value || m.val || "—");
    const low     = m.low  || m.min || "";
    const high    = m.high || m.max || "";
    const range   = (low !== "" && high !== "") ? esc(low) + " \u2192 " + esc(high) : "";
    const ip      = esc(m.ip  || "—");
    const port    = esc(m.port || "—");
    const adr     = esc(m.adr || m.addr || m.address || "—");
    const gateStr = buildGateStr(m);
    const scenes  = getMsgScenes(dev, name);
    const isSoloed = !!m.soloed;

    const sceneTags = scenes.map(function (s) {
      return '<span class="msg-stag">' + esc(s) + '</span>';
    }).join("") || '<span style="color:var(--text-light);font-size:12px">—</span>';

    /* ── data row ── */
    const tr = document.createElement("tr");
    tr.className = "msg-data-row";
    tr.id = "mr-" + name;
    tr.dataset.msgName = name;
    tr.dataset.msgScenes = scenes.join("\t");
    tr.innerHTML =
      '<td><span class="msg-name">' + esc(name) + '</span></td>' +
      '<td><div class="msg-scene-tags">' + sceneTags + '</div></td>' +
      '<td><div class="msg-sensor-cell">' +
        '<span class="msg-sensor-name">' + sensor + '</span>' +
        (range ? '<span class="msg-sensor-range">' + range + '</span>' : '') +
      '</div></td>' +
      '<td><div class="msg-solo-cell" onclick="event.stopPropagation()">' +
        '<button class="msg-btn-solo' + (isSoloed ? ' soloed' : '') + '" data-msgname="' + esc(name) + '">' +
          'solo' +
        '</button>' +
      '</div></td>';
    tr.addEventListener("click", function (e) {
      if (e.metaKey || e.ctrlKey) { populateMsgForm(name, m); return; }
      toggleMsgExp(name);
    });
    tr.querySelector(".msg-btn-solo").addEventListener("click", function (e) {
      const btn = e.currentTarget;
      const active = btn.classList.toggle("soloed");
      m.soloed = active;
      msgAction(active ? "solo" : "unsolo", name);
    });

    /* ── expand row ── */
    const expTr = document.createElement("tr");
    expTr.className = "msg-exp-row";
    expTr.id = "me-" + name;
    expTr.innerHTML =
      '<td colspan="4"><div class="msg-exp-inner" id="mei-' + esc(name) + '">' +
        '<span class="msg-exp-item"><span class="msg-exp-label">ip</span><span class="msg-exp-val">' + ip + '</span></span>' +
        '<span class="msg-exp-item"><span class="msg-exp-label">port</span><span class="msg-exp-val">' + port + '</span></span>' +
        '<span class="msg-exp-item"><span class="msg-exp-label">address</span><span class="msg-exp-val">' + adr + '</span></span>' +
        (gateStr ? '<span class="msg-exp-item"><span class="msg-exp-label">gate</span><span class="msg-exp-val">' + esc(gateStr) + '</span></span>' : '') +
        '<span class="msg-exp-item msg-exp-actions" onclick="event.stopPropagation()">' +
          '<button class="msg-exp-btn" data-act="enable">enable</button><span class="msg-exp-sep">·</span>' +
          '<button class="msg-exp-btn" data-act="disable">disable</button><span class="msg-exp-sep">·</span>' +
          '<button class="msg-exp-btn" data-act="info">resync?</button><span class="msg-exp-sep">·</span>' +
          '<button class="msg-exp-btn" data-act="save">save</button><span class="msg-exp-sep">·</span>' +
          '<button class="msg-exp-btn" data-act="edit">edit</button><span class="msg-exp-sep">·</span>' +
          '<button class="msg-exp-btn danger" data-act="delete">delete</button>' +
        '</span>' +
      '</div></td>';
    expTr.querySelectorAll("[data-act]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        const act = btn.dataset.act;
        if (act === "edit") { populateMsgForm(name, m); }
        else { msgAction(act, name); }
      });
    });

    tbody.appendChild(tr);
    tbody.appendChild(expTr);
  });
  renderMsgSceneFilter();
  if (window._refreshGateMsgSources) window._refreshGateMsgSources();
}

/* ── Message scene filter ── */

let _msgSceneFilter = new Set();

export function getMsgSceneFilter() { return _msgSceneFilter; }

export function renderMsgSceneFilter() {
  const filterBar = $("#msgSceneFilter");
  if (!filterBar) return;
  const dev = getActiveDev();
  const sceneNames = dev ? Object.keys(dev.scenes) : [];

  // Remove old scene chips (keep label + All button)
  Array.from(filterBar.querySelectorAll(".msg-filter-chip:not(.msg-filter-all)")).forEach(function (c) { c.remove(); });

  if (sceneNames.length === 0) {
    filterBar.style.display = "none";
    _msgSceneFilter.clear();
    return;
  }
  filterBar.style.display = "flex";

  sceneNames.forEach(function (sname) {
    const chip = document.createElement("button");
    chip.className = "msg-filter-chip" + (_msgSceneFilter.has(sname) ? " active" : "");
    chip.dataset.filter = sname;
    chip.textContent = sname;
    chip.addEventListener("click", function () {
      if (_msgSceneFilter.has(sname)) {
        _msgSceneFilter.delete(sname);
      } else {
        _msgSceneFilter.add(sname);
      }
      applyMsgSceneFilter();
    });
    filterBar.appendChild(chip);
  });

  // All button clears filter
  const allBtn = filterBar.querySelector(".msg-filter-all");
  if (allBtn) {
    allBtn.onclick = function () {
      _msgSceneFilter.clear();
      applyMsgSceneFilter();
    };
  }

  applyMsgSceneFilter();
}

export function applyMsgSceneFilter() {
  const filterBar = $("#msgSceneFilter");
  const allBtn = filterBar ? filterBar.querySelector(".msg-filter-all") : null;
  const isAll = _msgSceneFilter.size === 0;

  // Update chip active states
  if (filterBar) {
    filterBar.querySelectorAll(".msg-filter-chip").forEach(function (c) {
      if (c.classList.contains("msg-filter-all")) {
        c.classList.toggle("active", isAll);
      } else {
        c.classList.toggle("active", _msgSceneFilter.has(c.dataset.filter));
      }
    });
  }

  // Show/hide rows
  document.querySelectorAll(".msg-data-row, .msg-exp-row").forEach(function (row) {
    const dataRow = row.classList.contains("msg-data-row") ? row : document.getElementById("mr-" + row.id.replace("me-", ""));
    if (!dataRow) return;
    const rowScenes = (dataRow.dataset.msgScenes || "").split("\t").filter(Boolean);
    const visible = isAll || rowScenes.some(function (s) { return _msgSceneFilter.has(s); });
    row.style.display = visible ? "" : "none";
  });
}

export function toggleMsgExp(name) {
  const dataRow = document.getElementById("mr-" + name);
  const expRow  = document.getElementById("me-" + name);
  if (!dataRow || !expRow) return;
  const isOpen = expRow.classList.contains("visible");
  expRow.classList.toggle("visible", !isOpen);
  dataRow.classList.toggle("open", !isOpen);
}

/* ── Sensor categories and hints ── */

export const SENSOR_CATEGORIES = [
  { id: "acceleration", label: "Acceleration", hint: "Linear acceleration in device, world, and limb frames",
    sensors: [
      { value: "accelX", label: "Raw X", hint: "Device-frame left/right" },
      { value: "accelY", label: "Raw Y", hint: "Device-frame up/down" },
      { value: "accelZ", label: "Raw Z", hint: "Device-frame forward/back" },
      { value: "gaccelX", label: "Global X", hint: "World X axis (gravity-corrected)" },
      { value: "gaccelY", label: "Global Y", hint: "World Y axis (gravity-corrected)" },
      { value: "gaccelZ", label: "Global Z", hint: "World Z axis (gravity-corrected)" },
      { value: "limbFwd", label: "Limb Forward", hint: "Along limb direction (forward/back)" },
      { value: "limbLat", label: "Limb Lateral", hint: "Lateral to limb (sideways)" },
      { value: "limbVert", label: "Limb Vertical", hint: "Vertical relative to limb (up/down)" },
      { value: "twitch", label: "Twitch", hint: "Overall movement intensity (acceleration magnitude)" }
    ]},
  { id: "orientation", label: "Orientation", hint: "Rotation angles \u2014 Euler angles and swing-twist (limb projected)",
    sensors: [
      { value: "roll", label: "Roll (Euler)", hint: "Tilt left/right \u2014 subject to gimbal lock" },
      { value: "pitch", label: "Pitch (Euler)", hint: "Tilt forward/back \u2014 subject to gimbal lock" },
      { value: "yaw", label: "Yaw (Euler)", hint: "Compass heading \u2014 subject to gimbal lock" },
      { value: "twist", label: "Twist (Swing-Twist)", hint: "Wrist rotation around limb axis" },
      { value: "azi", label: "Azimuth (Swing-Twist)", hint: "Horizontal pointing direction" },
      { value: "tilt", label: "Tilt (Swing-Twist)", hint: "Vertical angle above/below horizon" }
    ]},
  { id: "gyroscope", label: "Gyroscope", hint: "Rotational velocity — raw sensor axes and swing-twist angle rates",
    sensors: [
      { value: "gyroX", label: "Raw X", hint: "Rolling" },
      { value: "gyroY", label: "Raw Y", hint: "Pitching" },
      { value: "gyroZ", label: "Raw Z", hint: "Spinning" },
      { value: "gyroLength", label: "Magnitude", hint: "Overall rotation speed" },
      { value: "twistVel", label: "Twist Rate", hint: "Angular velocity of wrist twist (±360 deg/s)" },
      { value: "aziVel",   label: "Azimuth Rate", hint: "Angular velocity of horizontal direction (±360 deg/s)" },
      { value: "tiltVel",  label: "Tilt Rate", hint: "Angular velocity of vertical angle (±360 deg/s)" }
    ]},
  { id: "barometer", label: "Barometer", hint: "Barometric pressure sensor",
    sensors: [
      { value: "baro", label: "Elevation", hint: "Altitude / air pressure" }
    ]},
  { id: "quaternion", label: "Quaternion", hint: "Raw quaternion components \u2014 advanced rotation math", advanced: true,
    sensors: [
      { value: "quatI", label: "Quaternion I", hint: "I component" },
      { value: "quatJ", label: "Quaternion J", hint: "J component" },
      { value: "quatK", label: "Quaternion K", hint: "K component" },
      { value: "quatR", label: "Quaternion R", hint: "R (scalar) component" }
    ]},
  { id: "constants", label: "Constants", hint: "Fixed values for output bounds",
    sensors: [
      { value: "high",   label: "High",   hint: "Always returns the high bound" },
      { value: "low",    label: "Low",    hint: "Always returns the low bound" },
      { value: "string", label: "String", hint: "Send a fixed text string — stored on the device and sent as an OSC string argument" }
    ]}
];

// Build flat lookup for backward compat (value → {category hint, sensor hint})
export const SENSOR_HINTS = {};
const SENSOR_TO_CAT = {};
SENSOR_CATEGORIES.forEach(function (cat) {
  cat.sensors.forEach(function (s) {
    SENSOR_HINTS[s.value] = s.hint;
    SENSOR_TO_CAT[s.value] = cat.id;
  });
});

/* ── Sensor picker factory ── */

export function initSensorPicker(catId, valId, catHintId, valHintId) {
  const catEl = $("#" + catId), valEl = $("#" + valId);
  const catHint = $("#" + catHintId), valHint = $("#" + valHintId);
  if (!catEl || !valEl) return;

  // Populate category dropdown
  SENSOR_CATEGORIES.forEach(function (cat) {
    if (cat.advanced) return; // hidden by default
    const opt = document.createElement("option");
    opt.value = cat.id; opt.textContent = cat.label;
    if (cat.advanced) { opt.className = "quat-cat-option"; opt.hidden = true; }
    catEl.appendChild(opt);
  });

  function populateSensors(catIdVal) {
    valEl.innerHTML = "";
    const none = document.createElement("option");
    none.value = ""; none.textContent = "\u2014 pick \u2014";
    valEl.appendChild(none);
    const cat = SENSOR_CATEGORIES.filter(function (c) { return c.id === catIdVal; })[0];
    if (!cat) return;
    cat.sensors.forEach(function (s) {
      const opt = document.createElement("option");
      opt.value = s.value; opt.textContent = s.label;
      valEl.appendChild(opt);
    });
    if (catHint) catHint.textContent = cat.hint || "";
    if (valHint) valHint.textContent = "";
  }

  catEl.addEventListener("change", function () {
    populateSensors(catEl.value);
  });
  valEl.addEventListener("change", function () {
    if (valHint) valHint.textContent = SENSOR_HINTS[valEl.value] || "";
  });

  // Return helpers for programmatic set
  return {
    setValue: function (sensorValue) {
      const catIdVal = SENSOR_TO_CAT[sensorValue] || "";
      if (catIdVal) {
        // Ensure cat option exists (for advanced)
        if (!catEl.querySelector('option[value="' + catIdVal + '"]')) {
          const cat = SENSOR_CATEGORIES.filter(function (c) { return c.id === catIdVal; })[0];
          if (cat) {
            const opt = document.createElement("option");
            opt.value = cat.id; opt.textContent = cat.label;
            catEl.appendChild(opt);
          }
        }
        catEl.value = catIdVal;
        populateSensors(catIdVal);
        valEl.value = sensorValue;
        if (valHint) valHint.textContent = SENSOR_HINTS[sensorValue] || "";
      } else {
        catEl.value = "";
        valEl.innerHTML = '<option value="">\u2014 pick sensor \u2014</option>';
        if (catHint) catHint.textContent = "";
        if (valHint) valHint.textContent = "";
      }
    },
    clear: function () {
      catEl.value = "";
      valEl.innerHTML = '<option value="">\u2014 pick sensor \u2014</option>';
      if (catHint) catHint.textContent = "";
      if (valHint) valHint.textContent = "";
    }
  };
}

export const msgPicker = initSensorPicker("msgCategory", "msgValue", "msgCategoryHint", "msgValueHint");
export const directPicker = initSensorPicker("directCategory", "directValue", "directCategoryHint", "directValueHint");

/* ── String mode: show/hide Low/High vs String input ── */

export function updateStringMode() {
  const isString = ($("#msgValue").value === "string");
  const loEl     = $("#msgLow"), hiEl = $("#msgHigh");
  const loFg     = loEl ? loEl.closest(".form-group") : null;
  const hiFg     = hiEl ? hiEl.closest(".form-group") : null;
  const strGrp   = $("#msgStringGroup");
  if (loFg)  loFg.style.display  = isString ? "none" : "";
  if (hiFg)  hiFg.style.display  = isString ? "none" : "";
  if (strGrp) strGrp.style.display = isString ? "" : "none";
}

(function () {
  const _msgValueEl = $("#msgValue");
  if (_msgValueEl) _msgValueEl.addEventListener("change", updateStringMode);
  updateStringMode();
}());

/* ── String registration (send string to device, get back str name) ── */

let _strRegisterCallback = null;

export function setStrRegisterCallback(cb) { _strRegisterCallback = cb; }

export function getStrRegisterCallback() { return _strRegisterCallback; }

export function registerString(strValue) {
  return new Promise(function (resolve, reject) {
    _strRegisterCallback = resolve;
    const _timeout = setTimeout(function () {
      if (_strRegisterCallback === resolve) {
        _strRegisterCallback = null;
        reject(new Error("String registration timeout"));
      }
    }, 4000);
    sendCmd(addr("/annieData/{device}/msg/string"), strValue).catch(function () {
      clearTimeout(_timeout);
      if (_strRegisterCallback === resolve) { _strRegisterCallback = null; }
      reject(new Error("Send failed"));
    });
  });
}

/* ── Gate picker (flat source dropdown) factory ── */

export function initGatePicker(sourceId, oriId, modeId, loId, hiId, hintId, oriGroupId, loGroupId, hiGroupId, opts) {
  const srcEl = $("#" + sourceId), oriEl = $("#" + oriId);
  const modeEl = $("#" + modeId), loEl = $("#" + loId), hiEl = $("#" + hiId);
  const hintEl = $("#" + hintId);
  const oriGroup = $("#" + oriGroupId), loGroup = $("#" + loGroupId), hiGroup = $("#" + hiGroupId);
  if (!srcEl || !modeEl) return null;

  opts = opts || {};
  const isScenePicker = !!opts.scene;

  // Labels that change depending on mode
  const loLabel = loGroup ? loGroup.querySelector("label") : null;
  const hiLabel = hiGroup ? hiGroup.querySelector("label") : null;

  // Populate flat source dropdown: none, Orientation, then all data streams grouped by category
  const noneOpt = document.createElement("option");
  noneOpt.value = ""; noneOpt.textContent = "\u2014 none \u2014";
  srcEl.appendChild(noneOpt);
  const oriOpt = document.createElement("option");
  oriOpt.value = "ori"; oriOpt.textContent = "Ori";
  srcEl.appendChild(oriOpt);
  SENSOR_CATEGORIES.forEach(function (cat) {
    if (cat.advanced) return;
    const grp = document.createElement("optgroup");
    grp.label = cat.label;
    cat.sensors.forEach(function (s) {
      const opt = document.createElement("option");
      opt.value = s.value; opt.textContent = s.label;
      grp.appendChild(opt);
    });
    srcEl.appendChild(grp);
  });

  // Message-value gate sources (optgroup, populated dynamically)
  const msgOptGroup = document.createElement("optgroup");
  msgOptGroup.label = "Message Values";
  msgOptGroup.className = "gate-msg-sources";
  msgOptGroup.style.display = "none";
  srcEl.appendChild(msgOptGroup);

  function isEdgeMode() {
    const m = modeEl.value;
    return m === "rising" || m === "falling";
  }

  function updateLabels() {
    if (!isScenePicker) return;
    const edge = isEdgeMode();
    if (loLabel) loLabel.textContent = edge ? "Trigger" : "Lower";
    if (hiLabel) hiLabel.textContent = edge ? "Delta" : "Upper";
    if (loEl) loEl.placeholder = edge ? "threshold" : "\u2265";
    if (hiEl) hiEl.placeholder = edge ? "min \u0394" : "\u2264";
  }

  function updateVisibility() {
    const v = srcEl.value;
    const isOri = (v === "ori");
    if (isOri) {
      oriGroup.style.display = "";
      loGroup.style.display = "none";
      hiGroup.style.display = "none";
      if (hintEl) hintEl.textContent = "Gate based on which ori is currently active";
    } else if (v) {
      oriGroup.style.display = "none";
      loGroup.style.display = "";
      hiGroup.style.display = "";
      if (v.indexOf("msg:") === 0) {
        if (hintEl) hintEl.textContent = "Gate using scaled output of message: " + v.substring(4);
      } else {
        if (hintEl) hintEl.textContent = SENSOR_HINTS[v] || "";
      }
    } else {
      oriGroup.style.display = "none";
      loGroup.style.display = "none";
      hiGroup.style.display = "none";
      if (hintEl) hintEl.textContent = "";
    }
    /* Rising/Falling are non-ori only — hide them when ori is selected */
    if (isScenePicker) {
      const risingOpt  = modeEl.querySelector('option[value="rising"]');
      const fallingOpt = modeEl.querySelector('option[value="falling"]');
      if (risingOpt)  risingOpt.disabled  = isOri;
      if (fallingOpt) fallingOpt.disabled = isOri;
      if (isOri && isEdgeMode()) modeEl.value = "";
    }
    updateLabels();
  }

  srcEl.addEventListener("change", updateVisibility);
  modeEl.addEventListener("change", function () { updateLabels(); });
  updateVisibility();

  return {
    getConfig: function () {
      const mode = modeEl.value;
      if (!mode) return null;
      let src;
      if (srcEl.value === "ori") {
        const oriName = oriEl.value.trim();
        if (!oriName) return null;
        src = "ori:" + oriName;
      } else {
        src = srcEl.value;
        if (!src) return null;
      }
      const lo = loEl.value.trim();
      const hi = hiEl.value.trim();
      return { gate_src: src, gate_mode: mode, gate_lo: lo, gate_hi: hi };
    },
    setValue: function (gateSrc, gateMode, gateLo, gateHi) {
      if (!gateSrc || !gateMode) { this.clear(); return; }
      modeEl.value = gateMode;
      if (gateSrc.indexOf("ori:") === 0) {
        srcEl.value = "ori";
        oriEl.value = gateSrc.substring(4);
      } else if (gateSrc.indexOf("msg:") === 0) {
        // Ensure the msg option exists in the optgroup
        let found = false;
        for (let i = 0; i < msgOptGroup.children.length; i++) {
          if (msgOptGroup.children[i].value === gateSrc) { found = true; break; }
        }
        if (!found) {
          const opt = document.createElement("option");
          opt.value = gateSrc; opt.textContent = gateSrc.substring(4);
          msgOptGroup.appendChild(opt);
          msgOptGroup.style.display = "";
        }
        srcEl.value = gateSrc;
      } else {
        srcEl.value = gateSrc;
      }
      loEl.value = (gateLo != null && gateLo !== "" && !isNaN(gateLo)) ? gateLo : "";
      hiEl.value = (gateHi != null && gateHi !== "" && !isNaN(gateHi)) ? gateHi : "";
      updateVisibility();
    },
    clear: function () {
      srcEl.value = "";
      oriEl.value = "";
      modeEl.value = "";
      loEl.value = "";
      hiEl.value = "";
      updateVisibility();
    },
    refreshMsgSources: function (msgNames) {
      msgOptGroup.innerHTML = "";
      if (msgNames && msgNames.length > 0) {
        msgNames.forEach(function (n) {
          const opt = document.createElement("option");
          opt.value = "msg:" + n; opt.textContent = n;
          msgOptGroup.appendChild(opt);
        });
        msgOptGroup.style.display = "";
      } else {
        msgOptGroup.style.display = "none";
      }
    }
  };
}

export const msgGatePicker = initGatePicker(
  "msgGateSource", "msgGateOri", "msgGateMode",
  "msgGateLo", "msgGateHi", "msgGateHint",
  "msgGateOriGroup", "msgGateLoGroup", "msgGateHiGroup"
);

/* ── Stub for refreshing gate message sources (called from settings) ── */

export function refreshGateMsgSources() {
  if (window._refreshGateMsgSources) window._refreshGateMsgSources();
}

/* ── Populate message form for editing ── */

export function populateMsgForm(name, m) {
  $("#msgName").value = name;
  const valStr = m.value || m.val || "";
  // If value is a string pool reference (str1, str2, ...) show String category
  if (/^str\d+$/i.test(valStr)) {
    if (msgPicker) msgPicker.setValue("string");
    const strInp = $("#msgStringVal");
    if (strInp) strInp.value = m.string_val || valStr;
  } else {
    if (msgPicker) msgPicker.setValue(valStr);
    const strInp2 = $("#msgStringVal");
    if (strInp2) strInp2.value = "";
  }
  updateStringMode();
  $("#msgIP").value = m.ip || "";
  $("#msgPort").value = m.port || "9000";
  $("#msgAdr").value = m.adr || m.addr || m.address || "";
  $("#msgLow").value = m.low || m.min || "";
  $("#msgHigh").value = m.high || m.max || "";
  $("#msgScene").value = m.scene || "";
  // Gate fields — handle both new gate_* keys and legacy ori_only/ori_not/ternori
  if (msgGatePicker) {
    let gs = m.gate_src || m.gate_source || "";
    let gm = m.gate_mode || "";
    const gl = m.gate_lo != null ? m.gate_lo : "";
    const gh = m.gate_hi != null ? m.gate_hi : "";
    // Legacy backward compat
    if (!gs && (m.ori_only || m.orionly)) { gs = "ori:" + (m.ori_only || m.orionly); gm = "only"; }
    else if (!gs && (m.ori_not || m.orinot)) { gs = "ori:" + (m.ori_not || m.orinot); gm = "not"; }
    else if (!gs && m.ternori) { gs = "ori:" + m.ternori; gm = "toggle"; }
    msgGatePicker.setValue(gs, gm, gl, gh);
    // Auto-show gate section if gate is populated
    if (gs && gm) {
      const sec = $("#msgGateSection"); if (sec) sec.style.display = "";
      const chk = $("#chkShowGate"); if (chk) chk.checked = true;
    }
  }
  if (typeof window.updateMsgPreview === "function") window.updateMsgPreview();
  /* scroll to form — switch to messages tab */
  $(".nav-btn[data-section='messages']").click();
  $("#msgName").focus();
}

/* ── Message actions (enable, disable, solo, delete, etc.) ── */

export function msgAction(act, name) {
  let template;
  switch (act) {
    case "info":    template = "/annieData/{device}/msg/{name}/info"; break;
    case "enable":  template = "/annieData/{device}/msg/{name}/enable"; break;
    case "disable": template = "/annieData/{device}/msg/{name}/disable"; break;
    case "solo":    template = "/annieData/{device}/msg/{name}/solo"; break;
    case "unsolo":  template = "/annieData/{device}/msg/{name}/unsolo"; break;
    case "save":    sendCmd(addr("/annieData/{device}/save/msg"), name); return;
    case "delete":  template = "/annieData/{device}/msg/{name}/delete"; break;
    default: return;
  }
  sendCmd(addr(template, name), null).then(function (res) {
    if (res.status === "ok") {
      toast(act + ": " + name, "success");
      if (act === "delete") {
        const dev = getActiveDev();
        if (dev) { delete dev.messages[name]; renderMsgTable(); if (typeof window.refreshAllDropdowns === "function") window.refreshAllDropdowns(); }
      }
    }
  });
}
