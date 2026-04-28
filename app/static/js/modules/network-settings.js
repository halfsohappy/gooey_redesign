/* ── Network Settings — WiFi intent lock, named destinations, connection indicator ── */

import { socket, $ } from "./state.js";
import { setNetworkDests } from "./field-validation.js";

const MODAL_ID     = "networkSettingsModal";
const INTENDED_KEY = "gooey_intended_network";
const DESTS_KEY    = "gooey_network_dests";

let _intendedSsid = "";
let _currentSsid  = null;
let _dests        = [];
let _socketOk     = false;

/* ── Minimal HTML escaping ── */
function esc(s) {
  return String(s)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

/* ── Persist / restore ── */
function loadStorage() {
  try {
    _intendedSsid = localStorage.getItem(INTENDED_KEY) || "";
    _dests = JSON.parse(localStorage.getItem(DESTS_KEY) || "[]");
  } catch (e) { _dests = []; }
}

function saveStorage() {
  try {
    localStorage.setItem(INTENDED_KEY, _intendedSsid);
    localStorage.setItem(DESTS_KEY, JSON.stringify(_dests));
  } catch (e) { /* ignore */ }
}

/* ── Connection indicator ── */
function syncIndicator() {
  const dot   = $("#wsDot");
  const label = $("#wsLabel");
  if (!dot || !label) return;

  if (!_socketOk) {
    dot.className     = "ws-dot disconnected";
    label.textContent = "Disconnected";
    return;
  }
  if (_intendedSsid && _currentSsid && _currentSsid !== _intendedSsid) {
    dot.className     = "ws-dot warn";
    label.textContent = "Wrong network";
    return;
  }
  dot.className     = "ws-dot connected";
  label.textContent = _intendedSsid ? "Connected · " + _intendedSsid : "Connected";
}

/* ── Socket events (these were missing from the modular code path) ── */
socket.on("connect",    function () { _socketOk = true;  syncIndicator(); });
socket.on("disconnect", function () { _socketOk = false; syncIndicator(); });

/* ── Scan for available networks ── */
async function fetchNetworks() {
  const scanBtn = $("#btnNetScan");
  const netSel  = $("#intendedNetworkSelect");
  const curEl   = $("#currentNetworkDisplay");

  if (scanBtn) { scanBtn.disabled = true; scanBtn.textContent = "Scanning…"; }
  try {
    const r = await fetch("/api/network-scan");
    const d = await r.json();
    _currentSsid = d.current || null;
    syncIndicator();

    if (curEl) curEl.textContent = d.current || "Unknown";

    if (netSel) {
      netSel.innerHTML = '<option value="">— any network —</option>';
      const list = Array.isArray(d.available) ? d.available : [];
      list.forEach(function (ssid) {
        const o = document.createElement("option");
        o.value = ssid;
        o.textContent = ssid;
        if (ssid === _intendedSsid) o.selected = true;
        netSel.appendChild(o);
      });
      /* If the saved intended SSID isn't in the visible list, add it anyway */
      if (_intendedSsid && !list.includes(_intendedSsid)) {
        const o = document.createElement("option");
        o.value       = _intendedSsid;
        o.textContent = _intendedSsid + " (saved)";
        o.selected    = true;
        netSel.appendChild(o);
      }
    }
  } catch (e) {
    if (curEl) curEl.textContent = "Scan failed";
  }
  if (scanBtn) { scanBtn.disabled = false; scanBtn.textContent = "Scan"; }
}

/* ── Destination list rendering ── */
function renderDests() {
  const list = $("#netDestListEl");
  if (!list) return;
  list.innerHTML = "";
  if (_dests.length === 0) {
    list.innerHTML = '<div class="net-dest-empty">No destinations saved yet.</div>';
    return;
  }
  _dests.forEach(function (d, i) {
    const row = document.createElement("div");
    row.className = "net-dest-row";
    row.innerHTML =
      '<span class="net-dest-name">' + esc(d.name) + '</span>' +
      '<span class="net-dest-ip">'   + esc(d.ip)   + '</span>' +
      '<span class="net-dest-port">:' + esc(String(d.port || "")) + '</span>' +
      '<button class="net-dest-del" data-idx="' + i + '" title="Remove">×</button>';
    list.appendChild(row);
  });
}

/* Keep the <datalist> in sync so IP fields get autocomplete hints */
function syncDatalist() {
  const dl = document.getElementById("netDestDatalist");
  if (!dl) return;
  dl.innerHTML = "";
  _dests.forEach(function (d) {
    const opt = document.createElement("option");
    opt.value = d.name;
    dl.appendChild(opt);
  });
}

/* ── Modal open / close ── */
function openModal() {
  const modal = $("#" + MODAL_ID);
  if (!modal) return;
  modal.classList.remove("hidden");
  renderDests();
  fetchNetworks();
}

function closeModal() {
  const modal = $("#" + MODAL_ID);
  if (modal) modal.classList.add("hidden");
}

/* ── Initialise ── */
(function init() {
  loadStorage();
  setNetworkDests(_dests);
  syncDatalist();
  syncIndicator();

  const triggerBtn = $("#btnNetworkSettings");
  const modal      = $("#" + MODAL_ID);
  if (!triggerBtn || !modal) return;

  /* Open */
  triggerBtn.addEventListener("click", openModal);

  /* Close on backdrop click */
  modal.addEventListener("click", function (e) {
    if (e.target === modal) closeModal();
  });

  /* Close button */
  const closeBtn = $("#btnCloseNetworkModal");
  if (closeBtn) closeBtn.addEventListener("click", closeModal);

  /* Network selection */
  const netSel = $("#intendedNetworkSelect");
  if (netSel) {
    netSel.addEventListener("change", function () {
      _intendedSsid = netSel.value;
      saveStorage();
      syncIndicator();
    });
  }

  /* Scan button */
  const scanBtn = $("#btnNetScan");
  if (scanBtn) scanBtn.addEventListener("click", fetchNetworks);

  /* Add destination */
  const addBtn  = $("#btnAddNetDest");
  const nameEl  = $("#netDestName");
  const ipEl    = $("#netDestIp");
  const portEl  = $("#netDestPort");

  function addDest() {
    const name = nameEl ? nameEl.value.trim() : "";
    const ip   = ipEl   ? ipEl.value.trim()   : "";
    const port = portEl ? portEl.value.trim()  : "";
    if (!name || !ip) return;

    const idx = _dests.findIndex(function (d) {
      return d.name.toLowerCase() === name.toLowerCase();
    });
    const entry = { name, ip, port };
    if (idx >= 0) _dests[idx] = entry;
    else _dests.push(entry);

    saveStorage();
    setNetworkDests(_dests);
    syncDatalist();
    renderDests();

    if (nameEl) nameEl.value = "";
    if (ipEl)   ipEl.value   = "";
    if (portEl) portEl.value = "";
    if (nameEl) nameEl.focus();
  }

  if (addBtn) addBtn.addEventListener("click", addDest);

  /* Enter key in add-form fields */
  [nameEl, ipEl, portEl].forEach(function (el) {
    if (!el) return;
    el.addEventListener("keydown", function (e) {
      if (e.key === "Enter") { e.preventDefault(); addDest(); }
    });
  });

  /* Delete via event delegation */
  const listEl = $("#netDestListEl");
  if (listEl) {
    listEl.addEventListener("click", function (e) {
      const btn = e.target.closest(".net-dest-del");
      if (!btn) return;
      const idx = parseInt(btn.dataset.idx, 10);
      if (!isNaN(idx)) {
        _dests.splice(idx, 1);
        saveStorage();
        setNetworkDests(_dests);
        syncDatalist();
        renderDests();
      }
    });
  }
}());
