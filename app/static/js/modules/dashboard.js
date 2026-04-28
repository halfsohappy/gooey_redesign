/* ── Dashboard — quick-command buttons, status config, status level ── */

import { $, devices, activeDevice } from './state.js';
import { withLoading } from './state.js';
import { toast, showConfirm } from './toast.js';
import { sendCmd, addr, CMD_ADDRESSES, getStatusConfigTargets, openDevSettingsModal } from './command.js';
import { sendToDevice } from './device-config.js';
import { toggleView } from './panel-resize.js';

/* Use event delegation so .qbtn[data-cmd] buttons work in the starred
   section too (cloneNode doesn't copy event listeners). */
document.addEventListener("click", function (e) {
  const btn = e.target.closest(".qbtn[data-cmd]");
  if (!btn) return;
  const confirmMsg = btn.dataset.confirm;
  const cmd = btn.dataset.cmd;
  const template = CMD_ADDRESSES[cmd];
  if (!template) return;
  const address = addr(template);
  const payload = btn.dataset.payload || null;
  const doSend = function () {
    withLoading(btn, function () {
      return sendCmd(address, payload).then(function (res) {
        if (res.status === "ok") toast("Sent: " + cmd, "success");
      });
    });
  };
  if (confirmMsg) {
    showConfirm("Confirm Action", confirmMsg, doSend, "OK", true);
  } else {
    doSend();
  }
});

/* Settings modal open/close — now the combined network + status config modal */
const _openSettings  = function () { openDevSettingsModal(); };
const _closeSettings = function () {
  const m = $("#networkSettingsModal");
  if (m) m.classList.add("hidden");
};

const btnStatusConfigHeader = $("#btnStatusConfigHeader");
if (btnStatusConfigHeader) btnStatusConfigHeader.addEventListener("click", _openSettings);

/* Status config apply */
$("#btnStatusConfig").addEventListener("click", function () {
  const ip = ($("#statusIP").value || "").trim();
  const port = ($("#statusPort").value || "").trim();
  const adr = ($("#statusAdr").value || "").trim();
  if (!ip || !port) { toast("IP and port are required", "error"); return; }
  let cfg = "ip:" + ip + ", port:" + port;
  if (adr) cfg += ", adr:" + adr;
  const targets = getStatusConfigTargets();
  if (!targets.length) { toast("No device selected", "error"); return; }
  targets.forEach(function (id) {
    const d = devices[id];
    if (d) sendToDevice(id, "/annieData/" + d.name + "/status/config", cfg);
  });
  toast("Status config applied" + (targets.length > 1 ? " (" + targets.length + " devices)" : ""), "success");
});

$("#btnStatusLevel").addEventListener("click", function () {
  const lvl = $("#statusLevel").value;
  if (!lvl) return;
  const targets = getStatusConfigTargets();
  if (!targets.length) { toast("No device selected", "error"); return; }
  targets.forEach(function (id) {
    const d = devices[id];
    if (d) sendToDevice(id, "/annieData/" + d.name + "/status/level", lvl);
  });
  toast("Level set: " + lvl, "success");
});

/* ── Header: Blackout / Restore all devices ── */
const btnBlackoutAll = $("#btnBlackoutAll");
if (btnBlackoutAll) btnBlackoutAll.addEventListener("click", function () {
  const ids = Object.keys(devices);
  if (!ids.length) { toast("No devices configured", "error"); return; }
  ids.forEach(function (id) {
    const d = devices[id];
    sendToDevice(id, "/annieData/" + d.name + "/blackout", null);
  });
  toast("Blackout sent to " + ids.length + " device(s)", "success");
});

const btnRestoreAll = $("#btnRestoreAll");
if (btnRestoreAll) btnRestoreAll.addEventListener("click", function () {
  const ids = Object.keys(devices);
  if (!ids.length) { toast("No devices configured", "error"); return; }
  ids.forEach(function (id) {
    const d = devices[id];
    sendToDevice(id, "/annieData/" + d.name + "/restore", null);
  });
  toast("Restore sent to " + ids.length + " device(s)", "success");
});

/* ── Header: Query device(s) ── */
const btnQueryDevice = $("#btnQueryDevice");
if (btnQueryDevice) btnQueryDevice.addEventListener("click", function () {
  const allChecked = $("#queryAllDevices") ? $("#queryAllDevices").checked : true;
  if (allChecked) {
    const ids = Object.keys(devices);
    if (!ids.length) { toast("No devices configured", "error"); return; }
    ids.forEach(function (id) {
      const d = devices[id];
      sendToDevice(id, "/annieData/" + d.name + "/list/all", "verbose");
    });
    toast("Querying " + ids.length + " device(s)…", "info");
  } else {
    const id = activeDevice.id;
    if (!id || !devices[id]) { toast("No active device", "error"); return; }
    const d = devices[id];
    sendToDevice(id, "/annieData/" + d.name + "/list/all", "verbose").then(function (res) {
      if (res.status === "ok") toast("Querying " + d.name + "…", "info");
    });
  }
  toggleView("feed");
});
