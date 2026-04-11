/* ── Device configuration modal & per-device dropdown ── */

import { devices, activeDevice, $ } from "./state.js";
import { toast } from "./toast.js";
import { api } from "./api.js";
import { addDevice, removeDevice, generateDeviceId, getActiveDev, renderDeviceTabs } from "./device-manager.js";

/* ── Late-binding callbacks for render functions from other modules ── */
let _renderMsgTable, _renderSceneTable, _renderOriTable;

export function setDeviceConfigCallbacks({ renderMsgTable, renderSceneTable, renderOriTable }) {
  _renderMsgTable = renderMsgTable;
  _renderSceneTable = renderSceneTable;
  _renderOriTable = renderOriTable;
}

/* ── IP resolver: type "me" to use this computer's IP ── */
let _myIpCache = null;

export function resolveIp(host, cb) {
  if (host.trim().toLowerCase() !== "me") { cb(host.trim()); return; }
  if (_myIpCache) { cb(_myIpCache); return; }
  fetch("/api/my-ip")
    .then(function (r) { return r.json(); })
    .then(function (d) { _myIpCache = d.ip || "127.0.0.1"; cb(_myIpCache); })
    .catch(function () { cb("127.0.0.1"); });
}

/* Auto-resolve "me" in any IP input field on blur */
["statusIP", "msgIP", "directIP", "rawHost", "bridgeOutHost", "sceneIP"].forEach(function (fieldId) {
  const el = $("#" + fieldId);
  if (!el) return;
  el.addEventListener("blur", function () {
    if (el.value.trim().toLowerCase() === "me") {
      resolveIp("me", function (ip) { el.value = ip; });
    }
  });
});

/* ── Device config modal (add / edit) ── */
let _deviceConfigMode = "add";
let _deviceConfigEditId = "";

export function openDeviceConfigModal(mode, deviceId) {
  _deviceConfigMode = mode || "add";
  _deviceConfigEditId = deviceId || "";
  const titleEl = $("#deviceConfigTitle");
  const saveBtn = $("#deviceConfigSave");
  const ipEl    = $("#deviceConfigIP");
  const portEl  = $("#deviceConfigPort");
  const nameEl  = $("#deviceConfigName");
  if (mode === "edit" && deviceId && devices[deviceId]) {
    const d = devices[deviceId];
    if (titleEl) titleEl.textContent = "Edit Device";
    if (saveBtn) saveBtn.textContent = "Update";
    if (ipEl)   ipEl.value   = d.host;
    if (portEl) portEl.value = d.port;
    if (nameEl) nameEl.value = d.name;
  } else {
    if (titleEl) titleEl.textContent = "Add Device";
    if (saveBtn) saveBtn.textContent = "Add Device";
    if (ipEl)   ipEl.value   = "192.168.1.100";
    if (portEl) portEl.value = "8000";
    if (nameEl) nameEl.value = "";
  }
  $("#deviceConfigModal").classList.remove("hidden");
  setTimeout(function () { if (nameEl) nameEl.focus(); }, 50);
}

/* ── Modal event wiring (IIFE) ── */
(function () {
  const modal   = $("#deviceConfigModal");
  const saveBtn = $("#deviceConfigSave");
  const cancelBtn = $("#deviceConfigCancel");

  if (cancelBtn) cancelBtn.addEventListener("click", function () {
    modal.classList.add("hidden");
  });
  if (modal) modal.addEventListener("click", function (e) {
    if (e.target === modal) modal.classList.add("hidden");
  });

  if (saveBtn) saveBtn.addEventListener("click", function () {
    const host = ($("#deviceConfigIP").value || "").trim();
    const port = ($("#deviceConfigPort").value || "").trim();
    const name = ($("#deviceConfigName").value || "").trim();
    if (!host) { toast("IP required", "error"); return; }
    if (!port) { toast("Port required", "error"); return; }
    if (!name) { toast("Name required", "error"); return; }
    modal.classList.add("hidden");
    resolveIp(host, function (resolvedHost) {
      if (_deviceConfigMode === "edit" && _deviceConfigEditId) {
        const wasActive = (activeDevice.id === _deviceConfigEditId);
        delete devices[_deviceConfigEditId];
        if (wasActive) activeDevice.id = "";
      }
      addDevice(resolvedHost, parseInt(port, 10), name);
      _renderMsgTable && _renderMsgTable();
      _renderSceneTable && _renderSceneTable();
      _renderOriTable && _renderOriTable();
      toast("Device " + (_deviceConfigMode === "edit" ? "updated" : "added") + ": " + name, "success");
    });
  });

  /* Enter key submits */
  [$("#deviceConfigIP"), $("#deviceConfigPort"), $("#deviceConfigName")].forEach(function (el) {
    if (el) el.addEventListener("keydown", function (e) {
      if (e.key === "Enter") { e.preventDefault(); saveBtn && saveBtn.click(); }
    });
  });
}());

/* ── Edit existing device ── */
export function editDevice(id) {
  openDeviceConfigModal("edit", id);
}

/* ── Add-device button ── */
const _btnAdd = $("#btnAddDevice");
if (_btnAdd) _btnAdd.addEventListener("click", function () {
  openDeviceConfigModal("add");
});

/* ═══════════════════════════════════════════
   PER-DEVICE DROPDOWN MENU
   ═══════════════════════════════════════════ */

export const dropdownDevice = { id: "" };

export function openDevDropdown(btn, deviceId) {
  const d = devices[deviceId];
  if (!d) return;
  dropdownDevice.id = deviceId;
  const dd = $("#devDropdown");
  const rect = btn.getBoundingClientRect();
  dd.style.top = (rect.bottom + 2) + "px";
  dd.style.left = rect.left + "px";
  dd.style.display = "block";
  $("#devDdTitle").textContent = d.name;
  $("#devDdInfo").textContent = d.host + ":" + d.port;
  /* Reflect on_change state: highlight the active mode */
  const onChangeOn = d.on_change === true;
  const onChangeOff = d.on_change === false;
  const onSpan = $("#devDdOnChangeOn .dd-toggle-on") || $("#devDdOnChangeOn").querySelector(".dd-toggle-on");
  const offSpan = $("#devDdOnChangeOff .dd-toggle-off") || $("#devDdOnChangeOff").querySelector(".dd-toggle-off");
  if (onSpan) onSpan.classList.toggle("dd-toggle-active", onChangeOn);
  if (offSpan) offSpan.classList.toggle("dd-toggle-active", onChangeOff);
  /* Reflect verbose mode state */
  const verbBtn = $("#devDdVerbose");
  if (verbBtn) verbBtn.textContent = d.verbose ? "◉ Verbose Mode ON" : "○ Verbose Mode";
}

export function closeDevDropdown() {
  $("#devDropdown").style.display = "none";
  dropdownDevice.id = "";
}

/* Close when clicking outside the dropdown or a device tab */
document.addEventListener("click", function (e) {
  if (!e.target.closest("#devDropdown") && !e.target.closest(".dev-tab")) {
    closeDevDropdown();
  }
});

/** Send a command to an arbitrary device (not just the active one). */
export function sendToDevice(deviceId, address, payload) {
  const d = devices[deviceId];
  if (!d) return Promise.resolve({ status: "error", message: "Device not found" });
  const data = { host: d.host, port: d.port, address: address };
  if (payload !== null && payload !== undefined) data.args = [payload];
  return api("send", data);
}
