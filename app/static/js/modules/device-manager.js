/* ── Device management (multi-device) ── */

import { devices, activeDevice, $, $$ } from "./state.js";
import { toast } from "./toast.js";

/* ── Late-binding callbacks (set by other modules to avoid circular deps) ── */
let _renderMsgTable, _renderSceneTable, _renderOriTable,
    _refreshAllDropdowns, _refreshQueryDeviceSelect, _openDevDropdown;

export function setDeviceCallbacks({ renderMsgTable, renderSceneTable, renderOriTable, refreshAllDropdowns, refreshQueryDeviceSelect, openDevDropdown }) {
  _renderMsgTable = renderMsgTable;
  _renderSceneTable = renderSceneTable;
  _renderOriTable = renderOriTable;
  _refreshAllDropdowns = refreshAllDropdowns;
  _refreshQueryDeviceSelect = refreshQueryDeviceSelect;
  _openDevDropdown = openDevDropdown;
}

export function generateDeviceId(host, port, name) {
  return name + "@" + host + ":" + port;
}

/** Persist device connection info to localStorage. */
export function saveDevicesToStorage() {
  try {
    const data = Object.keys(devices).map(function (id) {
      const d = devices[id];
      return { host: d.host, port: d.port, name: d.name };
    });
    localStorage.setItem("gooey_devices", JSON.stringify(data));
    localStorage.setItem("gooey_active_device", activeDevice.id);
  } catch (e) { /* storage full or unavailable */ }
}

/** Restore devices from localStorage on page load. */
export function restoreDevicesFromStorage() {
  try {
    const raw = localStorage.getItem("gooey_devices");
    if (!raw) return;
    const list = JSON.parse(raw);
    list.forEach(function (d) {
      if (d.host && d.port && d.name) {
        const id = generateDeviceId(d.host, d.port, d.name);
        if (!devices[id]) {
          devices[id] = { host: d.host, port: parseInt(d.port, 10), name: d.name, messages: {}, scenes: {}, oris: {} };
        }
      }
    });
    const savedActive = localStorage.getItem("gooey_active_device");
    if (savedActive && devices[savedActive]) {
      activeDevice.id = savedActive;
    } else {
      const keys = Object.keys(devices);
      if (keys.length > 0) activeDevice.id = keys[0];
    }
  } catch (e) { /* corrupt data */ }
}

export function addDevice(host, port, name) {
  const id = generateDeviceId(host, port, name);
  if (!devices[id]) {
    devices[id] = { host: host, port: parseInt(port, 10), name: name, messages: {}, scenes: {}, oris: {} };
  }
  activeDevice.id = id;
  renderDeviceTabs();
  _refreshAllDropdowns && _refreshAllDropdowns();
  saveDevicesToStorage();
  return id;
}

export function removeDevice(id) {
  delete devices[id];
  if (activeDevice.id === id) {
    const keys = Object.keys(devices);
    activeDevice.id = keys.length > 0 ? keys[0] : "";
  }
  renderDeviceTabs();
  _renderMsgTable && _renderMsgTable();
  _renderSceneTable && _renderSceneTable();
  _renderOriTable && _renderOriTable();
  _refreshAllDropdowns && _refreshAllDropdowns();
  saveDevicesToStorage();
}

export function getActiveDev() {
  return devices[activeDevice.id] || null;
}

export function devHost() { const d = getActiveDev(); return d ? d.host : "127.0.0.1"; }
export function devPort() { const d = getActiveDev(); return d ? d.port : 8000; }
export function devName() { const d = getActiveDev(); return d ? d.name : ""; }

/* ── Device tab rendering ── */
export function renderDeviceTabs() {
  const container = $("#devTabsWrap") || $("#hdrDevices");
  const devActions = $("#hdrDevActions");
  /* Remove existing tabs */
  $$(".dev-tab[data-device-id]").forEach(function (t) { t.remove(); });
  const devCount = Object.keys(devices).length;
  Object.keys(devices).forEach(function (id) {
    const d = devices[id];
    const isActive = (id === activeDevice.id);
    const btn = document.createElement("button");
    btn.className = "dev-tab" + (isActive ? " active" : "");
    btn.dataset.deviceId = id;
    /* Build button content: status dot + sanitised device name + caret */
    const dot = document.createElement("span");
    dot.className = "dev-dot";
    dot.title = isActive ? "Active device" : "Inactive";
    btn.appendChild(dot);
    const label = document.createElement("span");
    label.className = "tab-label";
    label.textContent = d.name;
    btn.appendChild(label);
    const caret = document.createElement("span");
    caret.className = "tab-caret";
    caret.textContent = "▾";
    btn.appendChild(caret);
    /* Right-click opens dropdown directly */
    btn.addEventListener("contextmenu", function (e) {
      e.preventDefault();
      activeDevice.id = id;
      renderDeviceTabs();
      _refreshAllDropdowns && _refreshAllDropdowns();
      _refreshQueryDeviceSelect && _refreshQueryDeviceSelect();
      const freshBtn = document.querySelector('.dev-tab[data-device-id="' + id + '"]');
      if (freshBtn) _openDevDropdown && _openDevDropdown(freshBtn, id);
    });
    /* Plain click: just select the device, no dropdown */
    btn.addEventListener("click", function (e) {
      activeDevice.id = id;
      renderDeviceTabs();
      _renderMsgTable && _renderMsgTable();
      _renderSceneTable && _renderSceneTable();
      _renderOriTable && _renderOriTable();
      _refreshAllDropdowns && _refreshAllDropdowns();
      _refreshQueryDeviceSelect && _refreshQueryDeviceSelect();
    });
    /* Caret click: open the per-device dropdown without re-triggering selection logic */
    caret.addEventListener("click", function (e) {
      e.stopPropagation();
      activeDevice.id = id;
      renderDeviceTabs();
      _refreshAllDropdowns && _refreshAllDropdowns();
      _refreshQueryDeviceSelect && _refreshQueryDeviceSelect();
      const freshBtn = document.querySelector('.dev-tab[data-device-id="' + id + '"]');
      if (freshBtn) _openDevDropdown && _openDevDropdown(freshBtn, id);
    });
    container.insertBefore(btn, devActions);
  });
  /* Show/hide welcome banner based on device count */
  const wb = $("#welcomeBanner");
  if (wb) wb.style.display = devCount === 0 ? "" : "none";
  /* Scale font size up when few devices; wrap to 2 rows when space saturated */
  if (container) {
    const fontSize = devCount <= 2 ? "15px" : devCount <= 4 ? "14px" : "13px";
    container.style.setProperty("--dev-tab-font", fontSize);
    container.classList.remove("wrap-2row");
    requestAnimationFrame(function () {
      if (container.scrollWidth > container.clientWidth + 4) {
        container.classList.add("wrap-2row");
      }
    });
  }
  /* Update feed device filter */
  const sel = $("#feedDeviceFilter");
  const curVal = sel.value;
  sel.innerHTML = '<option value="">All devices</option>';
  Object.keys(devices).forEach(function (id) {
    const d = devices[id];
    const opt = document.createElement("option");
    opt.value = d.name;
    opt.textContent = d.name;
    sel.appendChild(opt);
  });
  sel.value = curVal;
  /* Sync the header query-device select */
  _refreshQueryDeviceSelect && _refreshQueryDeviceSelect();
}

/* ── Restore devices from previous session ── */
restoreDevicesFromStorage();
renderDeviceTabs();
