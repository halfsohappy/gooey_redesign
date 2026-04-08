/* ==============================================
   Gooey — Control Center Frontend
   Multi-device · registry-based · table-driven
   ============================================== */

(function () {
  "use strict";

  var MAX_LOG_ENTRIES = 500;

  /* ── Socket.IO ── */
  var socket = io({ transports: ["websocket", "polling"] });

  /* ── DOM helpers ── */
  var $ = function (sel) { return document.querySelector(sel); };
  var $$ = function (sel) { return document.querySelectorAll(sel); };

  /* ── Connection indicator ── */
  socket.on("connect", function () {
    $("#wsDot").className = "ws-dot connected";
    $("#wsLabel").textContent = "Connected";
  });
  socket.on("disconnect", function () {
    $("#wsDot").className = "ws-dot error";
    $("#wsLabel").textContent = "Disconnected";
  });

  /* ─�� Theme toggle ── */
  (function () {
    var THEME_KEY = "gooey-theme";
    var btn = $("#btnThemeToggle");
    var icon = btn ? btn.querySelector("i") : null;

    function isDark() {
      return document.documentElement.classList.contains("dark");
    }

    function applyTheme(dark) {
      var html = document.documentElement;
      html.classList.add("transitioning");
      if (dark) html.classList.add("dark");
      else html.classList.remove("dark");
      if (icon) icon.className = dark ? "bi bi-sun-fill" : "bi bi-moon-fill";
      setTimeout(function () { html.classList.remove("transitioning"); }, 250);
    }

    // Sync button state with class set by inline <script> in <head>
    applyTheme(isDark());

    if (btn) {
      btn.addEventListener("click", function () {
        var dark = !isDark();
        try { localStorage.setItem(THEME_KEY, dark ? "dark" : "light"); } catch (e) {}
        applyTheme(dark);
      });
    }

    // Follow system preference when user hasn't manually chosen
    try {
      window.matchMedia("(prefers-color-scheme: dark)").addEventListener("change", function (e) {
        if (!localStorage.getItem(THEME_KEY)) applyTheme(e.matches);
      });
    } catch (e) {}
  }());

  /* ── Section nav ── */
  $$(".nav-btn[data-section]").forEach(function (btn) {
    btn.addEventListener("click", function () {
      $$(".nav-btn[data-section]").forEach(function (b) { b.classList.remove("active"); });
      $$(".section").forEach(function (s) { s.classList.remove("active"); });
      btn.classList.add("active");
      var sec = $("#sec-" + btn.dataset.section);
      if (sec) sec.classList.add("active");
    });
  });


  /* ── withLoading helper ── */
  function withLoading(btn, fn) {
    if (!btn) return fn();
    btn.disabled = true;
    btn.classList.add("loading");
    return Promise.resolve(fn()).finally(function () {
      btn.disabled = false;
      btn.classList.remove("loading");
    });
  }

  /* ── Toast history + notification system ── */
  var _toastHistory = [];

  function renderNotifHistory() {
    var container = $("#notifHistory");
    if (!container) return;
    container.innerHTML = "";
    if (_toastHistory.length === 0) {
      container.innerHTML = '<div class="notif-history-item"><span class="notif-history-msg" style="color:var(--text-light)">No notifications yet</span></div>';
      return;
    }
    _toastHistory.slice().reverse().forEach(function (item) {
      var div = document.createElement("div");
      div.className = "notif-history-item";
      div.innerHTML = '<span class="notif-history-time">' + item.time + '</span><span class="notif-history-msg notif-type-' + item.type + '">' + item.msg + '</span>';
      container.appendChild(div);
    });
  }

  // Render on load
  renderNotifHistory();

  /* ── showToast (public alias: toast) ── */
  function showToast(msg, type) {
    type = type || "info";
    /* Add to history (max 100) */
    var now = new Date();
    var timeStr = now.getHours().toString().padStart(2, "0") + ":" + now.getMinutes().toString().padStart(2, "0") + ":" + now.getSeconds().toString().padStart(2, "0");
    _toastHistory.push({ msg: msg, type: type, time: timeStr });
    if (_toastHistory.length > 100) _toastHistory.shift();
    renderNotifHistory();
    /* Update + flash latest notification in header */
    var latest = $("#notifLatest");
    if (latest) {
      latest.textContent = msg;
      latest.classList.remove("notif-flash");
      void latest.offsetWidth; // force reflow to restart animation
      latest.classList.add("notif-flash");
    }
  }

  /* Backward-compatible alias */
  function toast(msg, type) { showToast(msg, type); }

  /* ── Confirm modal ── */
  function showConfirm(title, body, onConfirm, okLabel, danger) {
    if (okLabel === undefined) okLabel = "Confirm";
    if (danger === undefined) danger = true;
    var modal = document.getElementById("confirmModal");
    if (!modal) { if (onConfirm) onConfirm(); return; }
    var box = modal.querySelector(".modal-box");
    var isVerbose = title.indexOf("Verbose") !== -1;
    if (box) {
      box.classList.toggle("modal-box--verbose", isVerbose);
    }
    document.getElementById("confirmTitle").textContent = title;
    document.getElementById("confirmBody").textContent = body;
    var okBtn = document.getElementById("confirmOk");
    okBtn.textContent = okLabel;
    okBtn.className = "btn " + (danger ? "btn-danger" : "btn-primary");
    modal.classList.remove("hidden");
    var cancel = document.getElementById("confirmCancel");
    cancel.focus();
    var cleanup = function () { modal.classList.add("hidden"); };
    okBtn.onclick = function () { cleanup(); onConfirm(); };
    cancel.onclick = cleanup;
    modal.onclick = function (e) { if (e.target === modal) cleanup(); };
  }

  /* ── toggleHelp for inline help boxes (exposed globally for onclick attrs) ── */
  window.toggleHelp = function (id) {
    var el = document.getElementById(id);
    if (el) el.classList.toggle("hidden");
  };
  function toggleHelp(id) { window.toggleHelp(id); }

  /* ── API helper ── */
  function api(endpoint, data, method) {
    var opts = {
      method: method || "POST",
      headers: { "Content-Type": "application/json" },
    };
    if (data) opts.body = JSON.stringify(data);
    return fetch("/api/" + endpoint, opts)
      .then(function (r) { return r.json(); })
      .then(function (res) {
        if (res.status === "error") toast(res.message, "error");
        return res;
      })
      .catch(function (err) {
        toast("Request failed: " + err.message, "error");
        return { status: "error", message: err.message };
      });
  }

  /* ═══════════════════════════════════════════
     DEVICE MANAGEMENT  (multi-device)
     ═══════════════════════════════════════════ */
  var devices = {};        // { id: { host, port, name, messages:{}, scenes:{} } }
  var activeDeviceId = "";  // current tab

  function generateDeviceId(host, port, name) {
    return name + "@" + host + ":" + port;
  }

  /** Persist device connection info to localStorage. */
  function saveDevicesToStorage() {
    try {
      var data = Object.keys(devices).map(function (id) {
        var d = devices[id];
        return { host: d.host, port: d.port, name: d.name };
      });
      localStorage.setItem("gooey_devices", JSON.stringify(data));
      localStorage.setItem("gooey_active_device", activeDeviceId);
    } catch (e) { /* storage full or unavailable */ }
  }

  /** Restore devices from localStorage on page load. */
  function restoreDevicesFromStorage() {
    try {
      var raw = localStorage.getItem("gooey_devices");
      if (!raw) return;
      var list = JSON.parse(raw);
      list.forEach(function (d) {
        if (d.host && d.port && d.name) {
          var id = generateDeviceId(d.host, d.port, d.name);
          if (!devices[id]) {
            devices[id] = { host: d.host, port: parseInt(d.port, 10), name: d.name, messages: {}, scenes: {}, oris: {} };
          }
        }
      });
      var savedActive = localStorage.getItem("gooey_active_device");
      if (savedActive && devices[savedActive]) activeDeviceId = savedActive;
      else {
        var keys = Object.keys(devices);
        if (keys.length > 0) activeDeviceId = keys[0];
      }
    } catch (e) { /* corrupt data */ }
  }

  function addDevice(host, port, name) {
    var id = generateDeviceId(host, port, name);
    if (!devices[id]) {
      devices[id] = { host: host, port: parseInt(port, 10), name: name, messages: {}, scenes: {}, oris: {} };
    }
    activeDeviceId = id;
    renderDeviceTabs();
    refreshAllDropdowns();
    saveDevicesToStorage();
    return id;
  }

  function removeDevice(id) {
    delete devices[id];
    if (activeDeviceId === id) {
      var keys = Object.keys(devices);
      activeDeviceId = keys.length > 0 ? keys[0] : "";
    }
    renderDeviceTabs();
    renderMsgTable();
    renderSceneTable();
    renderOriTable();
    refreshAllDropdowns();
    saveDevicesToStorage();
  }

  function getActiveDev() {
    return devices[activeDeviceId] || null;
  }

  function devHost() { var d = getActiveDev(); return d ? d.host : "127.0.0.1"; }
  function devPort() { var d = getActiveDev(); return d ? d.port : 8000; }
  function devName() { var d = getActiveDev(); return d ? d.name : ""; }

  /* ── Device tab rendering ── */
  function renderDeviceTabs() {
    var container = $("#devTabsWrap") || $("#hdrDevices");
    var devActions = $("#hdrDevActions");
    /* Remove existing tabs */
    $$(".dev-tab[data-device-id]").forEach(function (t) { t.remove(); });
    var devCount = Object.keys(devices).length;
    Object.keys(devices).forEach(function (id) {
      var d = devices[id];
      var isActive = (id === activeDeviceId);
      var btn = document.createElement("button");
      btn.className = "dev-tab" + (isActive ? " active" : "");
      btn.dataset.deviceId = id;
      /* Build button content: status dot + sanitised device name + caret */
      var dot = document.createElement("span");
      dot.className = "dev-dot";
      dot.title = isActive ? "Active device" : "Inactive";
      btn.appendChild(dot);
      var label = document.createElement("span");
      label.className = "tab-label";
      label.textContent = d.name;
      btn.appendChild(label);
      var caret = document.createElement("span");
      caret.className = "tab-caret";
      caret.textContent = "▾";
      btn.appendChild(caret);
      /* Right-click opens dropdown directly — accessible even when tab is narrow */
      btn.addEventListener("contextmenu", function (e) {
        e.preventDefault();
        activeDeviceId = id;
        renderDeviceTabs();
        refreshAllDropdowns();
        refreshQueryDeviceSelect();
        var freshBtn = document.querySelector('.dev-tab[data-device-id="' + id + '"]');
        if (freshBtn) openDevDropdown(freshBtn, id);
      });
      /* Plain click: just select the device, no dropdown */
      btn.addEventListener("click", function (e) {
        activeDeviceId = id;
        renderDeviceTabs();
        renderMsgTable();
        renderSceneTable();
        renderOriTable();
        refreshAllDropdowns();
        refreshQueryDeviceSelect();
      });
      /* Caret click: open the per-device dropdown without re-triggering selection logic */
      caret.addEventListener("click", function (e) {
        e.stopPropagation();
        /* Make sure this device is active first */
        activeDeviceId = id;
        renderDeviceTabs();
        refreshAllDropdowns();
        refreshQueryDeviceSelect();
        var freshBtn = document.querySelector('.dev-tab[data-device-id="' + id + '"]');
        if (freshBtn) openDevDropdown(freshBtn, id);
      });
      container.insertBefore(btn, devActions);
    });
    /* Show/hide welcome banner based on device count */
    var wb = $("#welcomeBanner");
    if (wb) wb.style.display = devCount === 0 ? "" : "none";
    /* Scale font size up when few devices; wrap to 2 rows when space saturated */
    if (container) {
      var fontSize = devCount <= 2 ? "15px" : devCount <= 4 ? "14px" : "13px";
      container.style.setProperty("--dev-tab-font", fontSize);
      container.classList.remove("wrap-2row");
      /* Defer saturation check until layout is done */
      requestAnimationFrame(function () {
        if (container.scrollWidth > container.clientWidth + 4) {
          container.classList.add("wrap-2row");
        }
      });
    }
    /* Update feed device filter */
    var sel = $("#feedDeviceFilter");
    var curVal = sel.value;
    sel.innerHTML = '<option value="">All devices</option>';
    Object.keys(devices).forEach(function (id) {
      var d = devices[id];
      var opt = document.createElement("option");
      opt.value = d.name;
      opt.textContent = d.name;
      sel.appendChild(opt);
    });
    sel.value = curVal;
    /* Sync the header query-device select */
    refreshQueryDeviceSelect();
  }

  /* ── Restore devices from previous session ── */
  restoreDevicesFromStorage();
  renderDeviceTabs();

  /* ── IP resolver: type "me" to use this computer's IP ── */
  var _myIpCache = null;
  function resolveIp(host, cb) {
    if (host.trim().toLowerCase() !== "me") { cb(host.trim()); return; }
    if (_myIpCache) { cb(_myIpCache); return; }
    fetch("/api/my-ip")
      .then(function (r) { return r.json(); })
      .then(function (d) { _myIpCache = d.ip || "127.0.0.1"; cb(_myIpCache); })
      .catch(function () { cb("127.0.0.1"); });
  }

  /* Auto-resolve "me" in any IP input field on blur */
  ["statusIP", "msgIP", "directIP", "rawHost", "bridgeOutHost", "sceneIP"].forEach(function (fieldId) {
    var el = $("#" + fieldId);
    if (!el) return;
    el.addEventListener("blur", function () {
      if (el.value.trim().toLowerCase() === "me") {
        resolveIp("me", function (ip) { el.value = ip; });
      }
    });
  });

  /* ── Device config modal (add / edit) ── */
  var _deviceConfigMode = "add";
  var _deviceConfigEditId = "";

  function openDeviceConfigModal(mode, deviceId) {
    _deviceConfigMode = mode || "add";
    _deviceConfigEditId = deviceId || "";
    var titleEl = $("#deviceConfigTitle");
    var saveBtn = $("#deviceConfigSave");
    var ipEl    = $("#deviceConfigIP");
    var portEl  = $("#deviceConfigPort");
    var nameEl  = $("#deviceConfigName");
    if (mode === "edit" && deviceId && devices[deviceId]) {
      var d = devices[deviceId];
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

  (function () {
    var modal  = $("#deviceConfigModal");
    var saveBtn = $("#deviceConfigSave");
    var cancelBtn = $("#deviceConfigCancel");

    if (cancelBtn) cancelBtn.addEventListener("click", function () {
      modal.classList.add("hidden");
    });
    if (modal) modal.addEventListener("click", function (e) {
      if (e.target === modal) modal.classList.add("hidden");
    });

    if (saveBtn) saveBtn.addEventListener("click", function () {
      var host = ($("#deviceConfigIP").value || "").trim();
      var port = ($("#deviceConfigPort").value || "").trim();
      var name = ($("#deviceConfigName").value || "").trim();
      if (!host) { toast("IP required", "error"); return; }
      if (!port) { toast("Port required", "error"); return; }
      if (!name) { toast("Name required", "error"); return; }
      modal.classList.add("hidden");
      resolveIp(host, function (resolvedHost) {
        if (_deviceConfigMode === "edit" && _deviceConfigEditId) {
          var wasActive = (activeDeviceId === _deviceConfigEditId);
          delete devices[_deviceConfigEditId];
          if (wasActive) activeDeviceId = "";
        }
        addDevice(resolvedHost, parseInt(port, 10), name);
        renderMsgTable(); renderSceneTable(); renderOriTable();
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
  function editDevice(id) {
    openDeviceConfigModal("edit", id);
  }

  /* ── Add-device button ── */
  $("#btnAddDevice").addEventListener("click", function () {
    openDeviceConfigModal("add");
  });

  /* ═══════════════════════════════════════════
     PER-DEVICE DROPDOWN MENU
     ═══════════════════════════════════════════ */

  var _dropdownDeviceId = "";

  function openDevDropdown(btn, deviceId) {
    var d = devices[deviceId];
    if (!d) return;
    _dropdownDeviceId = deviceId;
    var dd = $("#devDropdown");
    var rect = btn.getBoundingClientRect();
    dd.style.top = (rect.bottom + 2) + "px";
    dd.style.left = rect.left + "px";
    dd.style.display = "block";
    $("#devDdTitle").textContent = d.name;
    $("#devDdInfo").textContent = d.host + ":" + d.port;
    /* Reflect on_change state: highlight the active mode */
    var onChangeOn = d.on_change === true;
    var onChangeOff = d.on_change === false;
    var onSpan = $("#devDdOnChangeOn .dd-toggle-on") || $("#devDdOnChangeOn").querySelector(".dd-toggle-on");
    var offSpan = $("#devDdOnChangeOff .dd-toggle-off") || $("#devDdOnChangeOff").querySelector(".dd-toggle-off");
    if (onSpan) onSpan.classList.toggle("dd-toggle-active", onChangeOn);
    if (offSpan) offSpan.classList.toggle("dd-toggle-active", onChangeOff);
    /* Reflect verbose mode state */
    var verbBtn = $("#devDdVerbose");
    if (verbBtn) verbBtn.textContent = d.verbose ? "◉ Verbose Mode ON" : "○ Verbose Mode";
  }

  function closeDevDropdown() {
    $("#devDropdown").style.display = "none";
    _dropdownDeviceId = "";
  }

  /* Close when clicking outside the dropdown or a device tab */
  document.addEventListener("click", function (e) {
    if (!e.target.closest("#devDropdown") && !e.target.closest(".dev-tab")) {
      closeDevDropdown();
    }
  });

  /** Send a command to an arbitrary device (not just the active one). */
  function sendToDevice(deviceId, address, payload) {
    var d = devices[deviceId];
    if (!d) return Promise.resolve({ status: "error", message: "Device not found" });
    var data = { host: d.host, port: d.port, address: address };
    if (payload !== null && payload !== undefined) data.args = [payload];
    return api("send", data);
  }

  /* Per-device dropdown action handlers */

  /* ── Status config modal helpers ── */
  function openDevSettingsModal() {
    /* Populate target device dropdown */
    var sel = $("#statusConfigTarget");
    if (sel) {
      sel.innerHTML = '<option value="__all__">All devices</option>';
      Object.keys(devices).forEach(function (id) {
        var d = devices[id];
        var opt = document.createElement("option");
        opt.value = id;
        opt.textContent = d.name;
        if (id === activeDeviceId) opt.selected = true;
        sel.appendChild(opt);
      });
    }
    $("#devSettingsModal").classList.remove("hidden");
  }

  function getStatusConfigTargets() {
    var sel = $("#statusConfigTarget");
    if (!sel) return activeDeviceId ? [activeDeviceId] : [];
    var val = sel.value;
    if (val === "__all__") return Object.keys(devices);
    return val ? [val] : [];
  }

  var btnStatusConfigHeader = $("#btnStatusConfigHeader");
  if (btnStatusConfigHeader) btnStatusConfigHeader.addEventListener("click", openDevSettingsModal);

  $("#devSettingsClose").addEventListener("click", function () {
    $("#devSettingsModal").classList.add("hidden");
  });

  /* Close modal on backdrop click */
  $("#devSettingsModal").addEventListener("click", function (e) {
    if (e.target === this) this.classList.add("hidden");
  });

  $("#devDdNvsClear").addEventListener("click", function () {
    var id = _dropdownDeviceId;
    if (!id || !devices[id]) { closeDevDropdown(); return; }
    var d = devices[id];
    closeDevDropdown();
    showConfirm("Clear NVS", "Clear NVS for " + d.name + "? This erases all saved settings.", function () {
      sendToDevice(id, "/annieData/" + d.name + "/nvs/clear", null).then(function (res) {
        if (res.status === "ok") toast("NVS cleared: " + d.name, "success");
      });
    }, "Clear NVS", true);
  });

  $("#devDdBlackout").addEventListener("click", function () {
    var id = _dropdownDeviceId;
    if (!id || !devices[id]) { closeDevDropdown(); return; }
    var d = devices[id];
    sendToDevice(id, "/annieData/" + d.name + "/blackout", null).then(function (res) {
      if (res.status === "ok") toast("Blackout: " + d.name, "success");
    });
    closeDevDropdown();
  });

  $("#devDdRestore").addEventListener("click", function () {
    var id = _dropdownDeviceId;
    if (!id || !devices[id]) { closeDevDropdown(); return; }
    var d = devices[id];
    sendToDevice(id, "/annieData/" + d.name + "/restore", null).then(function (res) {
      if (res.status === "ok") toast("Restore: " + d.name, "success");
    });
    closeDevDropdown();
  });

  $("#devDdOnChangeOn").addEventListener("click", function () {
    var id = _dropdownDeviceId;
    if (!id || !devices[id]) { closeDevDropdown(); return; }
    var d = devices[id];
    sendToDevice(id, "/annieData/" + d.name + "/on_change", "on").then(function (res) {
      if (res.status === "ok") toast("On Change on: " + d.name, "success");
    });
    closeDevDropdown();
  });

  $("#devDdOnChangeOff").addEventListener("click", function () {
    var id = _dropdownDeviceId;
    if (!id || !devices[id]) { closeDevDropdown(); return; }
    var d = devices[id];
    sendToDevice(id, "/annieData/" + d.name + "/on_change", "off").then(function (res) {
      if (res.status === "ok") toast("On Change off: " + d.name, "success");
    });
    closeDevDropdown();
  });

  $("#devDdVerbose").addEventListener("click", function () {
    var id = _dropdownDeviceId;
    if (!id || !devices[id]) { closeDevDropdown(); return; }
    var d = devices[id];
    d.verbose = !d.verbose;
    toast("Verbose mode " + (d.verbose ? "ON" : "OFF") + " for " + d.name, d.verbose ? "success" : "info");
    closeDevDropdown();
  });

  $("#devDdEdit").addEventListener("click", function () {
    var id = _dropdownDeviceId;
    closeDevDropdown();
    if (id) editDevice(id);
  });

  $("#devDdRemove").addEventListener("click", function () {
    var id = _dropdownDeviceId;
    closeDevDropdown();
    if (id) removeDevice(id);
  });

  /* No default device — user adds devices manually */

  /* ═══════════════════════════════════════════
     SEND HELPERS
     ═══════════════════════════════════════════ */

  /** Send a TheaterGWD command for the active device. */
  function sendCmd(address, payload) {
    var data = {
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
  var _flushResolvers = [];
  function _onFlushReply(entry) {
    if (entry.direction !== "recv") return;
    if (!entry.address || !/\/flush$/i.test(entry.address)) return;
    var resolvers = _flushResolvers.splice(0);
    resolvers.forEach(function (r) { r(); });
  }

  /**
   * Send /flush and return a promise that resolves when the device replies.
   * Falls back to a 2s timeout if no reply is received (e.g. old firmware).
   */
  function sendFlush() {
    return new Promise(function (resolve) {
      var timer = setTimeout(function () {
        var idx = _flushResolvers.indexOf(resolve);
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
  function addr(template, name) {
    var a = template.replace("{device}", devName());
    if (name !== undefined) a = a.replace("{name}", name);
    return a;
  }

  /* ── Command address map ── */
  var CMD_ADDRESSES = {
    blackout:       "/annieData/{device}/blackout",
    restore:        "/annieData/{device}/restore",
    on_change:      "/annieData/{device}/on_change",
    save:           "/annieData/{device}/save",
    load:           "/annieData/{device}/load",
    nvs_clear:      "/annieData/{device}/nvs/clear",
    list_messages:  "/annieData/{device}/list/msgs",
    list_scenes:   "/annieData/{device}/list/scenes",
    list_all:       "/annieData/{device}/list/all",
  };

  /* ═══════════════════════════════════════════
     REPLY PARSING  — auto-populate registry
     ═══════════════════════════════════════════ */

  /**
   * Parse incoming OSC messages from the device. The device sends status
   * replies that include message and scene info. We try to extract names
   * and parameters from them to keep the local registry in sync.
   *
   * Typical reply patterns:
   *   /annieData/{device}/status   "msg: accelX | value:accelX ip:192.168.1.50 ..."
   *   /annieData/{device}/status   "scene: sensors | period:50 adrMode:fallback msgs:accelX+accelY"
   *   /annieData/{device}/status   "[INFO] list/msgs: accelX, accelY, gyroZ"
   */
  function parseReplyIntoRegistry(entry) {
    if (entry.direction !== "recv") return;

    var text = "";
    (entry.args || []).forEach(function (a) {
      if (a.value !== undefined) text += " " + a.value;
    });
    text = text.trim();
    if (!text) return;

    /* Determine which device this reply belongs to.
       Match "/deviceName/" or "/deviceName" at end of path. */
    var matchedId = "";
    Object.keys(devices).forEach(function (id) {
      var d = devices[id];
      if (!entry.address) return;
      var seg = "/" + d.name + "/";
      var tail = "/" + d.name;
      if (entry.address.indexOf(seg) !== -1 || entry.address.slice(-tail.length) === tail) matchedId = id;
    });
    if (!matchedId) matchedId = activeDeviceId;
    if (!matchedId) return;
    var dev = devices[matchedId];
    if (!dev) return;

    /* ── Parse list replies ──
       The device sends replies to /reply/{dev}/list/msgs (or /scenes, /all)
       with a multi-line payload: "Messages (N):\n  name1\n  name2\n..."
       Detect by address; fall back to text-pattern for legacy status messages. */
    var listAddr = entry.address || "";
    var isListReply = /\/list\/(msgs|messages|scenes|all)/i.test(listAddr);
    var isLegacyList = !isListReply && text.match(/list\/(?:msgs|scenes|all):\s*(.+)/i);
    if (isListReply || isLegacyList) {
      if (isLegacyList) {
        var legacyMatch = text.match(/list\/(?:msgs|scenes|all):\s*(.+)/i);
        var names = legacyMatch[1].split(/[,\s]+/).map(function (s) { return s.trim(); }).filter(Boolean);
        var isMsgList = text.match(/list\/msgs/i) || text.match(/list\/all/i);
        var isSceneList = text.match(/list\/scenes/i) || text.match(/list\/all/i);
        names.forEach(function (n) {
          if (isMsgList   && !dev.messages[n]) dev.messages[n] = {};
          if (isSceneList && !dev.scenes[n])  dev.scenes[n]  = {};
        });
      } else {
        /* Multi-line reply format from the device. For /list/all, track which
           block we are in; for /list/msgs or /list/scenes use the address. */
        var isAllList  = /\/list\/all/i.test(listAddr);
        var isMsgList  = /\/list\/msgs/i.test(listAddr);
        var curBlock = isAllList ? "" : (isMsgList ? "msg" : "scene");
        text.split(/\n/).forEach(function (line) {
          var trimmed = line.trim();
          if (!trimmed) return;
          /* Parse device-level state lines (on_change:on/off) */
          var on_changeMatch = trimmed.match(/^on_change:(on|off)$/i);
          if (on_changeMatch) {
            dev.on_change = on_changeMatch[1].toLowerCase() === "on";
            return;
          }
          if (/^messages\s*\(\d+\):/i.test(trimmed)) { curBlock = "msg";   return; }
          if (/^scenes\s*\(\d+\):/i.test(trimmed))  { curBlock = "scene"; return; }
          var n = trimmed.split(/\s+/)[0];
          if (!n) return;
          if (curBlock === "msg") {
            var mParams = parseConfigString(trimmed);
            // Normalize firmware's 'val:' key to 'value' for UI compatibility.
            if (mParams.val !== undefined && mParams.value === undefined) mParams.value = mParams.val;
            // Extract enabled state from [ON]/[OFF] status block.
            if (/\[ON\]/i.test(trimmed))  mParams.enabled = "true";
            if (/\[OFF\]/i.test(trimmed)) mParams.enabled = "false";
            dev.messages[n] = Object.assign(dev.messages[n] || {}, mParams);
          }
          if (curBlock === "scene") {
            var pParams = parseConfigString(trimmed);
            // Extract send period and running state from "[RUNNING, 50ms, …]" or "[STOPPED, 50ms, …]".
            var periodM = trimmed.match(/\[(RUNNING|STOPPED),\s*(\d+)ms/i);
            if (periodM) { pParams.period = periodM[2]; pParams.running = /RUNNING/i.test(periodM[1]); }
            dev.scenes[n] = Object.assign(dev.scenes[n] || {}, pParams);
          }
        });
      }
      renderMsgTable();
      renderSceneTable();
      refreshAllDropdowns();
      return;
    }

    /* ── Parse msg info reply ── */
    var msgMatch = text.match(/msg:\s*(\S+)\s*\|\s*(.*)/i);
    if (msgMatch) {
      var mName = msgMatch[1];
      var mParams = parseConfigString(msgMatch[2]);
      dev.messages[mName] = Object.assign(dev.messages[mName] || {}, mParams);
      renderMsgTable();
      refreshAllDropdowns();
      return;
    }

    /* ── Parse scene info reply ── */
    var sceneMatch = text.match(/scene:\s*(\S+)\s*\|\s*(.*)/i);
    if (sceneMatch) {
      var pName = sceneMatch[1];
      var pParams = parseConfigString(sceneMatch[2]);
      var infoRunM = sceneMatch[2].match(/\[(RUNNING|STOPPED)/i);
      if (infoRunM) pParams.running = /RUNNING/i.test(infoRunM[1]);
      dev.scenes[pName] = Object.assign(dev.scenes[pName] || {}, pParams);
      renderSceneTable();
      refreshAllDropdowns();
      return;
    }

    /* ── Parse ori list reply ──
       Address contains /ori/list.  Payload format (firmware v2):
       "light1 [3] (AX) (*), zone1 [5], pending1 [P]"  or  "(none)"
       [P]    = unsampled slot (registered but no samples captured)
       [N]    = N samples in cloud
       (AX)   = axis-aware matching (pointing mode)
       (*)    = currently active */
    if (/\/ori\/list/i.test(listAddr)) {
      dev.oris = {};
      if (text !== "(none)") {
        var oriParts = text.split(/,\s*/);
        oriParts.forEach(function (part) {
          part = part.trim();
          if (!part) return;
          /* Match: name followed by optional tags */
          var om = part.match(/^(\S+)/);
          if (!om) return;
          var oName = om[1];
          var isPending = /\[P\]/.test(part);
          var sampleMatch = part.match(/\[(\d+)\]/);
          var samples = isPending ? 0 : (sampleMatch ? parseInt(sampleMatch[1], 10) : 1);
          dev.oris[oName] = {
            name: oName,
            samples: samples,
            useAxis: /\(AX\)/.test(part),
            color: null,
            active: /\(\*\)/.test(part)
          };
        });
      }
      renderOriTable();
      refreshAllDropdowns();
      return;
    }

    /* ── Parse show list reply ──
       Address contains /show/list. Payload: CSV of show names or "(none)" */
    if (/\/show\/list/i.test(listAddr)) {
      var showNames = (text && text !== "(none)") ? text.split(/,\s*/) : [];
      renderShowDeviceTable(showNames);
      /* Populate the datalist for show name input */
      var showDl = $("#showNameList");
      if (showDl) {
        showDl.innerHTML = showNames.map(function (n) {
          return '<option value="' + esc(n.trim()) + '">';
        }).join("");
      }
      return;
    }

    /* ── Parse ori info reply ──
       Address contains /ori/info.  Payload format:
       "name: samples=N center=[x, y, z] half_w=[x, y, z]"  or
       "name: samples=1 point q=(qi,qj,qk,qr) euler=[x, y, z]" */
    if (/\/ori\/info/i.test(listAddr)) {
      showOriDetails(text);
      return;
    }

    /* ── Parse ori active reply ──
       Address contains /ori/active. Payload: ori name or "(none)" */
    if (/\/ori\/active/i.test(listAddr)) {
      Object.keys(dev.oris).forEach(function (k) { dev.oris[k].active = false; });
      if (text !== "(none)" && dev.oris[text]) {
        dev.oris[text].active = true;
      }
      renderOriTable();
      return;
    }

    /* ── Parse on_change reply ── */
    var onChangeReply = text.match(/^on_change\s+(ON|OFF)$/i);
    if (onChangeReply) {
      dev.on_change = onChangeReply[1].toUpperCase() === "ON";
      return;
    }

    /* ── Parse verbose list lines (key:val pairs with a leading name) ── */
    var verboseMatch = text.match(/^\[(?:INFO|DEBUG)\]\s+(\S+)\s*[:=]\s*(.*)/i);
    if (verboseMatch) {
      var vName = verboseMatch[1];
      var vRest = verboseMatch[2];
      if (vRest.indexOf("value:") !== -1 || vRest.indexOf("val:") !== -1 || vRest.indexOf("ip:") !== -1 || vRest.indexOf("adr:") !== -1) {
        var vParams = parseConfigString(vRest);
        if (vParams.val !== undefined && vParams.value === undefined) vParams.value = vParams.val;
        dev.messages[vName] = Object.assign(dev.messages[vName] || {}, vParams);
        renderMsgTable();
        refreshAllDropdowns();
      } else if (vRest.indexOf("period:") !== -1 || vRest.indexOf("adrMode:") !== -1 || vRest.indexOf("adr_mode:") !== -1 || vRest.indexOf("msgs:") !== -1) {
        var vpParams = parseConfigString(vRest);
        dev.scenes[vName] = Object.assign(dev.scenes[vName] || {}, vpParams);
        renderSceneTable();
        refreshAllDropdowns();
      }
    }
  }

  /** Parse key:value pairs separated by commas/whitespace; values may contain spaces until the next key:. */
  function parseConfigString(str) {
    var result = {};
    // Group 1 captures the key; group 2 captures the value.
    // The lookahead stops value capture at the next "key:" token (optionally comma-separated) or end of string.
    var re = /([a-zA-Z_][a-zA-Z0-9_]*)\s*:\s*(.*?)(?=(?:\s*,?\s*[a-zA-Z_][a-zA-Z0-9_]*\s*:)|$)/g;
    var match;
    while ((match = re.exec(str)) !== null) {
      result[match[1].trim()] = match[2].trim();
    }
    return result;
  }

  /* ═══════════════════════════════════════════
     MESSAGE TABLE
     ═══════════════════════════════════════════ */

  function getMsgScenes(dev, msgName) {
    var found = [];
    if (!dev || !dev.scenes) return found;
    Object.keys(dev.scenes).forEach(function (sname) {
      var msgs = (dev.scenes[sname].msgs || "").replace(/\+/g, ",");
      var list = msgs.split(",").map(function (s) { return s.trim(); });
      if (list.indexOf(msgName) !== -1) found.push(sname);
    });
    if (found.length === 0) {
      var sc = dev.messages && dev.messages[msgName] && dev.messages[msgName].scene;
      if (sc) found.push(sc);
    }
    return found;
  }

  function buildGateStr(m) {
    if (m.gate_src || m.gate_source) {
      var gs = m.gate_src || m.gate_source;
      var gm = m.gate_mode || "";
      var s = gm + ":" + gs;
      var isEdge = (gm === "rising" || gm === "falling");
      if (m.gate_lo != null && m.gate_lo !== "") s += (isEdge ? " trigger:" : " \u2265") + m.gate_lo;
      if (m.gate_hi != null && m.gate_hi !== "") s += (isEdge ? " delta:" : " \u2264") + m.gate_hi;
      return s;
    }
    if (m.ori_only || m.orionly) return "only:ori:" + (m.ori_only || m.orionly);
    if (m.ori_not  || m.orinot)  return "not:ori:"  + (m.ori_not  || m.orinot);
    if (m.ternori)                return "toggle:ori:" + m.ternori;
    return "";
  }

  function renderMsgTable() {
    var dev = getActiveDev();
    var tbody = $("#msgTableBody");
    tbody.innerHTML = "";
    if (!dev || Object.keys(dev.messages).length === 0) {
      tbody.innerHTML = '<tr><td colspan="4"><div class="empty-state"><div class="empty-icon">○</div><div class="empty-text">No messages tracked yet.</div><div class="empty-sub">Click the device tab → Query to load from device, or create one below.</div></div></td></tr>';
      return;
    }
    Object.keys(dev.messages).forEach(function (name) {
      var m = dev.messages[name];
      var sensor  = esc(m.value || m.val || "—");
      var low     = m.low  || m.min || "";
      var high    = m.high || m.max || "";
      var range   = (low !== "" && high !== "") ? esc(low) + " \u2192 " + esc(high) : "";
      var ip      = esc(m.ip  || "—");
      var port    = esc(m.port || "—");
      var adr     = esc(m.adr || m.addr || m.address || "—");
      var gateStr = buildGateStr(m);
      var scenes  = getMsgScenes(dev, name);
      var isSoloed = !!m.soloed;

      var sceneTags = scenes.map(function (s) {
        return '<span class="msg-stag">' + esc(s) + '</span>';
      }).join("") || '<span style="color:var(--text-light);font-size:12px">—</span>';

      /* ── data row ── */
      var tr = document.createElement("tr");
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
        var btn = e.currentTarget;
        var active = btn.classList.toggle("soloed");
        m.soloed = active;
        msgAction(active ? "solo" : "unsolo", name);
      });

      /* ── expand row ── */
      var expTr = document.createElement("tr");
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
          var act = btn.dataset.act;
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
  var _msgSceneFilter = new Set();

  function renderMsgSceneFilter() {
    var filterBar = $("#msgSceneFilter");
    if (!filterBar) return;
    var dev = getActiveDev();
    var sceneNames = dev ? Object.keys(dev.scenes) : [];

    // Remove old scene chips (keep label + All button)
    Array.from(filterBar.querySelectorAll(".msg-filter-chip:not(.msg-filter-all)")).forEach(function (c) { c.remove(); });

    if (sceneNames.length === 0) {
      filterBar.style.display = "none";
      _msgSceneFilter.clear();
      return;
    }
    filterBar.style.display = "flex";

    sceneNames.forEach(function (sname) {
      var chip = document.createElement("button");
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
    var allBtn = filterBar.querySelector(".msg-filter-all");
    if (allBtn) {
      allBtn.onclick = function () {
        _msgSceneFilter.clear();
        applyMsgSceneFilter();
      };
    }

    applyMsgSceneFilter();
  }

  function applyMsgSceneFilter() {
    var filterBar = $("#msgSceneFilter");
    var allBtn = filterBar ? filterBar.querySelector(".msg-filter-all") : null;
    var isAll = _msgSceneFilter.size === 0;

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
      var dataRow = row.classList.contains("msg-data-row") ? row : document.getElementById("mr-" + row.id.replace("me-", ""));
      if (!dataRow) return;
      var rowScenes = (dataRow.dataset.msgScenes || "").split("\t").filter(Boolean);
      var visible = isAll || rowScenes.some(function (s) { return _msgSceneFilter.has(s); });
      row.style.display = visible ? "" : "none";
    });
  }

  function toggleMsgExp(name) {
    var dataRow = document.getElementById("mr-" + name);
    var expRow  = document.getElementById("me-" + name);
    if (!dataRow || !expRow) return;
    var isOpen = expRow.classList.contains("visible");
    expRow.classList.toggle("visible", !isOpen);
    dataRow.classList.toggle("open", !isOpen);
  }

  /* ── Sensor categories and hints ── */
  var SENSOR_CATEGORIES = [
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
    { id: "gyroscope", label: "Gyroscope", hint: "Rotational velocity around each axis",
      sensors: [
        { value: "gyroX", label: "Raw X", hint: "Rolling" },
        { value: "gyroY", label: "Raw Y", hint: "Pitching" },
        { value: "gyroZ", label: "Raw Z", hint: "Spinning" },
        { value: "gyroLength", label: "Magnitude", hint: "Overall rotation speed" }
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
  var SENSOR_HINTS = {};
  var SENSOR_TO_CAT = {};
  SENSOR_CATEGORIES.forEach(function (cat) {
    cat.sensors.forEach(function (s) {
      SENSOR_HINTS[s.value] = s.hint;
      SENSOR_TO_CAT[s.value] = cat.id;
    });
  });

  function initSensorPicker(catId, valId, catHintId, valHintId) {
    var catEl = $("#" + catId), valEl = $("#" + valId);
    var catHint = $("#" + catHintId), valHint = $("#" + valHintId);
    if (!catEl || !valEl) return;

    // Populate category dropdown
    SENSOR_CATEGORIES.forEach(function (cat) {
      if (cat.advanced) return; // hidden by default
      var opt = document.createElement("option");
      opt.value = cat.id; opt.textContent = cat.label;
      if (cat.advanced) { opt.className = "quat-cat-option"; opt.hidden = true; }
      catEl.appendChild(opt);
    });

    function populateSensors(catIdVal) {
      valEl.innerHTML = "";
      var none = document.createElement("option");
      none.value = ""; none.textContent = "\u2014 pick \u2014";
      valEl.appendChild(none);
      var cat = SENSOR_CATEGORIES.filter(function (c) { return c.id === catIdVal; })[0];
      if (!cat) return;
      cat.sensors.forEach(function (s) {
        var opt = document.createElement("option");
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
        var catIdVal = SENSOR_TO_CAT[sensorValue] || "";
        if (catIdVal) {
          // Ensure cat option exists (for advanced)
          if (!catEl.querySelector('option[value="' + catIdVal + '"]')) {
            var cat = SENSOR_CATEGORIES.filter(function (c) { return c.id === catIdVal; })[0];
            if (cat) {
              var opt = document.createElement("option");
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

  var msgPicker = initSensorPicker("msgCategory", "msgValue", "msgCategoryHint", "msgValueHint");
  var directPicker = initSensorPicker("directCategory", "directValue", "directCategoryHint", "directValueHint");

  /* ── String mode: show/hide Low/High vs String input ── */
  function updateStringMode() {
    var isString = ($("#msgValue").value === "string");
    var loEl     = $("#msgLow"), hiEl = $("#msgHigh");
    var loFg     = loEl ? loEl.closest(".form-group") : null;
    var hiFg     = hiEl ? hiEl.closest(".form-group") : null;
    var strGrp   = $("#msgStringGroup");
    if (loFg)  loFg.style.display  = isString ? "none" : "";
    if (hiFg)  hiFg.style.display  = isString ? "none" : "";
    if (strGrp) strGrp.style.display = isString ? "" : "none";
  }
  var _msgValueEl = $("#msgValue");
  if (_msgValueEl) _msgValueEl.addEventListener("change", updateStringMode);
  updateStringMode();

  /* ── String registration (send string to device, get back str name) ── */
  var _strRegisterCallback = null;
  function registerString(strValue) {
    return new Promise(function (resolve, reject) {
      _strRegisterCallback = resolve;
      var _timeout = setTimeout(function () {
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

  /* ── Gate picker (flat source dropdown) ── */
  function initGatePicker(sourceId, oriId, modeId, loId, hiId, hintId, oriGroupId, loGroupId, hiGroupId, opts) {
    var srcEl = $("#" + sourceId), oriEl = $("#" + oriId);
    var modeEl = $("#" + modeId), loEl = $("#" + loId), hiEl = $("#" + hiId);
    var hintEl = $("#" + hintId);
    var oriGroup = $("#" + oriGroupId), loGroup = $("#" + loGroupId), hiGroup = $("#" + hiGroupId);
    if (!srcEl || !modeEl) return null;

    opts = opts || {};
    var isScenePicker = !!opts.scene;

    // Labels that change depending on mode
    var loLabel = loGroup ? loGroup.querySelector("label") : null;
    var hiLabel = hiGroup ? hiGroup.querySelector("label") : null;

    // Populate flat source dropdown: none, Orientation, then all data streams grouped by category
    var noneOpt = document.createElement("option");
    noneOpt.value = ""; noneOpt.textContent = "\u2014 none \u2014";
    srcEl.appendChild(noneOpt);
    var oriOpt = document.createElement("option");
    oriOpt.value = "ori"; oriOpt.textContent = "Ori";
    srcEl.appendChild(oriOpt);
    SENSOR_CATEGORIES.forEach(function (cat) {
      if (cat.advanced) return;
      var grp = document.createElement("optgroup");
      grp.label = cat.label;
      cat.sensors.forEach(function (s) {
        var opt = document.createElement("option");
        opt.value = s.value; opt.textContent = s.label;
        grp.appendChild(opt);
      });
      srcEl.appendChild(grp);
    });

    // Message-value gate sources (optgroup, populated dynamically)
    var msgOptGroup = document.createElement("optgroup");
    msgOptGroup.label = "Message Values";
    msgOptGroup.className = "gate-msg-sources";
    msgOptGroup.style.display = "none";
    srcEl.appendChild(msgOptGroup);

    function isEdgeMode() {
      var m = modeEl.value;
      return m === "rising" || m === "falling";
    }

    function updateLabels() {
      if (!isScenePicker) return;
      var edge = isEdgeMode();
      if (loLabel) loLabel.textContent = edge ? "Trigger" : "Lower";
      if (hiLabel) hiLabel.textContent = edge ? "Delta" : "Upper";
      if (loEl) loEl.placeholder = edge ? "threshold" : "\u2265";
      if (hiEl) hiEl.placeholder = edge ? "min \u0394" : "\u2264";
    }

    function updateVisibility() {
      var v = srcEl.value;
      var isOri = (v === "ori");
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
        var risingOpt  = modeEl.querySelector('option[value="rising"]');
        var fallingOpt = modeEl.querySelector('option[value="falling"]');
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
        var mode = modeEl.value;
        if (!mode) return null;
        var src;
        if (srcEl.value === "ori") {
          var oriName = oriEl.value.trim();
          if (!oriName) return null;
          src = "ori:" + oriName;
        } else {
          src = srcEl.value;
          if (!src) return null;
        }
        var lo = loEl.value.trim();
        var hi = hiEl.value.trim();
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
          var found = false;
          for (var i = 0; i < msgOptGroup.children.length; i++) {
            if (msgOptGroup.children[i].value === gateSrc) { found = true; break; }
          }
          if (!found) {
            var opt = document.createElement("option");
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
            var opt = document.createElement("option");
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

  var msgGatePicker = initGatePicker(
    "msgGateSource", "msgGateOri", "msgGateMode",
    "msgGateLo", "msgGateHi", "msgGateHint",
    "msgGateOriGroup", "msgGateLoGroup", "msgGateHiGroup"
  );

  var sceneGatePicker = initGatePicker(
    "sceneGateSource", "sceneGateOri", "sceneGateMode",
    "sceneGateLo", "sceneGateHi", "sceneGateHint",
    "sceneGateOriGroup", "sceneGateLoGroup", "sceneGateHiGroup",
    { scene: true }
  );

  function populateMsgForm(name, m) {
    $("#msgName").value = name;
    var valStr = m.value || m.val || "";
    // If value is a string pool reference (str1, str2, ...) show String category
    if (/^str\d+$/i.test(valStr)) {
      if (msgPicker) msgPicker.setValue("string");
      var strInp = $("#msgStringVal");
      if (strInp) strInp.value = m.string_val || valStr;
    } else {
      if (msgPicker) msgPicker.setValue(valStr);
      var strInp2 = $("#msgStringVal");
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
      var gs = m.gate_src || m.gate_source || "";
      var gm = m.gate_mode || "";
      var gl = m.gate_lo != null ? m.gate_lo : "";
      var gh = m.gate_hi != null ? m.gate_hi : "";
      // Legacy backward compat
      if (!gs && (m.ori_only || m.orionly)) { gs = "ori:" + (m.ori_only || m.orionly); gm = "only"; }
      else if (!gs && (m.ori_not || m.orinot)) { gs = "ori:" + (m.ori_not || m.orinot); gm = "not"; }
      else if (!gs && m.ternori) { gs = "ori:" + m.ternori; gm = "toggle"; }
      msgGatePicker.setValue(gs, gm, gl, gh);
      // Auto-show gate section if gate is populated
      if (gs && gm) {
        var sec = $("#msgGateSection"); if (sec) sec.style.display = "";
        var chk = $("#chkShowGate"); if (chk) chk.checked = true;
      }
    }
    updateMsgPreview();
    /* scroll to form — switch to messages tab */
    $(".nav-btn[data-section='messages']").click();
    $("#msgName").focus();
  }

  function msgAction(act, name) {
    var template;
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
          var dev = getActiveDev();
          if (dev) { delete dev.messages[name]; renderMsgTable(); refreshAllDropdowns(); }
        }
      }
    });
  }

  /* ═══════════════════════════════════════════
     SCENE TABLE
     ═══════════════════════════════════════════ */

  function renderSceneTable() {
    var dev = getActiveDev();
    var tbody = $("#sceneTableBody");
    tbody.innerHTML = "";
    if (!dev || Object.keys(dev.scenes).length === 0) {
      tbody.innerHTML = '<tr><td colspan="4"><div class="empty-state"><div class="empty-icon">○</div><div class="empty-text">No scenes tracked yet.</div><div class="empty-sub">Click the device tab → Query to load from device, or create one below.</div></div></td></tr>';
      return;
    }
    Object.keys(dev.scenes).forEach(function (name) {
      var p = dev.scenes[name];
      var msgsStr = p.msgs || "";
      if (typeof msgsStr === "string") msgsStr = msgsStr.replace(/\+/g, ", ");
      var isRunning = !!p.running;
      var rowState  = isRunning ? "run" : "stop";
      var expState  = isRunning ? "run-exp" : "stop-exp";
      var pillClass = isRunning ? "scene-pill-run" : "scene-pill-stop";
      var pillLabel = isRunning ? "ACTIVE" : "STOPPED";
      var overrides = (p.override || "—").replace(/\+/g, ", ");
      var adrMode   = esc(p.adrMode || p.adrmode || p.adr_mode || "fallback");
      var ip        = esc(p.ip || "—");
      var port      = esc(p.port || "9000");
      var adr       = esc(p.adr || "—");
      var period    = esc(p.period || "50") + "ms";

      /* ── data row ── */
      var tr = document.createElement("tr");
      tr.className = "scene-data-row " + rowState;
      tr.id = "sr-" + name;
      tr.dataset.sceneName = name;
      tr.innerHTML =
        '<td><span class="scene-pill ' + pillClass + '"><span class="scene-dot"></span>' + pillLabel + '</span></td>' +
        '<td><span class="scene-name" title="' + esc(name) + '">' + esc(name.length > 10 ? name.slice(0, 10) + '…' : name) + '</span></td>' +
        '<td><span class="scene-msg-cell" id="smsg-' + esc(name) + '">' + esc(msgsStr || "—") + '</span></td>' +
        '<td><div class="scene-acts" onclick="event.stopPropagation()">' +
          '<button class="scene-btn scene-btn-go" data-act="start" title="Start scene">▶</button>' +
          '<button class="scene-btn scene-btn-stp" data-act="stop"  title="Stop scene">■</button>' +
        '</div></td>';
      tr.addEventListener("click", function (e) {
        if (e.metaKey || e.ctrlKey) { populateSceneForm(name, p); return; }
        toggleSceneExp(name);
      });
      tr.querySelectorAll(".scene-btn").forEach(function (btn) {
        btn.addEventListener("click", function () { sceneAction(btn.dataset.act, name); });
      });

      /* ── expand row ── */
      var expTr = document.createElement("tr");
      expTr.className = "scene-exp-row " + expState;
      expTr.id = "se-" + name;
      var gateExpItem = "";
      if (p.gate_src && p.gate_mode) {
        var gateIsEdge = (p.gate_mode === "rising" || p.gate_mode === "falling");
        var gateSuffix = "";
        if (p.gate_lo != null && p.gate_lo !== "") gateSuffix += (gateIsEdge ? " trigger:" : " \u2265") + esc(String(p.gate_lo));
        if (p.gate_hi != null && p.gate_hi !== "") gateSuffix += (gateIsEdge ? " delta:" : " \u2264") + esc(String(p.gate_hi));
        gateExpItem = '<span class="scene-exp-item"><span class="scene-exp-label">scene gate</span><span class="scene-exp-val">' + esc(p.gate_src) + ' ' + esc((p.gate_mode || "").toUpperCase()) + gateSuffix + '</span></span>';
      }
      expTr.innerHTML =
        '<td colspan="4"><div class="scene-exp-inner" id="sei-' + esc(name) + '">' +
          '<span class="scene-exp-item"><span class="scene-exp-label">overrides</span><span class="scene-exp-val">' + esc(overrides) + '</span></span>' +
          '<span class="scene-exp-item"><span class="scene-exp-label">address</span><span class="scene-exp-val">' + adr + '</span></span>' +
          '<span class="scene-exp-item"><span class="scene-exp-label">mode</span><span class="scene-exp-val">' + adrMode + '</span></span>' +
          '<span class="scene-exp-item"><span class="scene-exp-label">ip</span><span class="scene-exp-val">' + ip + '</span></span>' +
          '<span class="scene-exp-item"><span class="scene-exp-label">port</span><span class="scene-exp-val">' + port + '</span></span>' +
          '<span class="scene-exp-item"><span class="scene-exp-label">period</span><span class="scene-exp-val">' + period + '</span></span>' +
          gateExpItem +
          '<span class="scene-exp-item scene-exp-actions" onclick="event.stopPropagation()">' +
            '<button class="scene-exp-act"   data-act="edit"       title="Edit in form">EDIT</button>' +
            '<span class="scene-exp-sep">·</span>' +
            '<button class="scene-exp-resync" data-act="info"      title="Re-query scene info">RESYNC?</button>' +
            '<span class="scene-exp-sep">·</span>' +
            '<button class="scene-exp-act"   data-act="enableAll"  title="Enable all messages">ENABLE ALL</button>' +
            '<span class="scene-exp-sep">·</span>' +
            '<button class="scene-exp-act"   data-act="unsolo"     title="Unsolo all messages">UNSOLO</button>' +
            '<span class="scene-exp-sep">·</span>' +
            '<button class="scene-exp-act"   data-act="save"       title="Save to device NVS">SAVE</button>' +
            '<span class="scene-exp-sep">·</span>' +
            '<button class="scene-exp-act danger" data-act="delete" title="Delete scene">DELETE</button>' +
          '</span>' +
        '</div></td>';
      expTr.querySelectorAll("[data-act]").forEach(function (btn) {
        btn.addEventListener("click", function () {
          var act = btn.dataset.act;
          if (act === "edit") { populateSceneForm(name, p); }
          else { sceneAction(act, name); }
        });
      });

      tbody.appendChild(tr);
      tbody.appendChild(expTr);
    });
  }

  function toggleSceneExp(name) {
    var dataRow  = document.getElementById("sr-" + name);
    var expRow   = document.getElementById("se-" + name);
    var msgEl    = document.getElementById("smsg-" + name);
    var expInner = document.getElementById("sei-" + name);
    if (!dataRow || !expRow) return;
    var isOpen = expRow.classList.contains("visible");
    if (!isOpen) {
      var clipped = msgEl && msgEl.scrollWidth > msgEl.clientWidth;
      var existing = expInner.querySelector(".scene-exp-all");
      if (existing) existing.remove();
      if (clipped) {
        var allMsgs = document.createElement("span");
        allMsgs.className = "scene-exp-all";
        allMsgs.innerHTML =
          '<span class="scene-exp-label">all messages</span>' +
          '<span class="scene-exp-val">' + esc(msgEl.textContent) + '</span>';
        expInner.appendChild(allMsgs);
      }
    }
    expRow.classList.toggle("visible", !isOpen);
    dataRow.classList.toggle("open", !isOpen);
  }

  function populateSceneForm(name, p) {
    $("#sceneName").value = name;
    $("#scenePeriod").value = p.period || "50";
    $("#sceneAdrMode").value = p.adrMode || p.adrmode || p.adr_mode || "fallback";
    $("#sceneIP").value = p.ip || "";
    $("#scenePort").value = p.port || "9000";
    $("#sceneAdr").value = p.adr || "";
    $("#sceneLow").value = p.low || "";
    $("#sceneHigh").value = p.high || "";
    /* Accept both legacy "+" and canonical comma-separated override replies. */
    var ov = (p.override || "").split(/[+,]/).map(function (s) { return s.trim(); }).filter(Boolean);
    $("#ovIP").checked = ov.indexOf("ip") !== -1;
    $("#ovPort").checked = ov.indexOf("port") !== -1;
    $("#ovAdr").checked = ov.indexOf("adr") !== -1;
    $("#ovLow").checked = ov.indexOf("low") !== -1;
    $("#ovHigh").checked = ov.indexOf("high") !== -1;
    /* Scene gate */
    if (sceneGatePicker) {
      var gs = p.gate_src || p.gate_source || "";
      var gm = p.gate_mode || "";
      var gl = p.gate_lo != null ? p.gate_lo : "";
      var gh = p.gate_hi != null ? p.gate_hi : "";
      if (gs && gm) {
        sceneGatePicker.setValue(gs, gm, gl, gh);
        var sec = $("#sceneGateSection"); if (sec) sec.style.display = "";
        var chk = $("#chkShowGate"); if (chk) chk.checked = true;
      } else {
        sceneGatePicker.clear();
      }
    }
    /* scroll to form — switch to scenes tab */
    $(".nav-btn[data-section='scenes']").click();
    $("#sceneName").focus();
  }

  function sceneAction(act, name) {
    var template;
    switch (act) {
      case "start":     template = "/annieData/{device}/scene/{name}/start"; break;
      case "stop":      template = "/annieData/{device}/scene/{name}/stop"; break;
      case "info":      template = "/annieData/{device}/scene/{name}/info"; break;
      case "enableAll": template = "/annieData/{device}/scene/{name}/enableAll"; break;
      case "unsolo":    template = "/annieData/{device}/scene/{name}/unsolo"; break;
      case "save":      sendCmd(addr("/annieData/{device}/save/scene"), name); return;
      case "delete":    template = "/annieData/{device}/scene/{name}/delete"; break;
      default: return;
    }
    sendCmd(addr(template, name), null).then(function (res) {
      if (res.status === "ok") {
        toast(act + ": " + name, "success");
        var dev = getActiveDev();
        if (act === "start" && dev && dev.scenes[name]) { dev.scenes[name].running = true;  renderSceneTable(); }
        if (act === "stop"  && dev && dev.scenes[name]) { dev.scenes[name].running = false; renderSceneTable(); }
        if (act === "delete") {
          if (dev) { delete dev.scenes[name]; renderSceneTable(); refreshAllDropdowns(); }
        }
      }
    });
  }

  /* ═══════════════════════════════════════════
     BULK ACTIONS — OSC pattern matching
     ═══════════════════════════════════════════ */

  /** Returns true if s contains OSC pattern metacharacters. */
  function hasPattern(s) { return /[*?\[{]/.test(s); }

  /** Add/remove .bulk-match class on table rows based on OSC pattern. */
  function highlightBulkMatches(pattern, rowSelector, nameAttr) {
    document.querySelectorAll(rowSelector).forEach(function (row) {
      var name = row.dataset[nameAttr];
      if (!name) return;
      row.classList.toggle("bulk-match", !!(pattern && oscPatternMatch(pattern, name)));
    });
  }

  /** Highlight the input when it contains a pattern. */
  function updatePatternHint(inputEl, hintEl, registry, applyBtn) {
    var val = inputEl.value.trim();
    if (hasPattern(val)) {
      inputEl.classList.add("has-pattern");
      var count = 0;
      if (registry) {
        var names = Object.keys(registry);
        count = names.filter(function (n) {
          return oscPatternMatch(val, n);
        }).length;
        hintEl.textContent = count + " match" + (count !== 1 ? "es" : "");
      }
      if (applyBtn) applyBtn.style.display = count > 0 ? "" : "none";
    } else {
      inputEl.classList.remove("has-pattern");
      hintEl.textContent = "";
      if (applyBtn) applyBtn.style.display = "";
    }
  }

  /**
   * Simple client-side OSC pattern matcher for preview hints.
   * Supports * ? [charset] {alt1,alt2}. Case-insensitive.
   */
  function oscPatternMatch(pattern, text) {
    // Convert OSC pattern to a JS regex.
    var re = "^";
    var i = 0;
    var p = pattern.toLowerCase();
    while (i < p.length) {
      var c = p[i];
      if (c === "*") { re += ".*"; i++; }
      else if (c === "?") { re += "."; i++; }
      else if (c === "[") {
        var j = p.indexOf("]", i);
        if (j < 0) { re += "\\["; i++; continue; }
        var inner = p.substring(i + 1, j);
        if (inner[0] === "!") inner = "^" + inner.substring(1);
        re += "[" + inner + "]";
        i = j + 1;
      }
      else if (c === "{") {
        var j2 = p.indexOf("}", i);
        if (j2 < 0) { re += "\\{"; i++; continue; }
        var alts = p.substring(i + 1, j2).split(",").map(function (a) {
          return a.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
        });
        re += "(?:" + alts.join("|") + ")";
        i = j2 + 1;
      }
      else {
        re += c.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
        i++;
      }
    }
    re += "$";
    try { return new RegExp(re, "i").test(text); }
    catch (e) { return false; }
  }

  /* ── Message bulk action ── */
  (function () {
    var inp    = $("#msgBulkPattern");
    var sel    = $("#msgBulkAction");
    var setVal = $("#msgBulkSetVal");
    var btn    = $("#btnMsgBulk");
    var hint   = $("#msgBulkHint");
    if (!inp || !btn) return;

    var MSG_SET = {
      "set-ip":    { key: "ip",    ph: "e.g. 192.168.1.50" },
      "set-port":  { key: "port",  ph: "e.g. 9000" },
      "set-adr":   { key: "adr",   ph: "e.g. /sensor/x" },
      "set-scene": { key: "scene", ph: "scene name" }
    };

    sel.addEventListener("change", function () {
      var info = MSG_SET[sel.value];
      if (info) { setVal.style.display = "inline-block"; setVal.placeholder = info.ph; setVal.value = ""; }
      else { setVal.style.display = "none"; setVal.value = ""; }
    });

    inp.addEventListener("input", function () {
      var dev = getActiveDev();
      updatePatternHint(inp, hint, dev ? dev.messages : null, btn);
      highlightBulkMatches(inp.value.trim(), ".msg-data-row", "msgName");
    });

    btn.addEventListener("click", function () {
      var pattern = inp.value.trim();
      if (!pattern) { toast("Enter a pattern", "warn"); return; }
      var act  = sel.value;
      var info = MSG_SET[act];

      if (info) {
        var val = setVal.value.trim();
        if (!val) { toast("Enter a value to set", "warn"); return; }
        if (info.key === "ip") val = expandIp(val);
        var dev = getActiveDev();
        if (!dev) return;
        var matches = Object.keys(dev.messages).filter(function (n) { return oscPatternMatch(pattern, n); });
        if (!matches.length) { toast("No matches for pattern", "warn"); return; }
        var promises = matches.map(function (name) {
          return sendCmd(addr("/annieData/{device}/msg/{name}", name), info.key + ":" + val);
        });
        Promise.all(promises).then(function () {
          toast("Set " + info.key + " on " + matches.length + " message(s)", "success");
          matches.forEach(function (name) { if (dev.messages[name]) dev.messages[name][info.key] = val; });
          renderMsgTable();
        });
      } else {
        if (act === "delete" && !confirm("Delete all messages matching '" + pattern + "'?")) return;
        var template = "/annieData/{device}/msg/{name}/" + act;
        sendCmd(addr(template, pattern), null).then(function (res) {
          if (res.status === "ok") toast("Bulk " + act + ": " + pattern, "success");
        });
      }
    });
  }());

  /* ── Scene bulk action ── */
  (function () {
    var inp    = $("#sceneBulkPattern");
    var sel    = $("#sceneBulkAction");
    var setVal = $("#sceneBulkSetVal");
    var btn    = $("#btnSceneBulk");
    var hint   = $("#sceneBulkHint");
    if (!inp || !btn) return;

    var SCENE_SET = {
      "set-ip":     { key: "ip",     ph: "e.g. 192.168.1.50" },
      "set-port":   { key: "port",   ph: "e.g. 9000" },
      "set-adr":    { key: "adr",    ph: "e.g. /scene/addr" },
      "set-period": { key: "period", ph: "e.g. 50" }
    };

    sel.addEventListener("change", function () {
      var info = SCENE_SET[sel.value];
      if (info) { setVal.style.display = "inline-block"; setVal.placeholder = info.ph; setVal.value = ""; }
      else { setVal.style.display = "none"; setVal.value = ""; }
    });

    inp.addEventListener("input", function () {
      var dev = getActiveDev();
      updatePatternHint(inp, hint, dev ? dev.scenes : null, btn);
      highlightBulkMatches(inp.value.trim(), ".scene-data-row", "sceneName");
    });

    btn.addEventListener("click", function () {
      var pattern = inp.value.trim();
      if (!pattern) { toast("Enter a pattern", "warn"); return; }
      var act  = sel.value;
      var info = SCENE_SET[act];

      if (info) {
        var val = setVal.value.trim();
        if (!val) { toast("Enter a value to set", "warn"); return; }
        if (info.key === "ip") val = expandIp(val);
        var dev = getActiveDev();
        if (!dev) return;
        var matches = Object.keys(dev.scenes).filter(function (n) { return oscPatternMatch(pattern, n); });
        if (!matches.length) { toast("No matches for pattern", "warn"); return; }
        var promises;
        if (act === "set-period") {
          promises = matches.map(function (name) {
            return sendCmd(addr("/annieData/{device}/scene/{name}/period", name), '"' + val + '"');
          });
        } else {
          promises = matches.map(function (name) {
            return sendCmd(addr("/annieData/{device}/scene/{name}", name), info.key + ":" + val);
          });
        }
        Promise.all(promises).then(function () {
          toast("Set " + info.key + " on " + matches.length + " scene(s)", "success");
          matches.forEach(function (name) { if (dev.scenes[name]) dev.scenes[name][info.key] = val; });
          renderSceneTable();
        });
      } else {
        if (act === "delete" && !confirm("Delete all scenes matching '" + pattern + "'?")) return;
        var template = "/annieData/{device}/scene/{name}/" + act;
        sendCmd(addr(template, pattern), null).then(function (res) {
          if (res.status === "ok") toast("Bulk " + act + ": " + pattern, "success");
        });
      }
    });
  }());

  /* ── Ori explainer dismiss ── */
  (function () {
    var card = $("#oriExplainerCard");
    var btn  = $("#oriExplainerDismiss");
    if (!card || !btn) return;
    if (localStorage.getItem("oriExplainerDismissed") === "1") {
      card.style.display = "none";
    }
    btn.addEventListener("click", function () {
      card.style.display = "none";
      localStorage.setItem("oriExplainerDismissed", "1");
    });
  }());

  /* ── Ori Registration (immediate send) ──
     Sends /ori/register/{name} with color directly to the device.
     No local pending list — same pattern as messages and scenes.
  ─────────────────────────────────────────────────────────────── */


  (function () {
    var nameInput  = $("#oriName");
    var btnReg     = $("#btnRegisterOri");

    function doRegister() {
      var name = (nameInput ? nameInput.value : "").trim();
      if (!name) { toast("Ori name required", "error"); return; }
      if (!getActiveDev()) { toast("Select a device first", "error"); return; }
      sendCmd(addr("/annieData/{device}/ori/register/" + name), null).then(function (res) {
        if (res && res.status === "ok") {
          toast("Registered: " + name, "success");
          /* Immediately add to local state so tracker updates without waiting for query */
          var dev = getActiveDev();
          if (dev && !dev.oris[name]) {
            dev.oris[name] = { samples: 0, active: false };
            renderOriTable();
            refreshAllDropdowns();
          }
          /* Query can correct/confirm the state if the device responds */
          sendCmd(addr("/annieData/{device}/ori/list"), null);
        } else {
          toast("Register failed: " + (res && res.message ? res.message : "unknown"), "error");
        }
      });
    }

    if (btnReg) btnReg.addEventListener("click", doRegister);
    if (nameInput) nameInput.addEventListener("keydown", function (e) {
      if (e.key === "Enter") { e.preventDefault(); doRegister(); }
    });
  }());

  /* ── Ori Recording UI ──
     Start/Stop/Cancel recording session with live sample counter.
     Polls /ori/record/status every 500ms while active.
  ─────────────────────────────────────────────────────────────── */

  (function () {
    var nameInput = $("#oriName");
    var recBtn    = $("#btnRecordStart");
    var _recording = false;

    function setRecording(active) {
      _recording = active;
      if (!recBtn) return;
      if (active) {
        recBtn.textContent = "■ Stop Recording";
        recBtn.classList.remove("btn-primary");
        recBtn.classList.add("btn-stop");
        if (nameInput) nameInput.disabled = true;
      } else {
        recBtn.textContent = "▶ Start Recording";
        recBtn.classList.remove("btn-stop");
        recBtn.classList.add("btn-primary");
        if (nameInput) nameInput.disabled = false;
      }
    }

    if (recBtn) recBtn.addEventListener("click", function () {
      if (!getActiveDev()) { toast("Select a device first", "error"); return; }
      if (!_recording) {
        var name = (nameInput ? nameInput.value : "").trim();
        if (!name) { toast("Ori name required", "error"); return; }
        sendCmd(addr("/annieData/{device}/ori/record/start/" + name), null).then(function (res) {
          if (res && res.status === "ok") {
            toast("Recording: " + name, "info");
            setRecording(true);
          } else {
            toast("Start failed: " + (res && res.message ? res.message : ""), "error");
          }
        });
      } else {
        sendCmd(addr("/annieData/{device}/ori/record/stop"), null).then(function (res) {
          setRecording(false);
          if (res && res.status === "ok") {
            toast("Saved: " + (res.message || ""), "success");
          }
          sendCmd(addr("/annieData/{device}/ori/list"), null);
        });
      }
    });
  }());

  /* ═══════════════════════════════════════════
     ORI TABLE
     ═══════════════════════════════════════════ */

  function renderOriTable() {
    var dev = getActiveDev();
    var tbody = $("#oriTableBody");
    if (!tbody) return;
    tbody.innerHTML = "";

    var devOriNames = dev ? Object.keys(dev.oris) : [];

    if (devOriNames.length === 0) {
      tbody.innerHTML = '<tr><td colspan="3"><div class="empty-state empty-state-inline">No orientations tracked yet — Query device or register one below.</div></td></tr>';
      return;
    }

    devOriNames.forEach(function (name) {
      var o = dev.oris[name];
      var isUnsampled = (o.samples === 0);
      var isActive    = !!o.active;

      /* Status pill */
      var statusHtml;
      if (isActive) {
        statusHtml = '<span class="ori-pill ori-pill-active"><span class="ori-dot"></span>ACTIVE</span>';
      } else if (isUnsampled) {
        statusHtml = '<span class="ori-pill ori-pill-pending">PENDING</span>';
      } else {
        statusHtml = '<span class="ori-pill ori-pill-idle">—</span>';
      }

      var tr = document.createElement("tr");
      tr.className = "ori-data-row" + (isActive ? " active" : "") + (isUnsampled ? " pending" : "");
      tr.dataset.oriName = name;
      tr.style.cursor = "pointer";
      tr.innerHTML =
        '<td class="ori-status-cell">' + statusHtml + '</td>' +
        '<td class="ori-name-cell">' + esc(name) + '</td>';

      tr.addEventListener("click", function () {
        var oriNameEl = $("#oriName");
        if (oriNameEl) oriNameEl.value = name;
      });
      tbody.appendChild(tr);
    });
  }

  function oriAction(act, name) {
    switch (act) {
      case "reset":
        sendCmd(addr("/annieData/{device}/ori/reset/" + name), null).then(function (res) {
          if (res && res.status === "ok") {
            toast("Samples cleared: " + name, "success");
            var dev = getActiveDev();
            if (dev && dev.oris[name]) {
              dev.oris[name].samples = 0;
              dev.oris[name].useAxis = false;
              renderOriTable();
            }
          }
        });
        break;
      case "delete":
        sendCmd(addr("/annieData/{device}/ori/delete/" + name), null).then(function (res) {
          if (res.status === "ok") {
            toast("Deleted: " + name, "success");
            var dev = getActiveDev();
            if (dev) { delete dev.oris[name]; renderOriTable(); refreshAllDropdowns(); }
          }
        });
        break;
    }
  }

  function showOriDetails(text) {
    var card = $("#oriDetailsCard");
    var content = $("#oriDetailsContent");
    if (!card || !content) return;
    card.style.display = "block";
    /* Parse v2 format:
       "name: samples=N axis=(x,y,z) tol=10.0deg q0=(...) ... color=(r,g,b) (ACTIVE)"
       or unsampled: "name: samples=0 (unsampled) color=(r,g,b)" */
    var html = "";
    var nm = text.match(/^(\S+):/);
    if (nm) html += '<div class="ori-detail-row"><span class="ori-detail-label">Name</span><span>' + esc(nm[1]) + '</span></div>';
    var sm = text.match(/samples=(\d+)/);
    if (sm) html += '<div class="ori-detail-row"><span class="ori-detail-label">Samples</span><span>' + esc(sm[1]) + '</span></div>';
    var axM = text.match(/axis=\(([^)]+)\)/);
    if (axM) html += '<div class="ori-detail-row"><span class="ori-detail-label">Axis</span><span class="cell-mono">(' + esc(axM[1]) + ')</span></div>';
    if (/axis=fullQ/.test(text)) html += '<div class="ori-detail-row"><span class="ori-detail-label">Axis</span><span class="cell-mono">full quaternion</span></div>';
    var tolM = text.match(/tol=([\d.]+)deg/);
    if (tolM) html += '<div class="ori-detail-row"><span class="ori-detail-label">Tolerance</span><span>' + esc(tolM[1]) + '°</span></div>';
    /* Show first sample quaternion if present */
    var q0 = text.match(/q0=\(([^)]+)\)/);
    if (q0) html += '<div class="ori-detail-row"><span class="ori-detail-label">Sample q0</span><span class="cell-mono">(' + esc(q0[1]) + ')</span></div>';
    if (/\(ACTIVE\)/i.test(text)) html += '<div class="ori-detail-row"><span class="ori-detail-label">Status</span><span class="ori-badge ori-badge-active">Active</span></div>';
    if (!html) html = '<p class="cell-mono">' + esc(text) + '</p>';
    content.innerHTML = html;
    /* Switch to ori tab */
    $(".nav-btn[data-section='ori']").click();
  }

  /* ═══════════════════════════════════════════
     DROPDOWN / DATALIST REFRESH
     ═══════════════════════════════════════════ */

  function refreshAllDropdowns() {
    var dev = getActiveDev();
    if (!dev) return;

    /* Message name datalists */
    var msgNames = Object.keys(dev.messages);
    ["#msgNameList", "#msgNameList2"].forEach(function (sel) {
      var dl = $(sel);
      if (!dl) return;
      dl.innerHTML = "";
      msgNames.forEach(function (n) {
        var o = document.createElement("option");
        o.value = n;
        dl.appendChild(o);
      });
    });

    /* Scene name datalists */
    var sceneNames = Object.keys(dev.scenes);
    ["#sceneNameList", "#sceneNameList2", "#sceneNameList3", "#sceneNameList4", "#sceneNameListSetAll"].forEach(function (sel) {
      var dl = $(sel);
      if (!dl) return;
      dl.innerHTML = "";
      sceneNames.forEach(function (n) {
        var o = document.createElement("option");
        o.value = n;
        dl.appendChild(o);
      });
    });

    /* Ori name datalist */
    var oriNames = Object.keys(dev.oris || {});
    var oriDl = $("#oriNameList");
    if (oriDl) {
      oriDl.innerHTML = "";
      oriNames.forEach(function (n) {
        var o = document.createElement("option");
        o.value = n;
        oriDl.appendChild(o);
      });
    }
  }

  /* ═══════════════════════════════════════════
     LOG / FEED
     ═══════════════════════════════════════════ */

  function esc(s) {
    var d = document.createElement("div");
    d.textContent = s;
    return d.innerHTML;
  }

  function renderLogEntry(entry) {
    var div = document.createElement("div");
    div.className = "log-entry";
    var tagClass = { send: "log-tag-send", recv: "log-tag-recv", bridge: "log-tag-bridge" };
    var argsStr = (entry.args || []).map(function (a) {
      if (a.type === "s") return '"' + a.value + '"';
      return a.value;
    }).join(" ");
    var destInfo = "";
    if (entry.dest) destInfo = " → " + entry.dest;
    if (entry.source && entry.dest) destInfo = " " + entry.source + " → " + entry.dest;
    else if (entry.source) destInfo = " ← " + entry.source;
    /* Determine device tag */
    var deviceTag = "";
    Object.keys(devices).forEach(function (id) {
      var d = devices[id];
      if (!entry.address) return;
      var seg = "/" + d.name + "/";
      var tail = "/" + d.name;
      if (entry.address.indexOf(seg) !== -1 || entry.address.slice(-tail.length) === tail) {
        deviceTag = '<span class="log-device-tag">' + d.name + '</span> ';
      }
    });
    div.innerHTML = [
      '<span class="log-time">' + entry.time + '</span>',
      '<span class="log-tag ' + (tagClass[entry.direction] || "") + '">' + entry.direction + '</span>',
      deviceTag,
      '<span class="log-address">' + esc(entry.address) + '</span>',
      argsStr ? '<span class="log-args">(' + esc(argsStr) + ')</span>' : '',
      destInfo ? '<span class="log-dest">' + esc(destInfo) + '</span>' : '',
    ].join("");
    return div;
  }

  function appendToFeed(entry) {
    /* Device filter */
    var devFilter = ($("#feedDeviceFilter").value || "").trim();
    if (devFilter && entry.address) {
      var seg = "/" + devFilter + "/";
      var tail = "/" + devFilter;
      if (entry.address.indexOf(seg) === -1 && entry.address.slice(-tail.length) !== tail) return;
    }

    /* Text filter */
    var filterText = ($("#feedFilter").value || "").trim().toLowerCase();
    if (filterText) {
      var fullText = entry.address + " " + JSON.stringify(entry.args);
      if (fullText.toLowerCase().indexOf(filterText) === -1) return;
    }

    var feedEl = $("#feedLog");
    feedEl.appendChild(renderLogEntry(entry));
    while (feedEl.children.length > MAX_LOG_ENTRIES) {
      feedEl.removeChild(feedEl.firstChild);
    }
    if ($("#feedAutoScroll").checked) {
      feedEl.scrollTop = feedEl.scrollHeight;
    }
  }

  /* ── Message counter ── */
  var msgCount = 0;
  var rateCounter = 0;
  var lastRateCheck = Date.now();

  setInterval(function () {
    var now = Date.now();
    var elapsed = (now - lastRateCheck) / 1000;
    var rate = elapsed > 0 ? Math.round(rateCounter / elapsed) : 0;
    rateCounter = 0;
    lastRateCheck = now;
    var countEl = $("#feedCount");
    var rateEl = $("#feedRate");
    if (countEl) countEl.textContent = msgCount + " messages";
    if (rateEl) rateEl.textContent = rate + " msg/s";
  }, 1000);

  /* ── Realtime messages ── */
  socket.on("osc_message", function (entry) {
    /* String registration reply: /reply{device}/str/registered */
    if (entry.address && entry.address.indexOf("/str/registered") >= 0) {
      var strName = (entry.args && entry.args[0]) ? String(entry.args[0]) : "";
      if (_strRegisterCallback && strName) {
        var cb = _strRegisterCallback;
        _strRegisterCallback = null;
        cb(strName);
      }
      return;
    }
    msgCount++;
    rateCounter++;
    appendToFeed(entry);
    /* Auto-parse replies into registry */
    var prevMsgCount = 0, prevSceneCount = 0;
    var matchedDevId = "";
    Object.keys(devices).forEach(function (id) {
      var d = devices[id];
      if (!entry.address) return;
      var seg = "/" + d.name + "/";
      var tail = "/" + d.name;
      if (entry.address.indexOf(seg) !== -1 || entry.address.slice(-tail.length) === tail) matchedDevId = id;
    });
    if (!matchedDevId) matchedDevId = activeDeviceId;
    if (matchedDevId && devices[matchedDevId] && /\/list\/(all|msgs|messages)/i.test(entry.address || "")) {
      var preDev = devices[matchedDevId];
      prevMsgCount = Object.keys(preDev.messages || {}).length;
      prevSceneCount = Object.keys(preDev.scenes || {}).length;
    }
    parseReplyIntoRegistry(entry);
    _onFlushReply(entry);
    /* Show query feedback toast after list/all replies add new data */
    if (matchedDevId && devices[matchedDevId] && /\/list\/(all|msgs|messages)/i.test(entry.address || "")) {
      var postDev = devices[matchedDevId];
      var newMsgCount = Object.keys(postDev.messages || {}).length;
      var newSceneCount = Object.keys(postDev.scenes || {}).length;
      if ((newMsgCount > 0 || newSceneCount > 0) && (newMsgCount !== prevMsgCount || newSceneCount !== prevSceneCount)) {
        showToast("Loaded " + newMsgCount + " message" + (newMsgCount !== 1 ? "s" : "") + " and " + newSceneCount + " scene" + (newSceneCount !== 1 ? "es" : "") + " from device.", "success");
      }
    }
  });

  /* ═══════════════════════════════════════════
     DASHBOARD
     ═══════════════════════════════════════════ */

  /* Use event delegation so .qbtn[data-cmd] buttons work in the starred
     section too (cloneNode doesn't copy event listeners). */
  document.addEventListener("click", function (e) {
    var btn = e.target.closest(".qbtn[data-cmd]");
    if (!btn) return;
    var confirmMsg = btn.dataset.confirm;
    var cmd = btn.dataset.cmd;
    var template = CMD_ADDRESSES[cmd];
    if (!template) return;
    var address = addr(template);
    var payload = btn.dataset.payload || null;
    var doSend = function () {
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

  /* Status config */
  $("#btnStatusConfig").addEventListener("click", function () {
    var ip = ($("#statusIP").value || "").trim();
    var port = ($("#statusPort").value || "").trim();
    var adr = ($("#statusAdr").value || "").trim();
    if (!ip || !port) { toast("IP and port are required", "error"); return; }
    var cfg = "ip:" + ip + ", port:" + port;
    if (adr) cfg += ", adr:" + adr;
    var targets = getStatusConfigTargets();
    if (!targets.length) { toast("No device selected", "error"); return; }
    targets.forEach(function (id) {
      var d = devices[id];
      if (d) sendToDevice(id, "/annieData/" + d.name + "/status/config", cfg);
    });
    toast("Status config applied" + (targets.length > 1 ? " (" + targets.length + " devices)" : ""), "success");
  });

  $("#btnStatusLevel").addEventListener("click", function () {
    var lvl = $("#statusLevel").value;
    if (!lvl) return;
    var targets = getStatusConfigTargets();
    if (!targets.length) { toast("No device selected", "error"); return; }
    targets.forEach(function (id) {
      var d = devices[id];
      if (d) sendToDevice(id, "/annieData/" + d.name + "/status/level", lvl);
    });
    toast("Level set: " + lvl, "success");
  });

  /* ═══════════════════════════════════════════
     MESSAGES
     ═══════════════════════════════════════════ */

  /* Config preview update */
  function previewPair(key, val) {
    if (val.charAt(0) === "<") return key + "<" + val.substring(1);
    return key + ":" + val;
  }
  function updateMsgPreview() {
    var a = ($("#msgAdr") ? $("#msgAdr").value.trim() : "");
    var adrEl = $("#msgPreviewAdr");
    var cfgEl = $("#msgPreviewCfg");
    if (adrEl) adrEl.textContent = a ? previewPair("adr", a) : "(no address)";
    var parts = [];
    var v = $("#msgValue").value; if (v) parts.push(previewPair("value", v));
    var ip = $("#msgIP").value.trim(); if (ip) parts.push(previewPair("ip", ip));
    var port = $("#msgPort").value; if (port) parts.push(previewPair("port", port));
    var lo = $("#msgLow").value.trim(); if (lo) parts.push(previewPair("low", lo));
    var hi = $("#msgHigh").value.trim(); if (hi) parts.push(previewPair("high", hi));
    var pa = $("#msgScene").value.trim(); if (pa) parts.push(previewPair("scene", pa));
    if (msgGatePicker) {
      var gc = msgGatePicker.getConfig();
      if (gc) {
        parts.push(previewPair("gate_src", gc.gate_src));
        parts.push(previewPair("gate_mode", gc.gate_mode));
        if (gc.gate_lo) parts.push(previewPair("gate_lo", gc.gate_lo));
        if (gc.gate_hi) parts.push(previewPair("gate_hi", gc.gate_hi));
      }
    }
    if (cfgEl) cfgEl.textContent = parts.join(", ");
  }

  ["msgCategory", "msgValue", "msgIP", "msgPort", "msgAdr", "msgLow", "msgHigh", "msgScene", "msgStringVal", "msgGateSource", "msgGateOri", "msgGateMode", "msgGateLo", "msgGateHi"].forEach(function (id) {
    var el = $("#" + id);
    if (el) el.addEventListener("input", updateMsgPreview);
  });
  updateMsgPreview();

  /* Apply message (create/update) */
  $("#btnMsgApply").addEventListener("click", function () {
    var name = ($("#msgName").value || "").trim();
    if (!name) { toast("Message name required", "error"); return; }
    /* Resolve "name" shorthand: address → "/msgName", ori fields → "ori_msgName" */
    function resolveName(val, ori) {
      return val.toLowerCase() === "name" ? (ori ? "ori_" + name : "/" + name) : val;
    }
    /* Build key:value or key<refName pairs.
       If a field value starts with "<", the rest is a registry reference name
       and the separator becomes "<" instead of ":" (firmware key<refName syntax).
       Example: typing "<myScene" in the IP field sends "ip<myScene". */
    function cfgPair(key, val) {
      if (val.charAt(0) === "<") return key + "<" + val.substring(1);
      return key + ":" + val;
    }
    var isStringType = ($("#msgValue").value === "string");
    var strValRaw    = isStringType ? ($("#msgStringVal").value || "").trim() : "";
    if (isStringType && !strValRaw) { toast("String value required", "error"); return; }

    function doMsgApply(resolvedStrName) {
      var parts = [];
      var a = ($("#msgAdr") ? $("#msgAdr").value.trim() : ""); if (a) parts.push(cfgPair("adr", resolveName(a, false)));
      var v = isStringType ? resolvedStrName : $("#msgValue").value; if (v) parts.push(cfgPair("value", v));
      var ip = $("#msgIP").value.trim(); if (ip) parts.push(cfgPair("ip", ip));
      var port = $("#msgPort").value; if (port) parts.push(cfgPair("port", port));
      if (!isStringType) {
        var lo = $("#msgLow").value.trim(); if (lo) parts.push(cfgPair("low", lo));
        var hi = $("#msgHigh").value.trim(); if (hi) parts.push(cfgPair("high", hi));
      }
      var pa = $("#msgScene").value.trim(); if (pa) parts.push(cfgPair("scene", pa));
      if (msgGatePicker) {
        var gc = msgGatePicker.getConfig();
        if (gc) {
          var gSrc = gc.gate_src;
          if (gSrc.indexOf("ori:") === 0) {
            var oriPart = gSrc.substring(4);
            gSrc = "ori:" + resolveName(oriPart, true);
          }
          parts.push(cfgPair("gate_src", gSrc));
          parts.push(cfgPair("gate_mode", gc.gate_mode));
          if (gc.gate_lo) parts.push(cfgPair("gate_lo", gc.gate_lo));
          if (gc.gate_hi) parts.push(cfgPair("gate_hi", gc.gate_hi));
        }
      }
      var cfg = parts.join(", ");
      var address = addr("/annieData/{device}/msg/{name}", name);
      sendCmd(address, cfg || null).then(function (res) {
        if (res.status === "ok") {
          toast("Applied: " + name, "success");
          var dev = getActiveDev();
          if (dev) {
            var parsed = parseConfigString(cfg);
            if (isStringType) { parsed.string_val = strValRaw; }
            dev.messages[name] = parsed;
          /* Auto-register ori names from gate */
          if (msgGatePicker) {
            var gc2 = msgGatePicker.getConfig();
            if (gc2 && gc2.gate_src.indexOf("ori:") === 0) {
              var oriName = gc2.gate_src.substring(4);
              oriName = resolveName(oriName, true);
              if (oriName && !dev.oris[oriName]) {
                dev.oris[oriName] = { color: [255, 255, 255], samples: 0, pre_reg: true };
              }
            }
          }
          renderMsgTable();
          renderOriTable();
          refreshAllDropdowns();
        }
      }
    });
    }  // end doMsgApply

    // Register string on device first if needed, then apply
    if (isStringType && !/^str\d+$/i.test(strValRaw)) {
      registerString(strValRaw)
        .then(function (strName) { doMsgApply(strName); })
        .catch(function (e) { toast("String registration failed: " + e.message, "error"); });
    } else {
      doMsgApply(isStringType ? strValRaw : "");
    }
  });

  /* Clear form */
  $("#btnMsgClear").addEventListener("click", function () {
    ["msgName", "msgIP", "msgAdr", "msgLow", "msgHigh", "msgScene", "msgStringVal"].forEach(function (id) {
      var el = $("#" + id); if (el) el.value = "";
    });
    $("#msgValue").value = "";
    if (msgPicker) msgPicker.clear();
    if (msgGatePicker) msgGatePicker.clear();
    updateStringMode();
    updateMsgPreview();
  });

  /* Clone / Rename */
  $("#btnMsgClone").addEventListener("click", function () {
    var src = ($("#msgSrcName").value || "").trim();
    var dest = ($("#msgDestName").value || "").trim();
    if (!src || !dest) { toast("Both names required", "error"); return; }
    sendCmd(addr("/annieData/{device}/msg/clone"), src + ", " + dest).then(function (res) {
      if (res.status === "ok") toast("Cloned: " + src + " → " + dest, "success");
    });
  });

  $("#btnMsgRename").addEventListener("click", function () {
    var src = ($("#msgSrcName").value || "").trim();
    var dest = ($("#msgDestName").value || "").trim();
    if (!src || !dest) { toast("Both names required", "error"); return; }
    sendCmd(addr("/annieData/{device}/msg/rename"), src + ", " + dest).then(function (res) {
      if (res.status === "ok") {
        toast("Renamed: " + src + " → " + dest, "success");
        var dev = getActiveDev();
        if (dev && dev.messages[src]) {
          dev.messages[dest] = dev.messages[src];
          delete dev.messages[src];
          renderMsgTable();
          refreshAllDropdowns();
        }
      }
    });
  });

  /* ═══════════════════════════════════════════
     SCENES
     ═══════════════════════════════════════════ */

  /* Apply scene config helper */
  function applySceneConfig(thenStart) {
    var name = ($("#sceneName").value || "").trim();
    if (!name) { toast("Scene name required", "error"); return; }
    var period   = ($("#scenePeriod").value || "").trim();
    var mode     = ($("#sceneAdrMode").value || "").trim();
    var ip       = ($("#sceneIP").value || "").trim();
    var port     = ($("#scenePort").value || "").trim();
    var sceneAdr = ($("#sceneAdr").value || "").trim();
    var low      = ($("#sceneLow").value || "").trim();
    var high     = ($("#sceneHigh").value || "").trim();

    var ovParts = [];
    if ($("#ovIP").checked)   ovParts.push("ip");
    if ($("#ovPort").checked) ovParts.push("port");
    if ($("#ovAdr").checked)  ovParts.push("adr");
    if ($("#ovLow").checked)  ovParts.push("low");
    if ($("#ovHigh").checked) ovParts.push("high");

    function cfgPairS(key, val) {
      if (val.charAt(0) === "<") return key + "<" + val.substring(1);
      return key + ":" + val;
    }
    var cfgParts = [];
    if (ip)       cfgParts.push(cfgPairS("ip",  ip));
    if (port)     cfgParts.push(cfgPairS("port", port));
    if (sceneAdr) cfgParts.push(cfgPairS("adr",  sceneAdr));
    if (low)      cfgParts.push(cfgPairS("low",  low));
    if (high)     cfgParts.push(cfgPairS("high", high));
    if (period)   cfgParts.push("period:" + period);
    if (mode)     cfgParts.push("adrMode:" + mode);
    cfgParts.push("override:" + (ovParts.length ? ovParts.join("+") : "none"));

    var sceneGateConfig = null;
    if (sceneGatePicker) {
      sceneGateConfig = sceneGatePicker.getConfig();
      if (sceneGateConfig && sceneGateConfig.gate_src) {
        cfgParts.push("gate_src:" + sceneGateConfig.gate_src);
        if (sceneGateConfig.gate_mode) cfgParts.push("gate_mode:" + sceneGateConfig.gate_mode);
        if (sceneGateConfig.gate_lo)   cfgParts.push("gate_lo:"   + sceneGateConfig.gate_lo);
        if (sceneGateConfig.gate_hi)   cfgParts.push("gate_hi:"   + sceneGateConfig.gate_hi);
      }
    }

    var cfg = cfgParts.join(", ");
    var startStop = thenStart
      ? addr("/annieData/{device}/scene/{name}/start", name)
      : addr("/annieData/{device}/scene/{name}/stop",  name);

    sendCmd(addr("/annieData/{device}/scene/{name}", name), cfg).then(function () {
      sendCmd(startStop, null).then(function () {
        toast("Scene " + (thenStart ? "started" : "applied") + ": " + name, "success");
        var dev = getActiveDev();
        if (dev) {
          dev.scenes[name] = Object.assign(dev.scenes[name] || {}, {
            ip: ip, port: port, adr: sceneAdr, low: low, high: high,
            period: period, adrMode: mode, override: ovParts.join(", "),
            running: thenStart,
            gate_src:  sceneGateConfig ? (sceneGateConfig.gate_src  || "") : "",
            gate_mode: sceneGateConfig ? (sceneGateConfig.gate_mode || "") : "",
            gate_lo:   sceneGateConfig ? (sceneGateConfig.gate_lo   || "") : "",
            gate_hi:   sceneGateConfig ? (sceneGateConfig.gate_hi   || "") : ""
          });
          renderSceneTable();
          refreshAllDropdowns();
        }
      });
    });
  }

  $("#btnSceneApplyActive").addEventListener("click",   function () { applySceneConfig(true);  });
  $("#btnSceneApplyStopped").addEventListener("click",  function () { applySceneConfig(false); });

  /* Clear scene form */
  $("#btnSceneClear").addEventListener("click", function () {
    ["sceneName", "sceneIP", "sceneAdr", "sceneLow", "sceneHigh"].forEach(function (id) {
      $("#" + id).value = "";
    });
    $("#scenePeriod").value = "50";
    $("#sceneAdrMode").value = "fallback";
    ["ovIP", "ovPort", "ovAdr", "ovLow", "ovHigh"].forEach(function (id) {
      $("#" + id).checked = false;
    });
    if (sceneGatePicker) sceneGatePicker.clear();
    updateScenePreview();
  });

  /* Scene ops tabs */
  document.querySelectorAll(".scene-ops-tab-btn").forEach(function (btn) {
    btn.addEventListener("click", function () {
      document.querySelectorAll(".scene-ops-tab-btn").forEach(function (b) { b.classList.remove("active"); });
      document.querySelectorAll(".scene-ops-tab-pane").forEach(function (p) { p.classList.remove("active"); });
      btn.classList.add("active");
      var pane = document.getElementById("sceneOpsPane-" + btn.dataset.tab);
      if (pane) pane.classList.add("active");
    });
  });

  /* Add/Remove/Solo/Move messages */
  $("#btnSceneAddMsg").addEventListener("click", function () {
    var pname = ($("#sceneMsgScene").value || "").trim();
    var mnames = ($("#sceneMsgNames").value || "").trim();
    if (!pname || !mnames) { toast("Scene and message name(s) required", "error"); return; }
    sendCmd(addr("/annieData/{device}/scene/{name}/addMsg", pname), mnames).then(function (res) {
      if (res.status === "ok") toast("Added to " + pname, "success");
    });
  });

  $("#btnSceneRemoveMsg").addEventListener("click", function () {
    var pname = ($("#sceneMsgScene").value || "").trim();
    var mnames = ($("#sceneMsgNames").value || "").trim();
    if (!pname || !mnames) { toast("Scene and message name required", "error"); return; }
    sendCmd(addr("/annieData/{device}/scene/{name}/removeMsg", pname), mnames).then(function (res) {
      if (res.status === "ok") toast("Removed from " + pname, "success");
    });
  });

$("#btnSceneMove").addEventListener("click", function () {
    var mname = ($("#sceneMsgNames").value || "").trim();
    var pname = ($("#sceneMsgScene").value || "").trim();
    if (!pname || !mname) { toast("Message and scene name required", "error"); return; }
    sendCmd(addr("/annieData/{device}/move"), mname + ", " + pname).then(function (res) {
      if (res.status === "ok") toast("Moved: " + mname + " → " + pname, "success");
    });
  });

  $("#btnSceneSetAll").addEventListener("click", function () {
    var pname = ($("#sceneSetAllScene").value || "").trim();
    var cfg = ($("#sceneSetAllCfg").value || "").trim();
    if (!pname || !cfg) { toast("Scene and config string required", "error"); return; }
    sendCmd(addr("/annieData/{device}/scene/{name}/setAll", pname), cfg).then(function (res) {
      if (res.status === "ok") toast("setAll applied: " + pname, "success");
    });
  });

  /* Clone / Rename */
  $("#btnSceneClone").addEventListener("click", function () {
    var src = ($("#sceneSrcName").value || "").trim();
    var dest = ($("#sceneDestName").value || "").trim();
    if (!src || !dest) { toast("Both names required", "error"); return; }
    sendCmd(addr("/annieData/{device}/scene/clone"), src + ", " + dest).then(function (res) {
      if (res.status === "ok") toast("Cloned: " + src + " → " + dest, "success");
    });
  });

  $("#btnSceneRename").addEventListener("click", function () {
    var src = ($("#sceneSrcName").value || "").trim();
    var dest = ($("#sceneDestName").value || "").trim();
    if (!src || !dest) { toast("Both names required", "error"); return; }
    sendCmd(addr("/annieData/{device}/scene/rename"), src + ", " + dest).then(function (res) {
      if (res.status === "ok") {
        toast("Renamed: " + src + " → " + dest, "success");
        var dev = getActiveDev();
        if (dev && dev.scenes[src]) {
          dev.scenes[dest] = dev.scenes[src];
          delete dev.scenes[src];
          renderSceneTable();
          refreshAllDropdowns();
        }
      }
    });
  });

  /* ═══════════════════════════════════════════
     SEND DIRECT (from message editor)
     ═══════════════════════════════════════════ */

  $("#btnMsgDirect").addEventListener("click", function () {
    var name = ($("#msgName").value || "").trim();
    if (!name) { toast("Message name required", "error"); return; }
    function cfgPairD(key, val) {
      if (val.charAt(0) === "<") return key + "<" + val.substring(1);
      return key + ":" + val;
    }
    var isDirStrType = ($("#msgValue").value === "string");
    var dirStrRaw    = isDirStrType ? ($("#msgStringVal").value || "").trim() : "";
    if (isDirStrType && !dirStrRaw) { toast("String value required", "error"); return; }

    function doDirect(resolvedStrName) {
      var parts = [];
      var v = isDirStrType ? resolvedStrName : $("#msgValue").value; if (v) parts.push(cfgPairD("value", v));
      var ip = $("#msgIP").value.trim(); if (ip) parts.push(cfgPairD("ip", ip));
      var port = $("#msgPort").value; if (port) parts.push(cfgPairD("port", port));
      var a = ($("#msgAdr") ? $("#msgAdr").value.trim() : "");
      if (a) parts.push(cfgPairD("adr", a.toLowerCase() === "name" ? "/" + name : a));
      if (!isDirStrType) {
        var lo = $("#msgLow").value.trim(); if (lo) parts.push(cfgPairD("low", lo));
        var hi = $("#msgHigh").value.trim(); if (hi) parts.push(cfgPairD("high", hi));
      }
      /* Gate config — applied to the message only (not the scene) */
      if (msgGatePicker) {
        var gc = msgGatePicker.getConfig();
        if (gc) {
          parts.push(cfgPairD("gate_src", gc.gate_src));
          parts.push(cfgPairD("gate_mode", gc.gate_mode));
          if (gc.gate_lo) parts.push(cfgPairD("gate_lo", gc.gate_lo));
          if (gc.gate_hi) parts.push(cfgPairD("gate_hi", gc.gate_hi));
        }
      }
      parts.push("period:50");
      var cfg = parts.join(", ");

      var sceneName = ($("#msgScene").value || "").trim();
      var autoStart = !($("#chkDirectAutoStart") && !$("#chkDirectAutoStart").checked);

      sendCmd(addr("/annieData/{device}/direct/{name}", name), cfg).then(function (res) {
      if (res.status !== "ok") return;
      var dev = getActiveDev();
      if (!dev) { toast("Direct: " + name, "success"); return; }

      /* Update message tracker locally */
      dev.messages[name] = parseConfigString(cfg);

      /* Direct always creates a same-named scene and starts it.
         Register it locally, then rename if user specified a scene name. */
      var finalSceneName = (sceneName && sceneName !== name) ? sceneName : name;
      dev.scenes[name] = Object.assign(dev.scenes[name] || {}, {
        period: "50", running: autoStart
      });

      var finish = function () {
        if (!autoStart) {
          /* Stop the scene if auto-start is off */
          sendCmd(addr("/annieData/{device}/scene/{name}/stop", name), null);
          dev.scenes[finalSceneName] = Object.assign(dev.scenes[finalSceneName] || {}, { running: false });
        }
        renderMsgTable();
        renderSceneTable();
        refreshAllDropdowns();
        toast("Direct: " + name + (finalSceneName !== name ? " → scene: " + finalSceneName : ""), "success");
      };

      if (finalSceneName !== name) {
        /* Rename the auto-created scene to the user's scene name */
        sendCmd(addr("/annieData/{device}/scene/rename"), name + ", " + finalSceneName).then(function () {
          dev.scenes[finalSceneName] = Object.assign({}, dev.scenes[name], { running: autoStart });
          delete dev.scenes[name];
          finish();
        });
      } else {
        finish();
      }
    });
    }  // end doDirect

    if (isDirStrType && !/^str\d+$/i.test(dirStrRaw)) {
      registerString(dirStrRaw)
        .then(function (strName) { doDirect(strName); })
        .catch(function (e) { toast("String registration failed: " + e.message, "error"); });
    } else {
      doDirect(isDirStrType ? dirStrRaw : "");
    }
  });

  /* ═══════════════════════════════════════════
     ADVANCED
     ═══════════════════════════════════════════ */

  var repeatId = null;

  function getRawArgs() {
    var raw = ($("#rawArgs").value || "").trim() || null;
    if (raw && $("#rawSingleString") && $("#rawSingleString").checked) {
      return [raw];  // wrap in array to send as a single string argument
    }
    return raw;
  }

  $("#btnRawSend").addEventListener("click", function () {
    api("send", {
      host: ($("#rawHost").value || "").trim(),
      port: parseInt($("#rawPort").value, 10),
      address: ($("#rawAddress").value || "").trim(),
      args: getRawArgs(),
    }).then(function (res) {
      if (res.status === "ok") toast("Sent", "success");
    });
  });

  $("#btnRawRepeat").addEventListener("click", function () {
    api("send/repeat", {
      host: ($("#rawHost").value || "").trim(),
      port: parseInt($("#rawPort").value, 10),
      address: ($("#rawAddress").value || "").trim(),
      args: getRawArgs(),
      interval: parseInt($("#rawInterval").value, 10),
      id: "raw-repeat",
    }).then(function (res) {
      if (res.status === "ok") {
        repeatId = "raw-repeat";
        $("#btnRawRepeat").disabled = true;
        $("#btnRawStop").disabled = false;
        toast("Repeat started", "success");
      }
    });
  });

  $("#btnRawStop").addEventListener("click", function () {
    api("send/stop", { id: repeatId || "raw-repeat" }).then(function () {
      repeatId = null;
      $("#btnRawRepeat").disabled = false;
      $("#btnRawStop").disabled = true;
      toast("Repeat stopped", "info");
    });
  });

  /* Bridge */
  $("#btnBridgeStart").addEventListener("click", function () {
    api("bridge/start", {
      in_port: parseInt($("#bridgeInPort").value, 10),
      out_host: ($("#bridgeOutHost").value || "").trim(),
      out_port: parseInt($("#bridgeOutPort").value, 10),
      filter: ($("#bridgeFilter").value || "").trim(),
    }).then(function (res) {
      if (res.status === "ok") {
        toast("Bridge started", "success");
        $("#btnBridgeStop").disabled = false;
        refreshBridgeList();
      }
    });
  });

  $("#btnBridgeStop").addEventListener("click", function () {
    api("stop-all", {}).then(function () {
      toast("All bridges stopped", "info");
      $("#btnBridgeStop").disabled = true;
      $("#activeBridges").innerHTML = "";
    });
  });

  /* IMU Tare (device dropdown) */
  $("#devDdImuTare").addEventListener("click", function () {
    closeDevDropdown();
    sendCmd(addr("/annieData/{device}/tare"), null).then(function (res) {
      if (res.status === "ok") toast("IMU tare set", "success");
    });
  });

  function refreshBridgeList() {
    fetch("/api/status").then(function (r) { return r.json(); }).then(function (data) {
      var container = $("#activeBridges");
      container.innerHTML = "";
      var bridges = data.bridges || {};
      Object.keys(bridges).forEach(function (id) {
        var b = bridges[id];
        var div = document.createElement("div");
        div.className = "active-item";
        div.innerHTML = '<span><span class="active-item-dot"></span>' + b.in_port + ' → ' + b.out_host + ':' + b.out_port + '</span>';
        container.appendChild(div);
      });
    });
  }

  /* ═══════════════════════════════════════════
     MULTI-VIEW PANEL STATE MANAGEMENT
     ═══════════════════════════════════════════ */

  var _activeViews = {}; // { feed: true, serial: true, notifications: true, reference: true }
  var _viewOrder = ["feed", "serial", "notifications", "reference"];
  var _viewElements = {
    feed:          "viewFeed",
    serial:        "viewSerial",
    notifications: "viewNotifications",
    reference:     "viewReference"
  };
  var _viewToggleButtons = {
    feed:          "btnFeedToggle",
    serial:        "btnSerialToggle",
    reference:     "btnRefToggle",
    notifications: "btnNotifToggle"
  };
  var VSIZE_KEY = "gooey_panel_vsizes";
  var _viewFlexSizes = { feed: 1, serial: 1, notifications: 1, reference: 1 };
  try {
    var _savedVSizes = JSON.parse(localStorage.getItem(VSIZE_KEY) || "{}");
    Object.keys(_savedVSizes).forEach(function (k) {
      if (_viewFlexSizes.hasOwnProperty(k) && _savedVSizes[k] > 0) {
        _viewFlexSizes[k] = _savedVSizes[k];
      }
    });
  } catch (e) {}

  function updateVResizeHandles() {
    for (var i = 0; i < _viewOrder.length - 1; i++) {
      var a = _viewOrder[i], b = _viewOrder[i + 1];
      var handle = document.getElementById("vresize-" + a + "-" + b);
      if (!handle) continue;
      var bothActive = !!_activeViews[a] && !!_activeViews[b];
      handle.style.display = bothActive ? "block" : "none";
    }
  }

  function updatePanelLayout() {
    var panel = $("#panelRight");
    var activeCount = 0;
    _viewOrder.forEach(function (k) { if (_activeViews[k]) activeCount++; });

    if (activeCount === 0) {
      panel.classList.add("panel-hidden");
    } else {
      panel.classList.remove("panel-hidden");
    }

    /* When a newly-activated view has no prior size, give it the average of active peers */
    _viewOrder.forEach(function (k) {
      if (_activeViews[k] && _viewFlexSizes[k] === 1) {
        var sum = 0, cnt = 0;
        _viewOrder.forEach(function (j) { if (_activeViews[j] && j !== k) { sum += _viewFlexSizes[j]; cnt++; } });
        if (cnt > 0) _viewFlexSizes[k] = sum / cnt;
      }
    });

    _viewOrder.forEach(function (k) {
      var el = $("#" + _viewElements[k]);
      if (!el) return;
      if (_activeViews[k]) {
        el.style.display = "flex";
        el.style.flex = _viewFlexSizes[k] + " 1 0";
        el.style.minHeight = "80px";
        el.style.height = "";
      } else {
        el.style.display = "none";
        el.style.flex = "";
        el.style.minHeight = "";
      }
    });

    updateVResizeHandles();

    /* Update toggle button active states */
    Object.keys(_viewToggleButtons).forEach(function (k) {
      var btn = $("#" + _viewToggleButtons[k]);
      if (btn) {
        if (_activeViews[k]) btn.classList.add("panel-active");
        else btn.classList.remove("panel-active");
      }
    });
  }

  function toggleView(name) {
    if (_activeViews[name]) {
      delete _activeViews[name];
    } else {
      _activeViews[name] = true;
      /* Auto-refresh serial ports when showing serial */
      if (name === "serial") {
        socket.emit("serial_list_ports");
      }
    }
    updatePanelLayout();
  }

  /* ── Vertical panel resize ── */
  (function () {
    var MIN_H = 80;
    document.querySelectorAll(".panel-vresize-handle").forEach(function (handle) {
      handle.addEventListener("mousedown", function (e) {
        e.preventDefault();
        var above = handle.dataset.above;
        var below = handle.dataset.below;
        var elA = $("#" + _viewElements[above]);
        var elB = $("#" + _viewElements[below]);
        if (!elA || !elB) return;
        var startY = e.clientY;
        var startHA = elA.offsetHeight;
        var startHB = elB.offsetHeight;
        var totalH = startHA + startHB;
        var totalFlex = _viewFlexSizes[above] + _viewFlexSizes[below];

        function onMove(ev) {
          var delta = ev.clientY - startY;
          var newHA = Math.max(MIN_H, Math.min(totalH - MIN_H, startHA + delta));
          var frac = newHA / totalH;
          _viewFlexSizes[above] = frac * totalFlex;
          _viewFlexSizes[below] = (1 - frac) * totalFlex;
          elA.style.flex = _viewFlexSizes[above] + " 1 0";
          elB.style.flex = _viewFlexSizes[below] + " 1 0";
        }

        function onUp() {
          document.removeEventListener("mousemove", onMove);
          document.removeEventListener("mouseup", onUp);
          try { localStorage.setItem(VSIZE_KEY, JSON.stringify(_viewFlexSizes)); } catch (e2) {}
        }

        document.addEventListener("mousemove", onMove);
        document.addEventListener("mouseup", onUp);
      });
    });
  }());

  /* Feed toggle button */
  $("#btnFeedToggle").addEventListener("click", function () { toggleView("feed"); });

  /* Serial toggle button */
  $("#btnSerialToggle").addEventListener("click", function () { toggleView("serial"); });

  /* Reference toggle button */
  $("#btnRefToggle").addEventListener("click", function () { toggleView("reference"); });

  /* Notification history toggle — bell button */
  $("#btnNotifToggle").addEventListener("click", function () {
    toggleView("notifications");
  });

  /* Collapsible reference section blocks */
  (function () {
    var refContent = $("#viewReference");
    if (!refContent) return;
    refContent.addEventListener("click", function (e) {
      var title = e.target.closest(".ref-section-title");
      if (!title) return;
      var block = title.closest(".ref-section-block");
      if (block) block.classList.toggle("collapsed");
    });
  }());

  /* Click on latest notif text also toggles notifications panel */
  var notifLatestEl = $("#notifLatest");
  if (notifLatestEl) {
    notifLatestEl.addEventListener("click", function () {
      toggleView("notifications");
    });
  }

  /* Clear button inside notifications panel */
  var btnNotifClearPanel = $("#btnNotifClear");
  if (btnNotifClearPanel) {
    btnNotifClearPanel.addEventListener("click", function () {
      _toastHistory = [];
      renderNotifHistory();
      var latest = $("#notifLatest");
      if (latest) latest.textContent = "No notifications";
    });
  }

  /* ═══════════════════════════════════════════
     HORIZONTAL PANEL RESIZE
     ═══════════════════════════════════════════ */
  (function () {
    var handle = $("#panelResizeHandle");
    var panel  = $("#panelRight");
    if (!handle || !panel) return;

    var STORAGE_KEY = "gooey_panel_width";
    var MIN_PX = 200;

    /* Restore persisted width */
    try {
      var saved = localStorage.getItem(STORAGE_KEY);
      if (saved) panel.style.width = saved + "px";
    } catch (e) {}

    handle.addEventListener("mousedown", function (e) {
      e.preventDefault();
      var startX = e.clientX;
      var startW = panel.offsetWidth;

      function onMove(ev) {
        var delta = startX - ev.clientX;
        var newW = Math.max(MIN_PX, startW + delta);
        var maxW = window.innerWidth * 0.7;
        newW = Math.min(newW, maxW);
        panel.style.width = newW + "px";
      }

      function onUp() {
        document.removeEventListener("mousemove", onMove);
        document.removeEventListener("mouseup", onUp);
        try { localStorage.setItem(STORAGE_KEY, panel.offsetWidth); } catch (e) {}
      }

      document.addEventListener("mousemove", onMove);
      document.addEventListener("mouseup", onUp);
    });
  }());

  /* ═══════════════════════════════════════════
     LISTEN  (auto-starts on page load; port is always active)
     ═══════════════════════════════════════════ */

  var isListening = false;
  var _listenPort = 9000;

  function startListen(port) {
    port = parseInt(port, 10) || 9000;
    var doStart = function () {
      api("recv/start", { port: port, id: "reply-listener" }).then(function (res) {
        if (res.status === "ok") {
          isListening = true;
          _listenPort = port;
          $("#listenPortDisplay").textContent = port;
          $("#replyPort").value = port;
          $("#listenDot").classList.add("on");
        } else {
          toast("Listen failed: " + (res.message || ""), "error");
        }
      });
    };
    if (isListening) {
      /* Stop previous listener first, then restart on new port */
      api("recv/stop", { id: "reply-listener" }).then(function () {
        isListening = false;
        doStart();
      });
    } else {
      doStart();
    }
  }

  /* Apply port button: restart listener on the entered port */
  $("#btnApplyPort").addEventListener("click", function () {
    var port = parseInt($("#replyPort").value, 10) || 9000;
    if (port === _listenPort && isListening) {
      toast("Already listening on port " + port, "info");
      return;
    }
    startListen(port);
    toast("Listening on port " + port, "success");
  });

  /* Auto-start listening on page load, show feed */
  startListen(9000);
  _activeViews.feed = true;
  updatePanelLayout();

  /* ── Demo mode init ── */
  if (window.GOOEY_DEMO) {
    // Force light theme regardless of localStorage or system preference
    try { localStorage.removeItem("gooey-theme"); } catch (e) {}
    document.documentElement.classList.remove("dark");

    // Open Notifications panel alongside Feed
    _activeViews.notifications = true;
    updatePanelLayout();

    // Seed getting-started steps as notifications (added in reverse so step 1 shows at top)
    showToast("Step 4: Start a scene \u2014 group messages together and click Start to begin streaming.", "info");
    showToast("Step 3: Create messages \u2014 map sensor streams to OSC addresses in the Messages tab.", "info");
    showToast("Step 2: Query it \u2014 select your device tab, then click \u27f3 Query to load its config.", "info");
    showToast("Step 1: Add a device \u2014 click + in the header to enter your device\u2019s IP and port.", "info");
    showToast("Welcome to the annieData demo \u2014 OSC sending is disabled. Explore freely!", "info");
  }

  /* ═══════════════════════════════════════════
     FEED CONTROLS
     ═══════════════════════════════════════════ */

  $("#btnFeedClear").addEventListener("click", function () {
    $("#feedLog").innerHTML = "";
    msgCount = 0;
    rateCounter = 0;
    api("log/clear", {});
  });

  /* ═══════════════════════════════════════════
     ALL-DEVICE BLACKOUT / RESTORE  (header buttons)
     ═══════════════════════════════════════════ */

  $("#btnBlackoutAll").addEventListener("click", function () {
    var ids = Object.keys(devices);
    if (!ids.length) { toast("No devices configured", "error"); return; }
    ids.forEach(function (id) {
      var d = devices[id];
      sendToDevice(id, "/annieData/" + d.name + "/blackout", null);
    });
    toast("Blackout sent to " + ids.length + " device(s)", "success");
  });

  $("#btnRestoreAll").addEventListener("click", function () {
    var ids = Object.keys(devices);
    if (!ids.length) { toast("No devices configured", "error"); return; }
    ids.forEach(function (id) {
      var d = devices[id];
      sendToDevice(id, "/annieData/" + d.name + "/restore", null);
    });
    toast("Restore sent to " + ids.length + " device(s)", "success");
  });

  /* ═══════════════════════════════════════════
     SAVE / LOAD DEVICES
     ═══════════════════════════════════════════ */

  $("#btnSaveDevices").addEventListener("click", function () {
    var data = Object.keys(devices).map(function (id) {
      var d = devices[id];
      return { host: d.host, port: d.port, name: d.name };
    });
    var blob = new Blob([JSON.stringify(data, null, 2)], { type: "application/json" });
    var a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = "gooey-devices.json";
    a.click();
  });

  $("#deviceFileInput").addEventListener("change", function (e) {
    var file = e.target.files[0];
    if (!file) return;
    var reader = new FileReader();
    reader.onload = function (ev) {
      try {
        var list = JSON.parse(ev.target.result);
        list.forEach(function (d) {
          if (d.host && d.port && d.name) addDevice(d.host, parseInt(d.port, 10), d.name);
        });
        renderMsgTable();
        renderSceneTable();
        renderOriTable();
        toast("Loaded " + list.length + " device(s)", "success");
      } catch (_) { toast("Invalid device file", "error"); }
    };
    reader.readAsText(file);
    e.target.value = "";
  });

  /* ═══════════════════════════════════════════
     QUERY DEVICE SELECT  (header dropdown)
     ═══════════════════════════════════════════ */

  /** No-op — query target is now controlled by #queryAllDevices checkbox. */
  function refreshQueryDeviceSelect() {}

  /* ═══════════════════════════════════════════
     QUERY BUTTON  (header — always verbose)
     ═══════════════════════════════════════════ */

  $("#btnQueryDevice").addEventListener("click", function () {
    var allChecked = $("#queryAllDevices") ? $("#queryAllDevices").checked : true;
    if (allChecked) {
      /* Query all configured devices */
      var ids = Object.keys(devices);
      if (!ids.length) { toast("No devices configured", "error"); return; }
      ids.forEach(function (id) {
        var d = devices[id];
        sendToDevice(id, "/annieData/" + d.name + "/list/all", "verbose");
      });
      toast("Querying " + ids.length + " device(s)…", "info");
    } else {
      /* Query active device only */
      if (!activeDeviceId || !devices[activeDeviceId]) { toast("No active device", "error"); return; }
      var d = devices[activeDeviceId];
      sendToDevice(activeDeviceId, "/annieData/" + d.name + "/list/all", "verbose").then(function (res) {
        if (res.status === "ok") toast("Querying " + d.name + "…", "info");
      });
    }
    showPanel("feed");
  });

  /* Auto-query removed — use the Query button manually. */

  /* ═══════════════════════════════════════════
     REFERENCE — populate from presets
     ═══════════════════════════════════════════ */

  fetch("/api/presets/theater-gwd")
    .then(function (r) { return r.json(); })
    .then(function (data) {
      var presets = data.presets;
      if (!presets) return;

      /* Shared search input */
      var refSearch = $("#refSearch");

      /* Command list */
      var cmdContainer = $("#cmdList");
      var cmds = presets.commands || {};

      function renderCmds(filter) {
        cmdContainer.innerHTML = "";
        Object.keys(cmds).forEach(function (key) {
          var c = cmds[key];
          if (filter && key.toLowerCase().indexOf(filter) === -1 && (c.description || "").toLowerCase().indexOf(filter) === -1) return;
          var div = document.createElement("div");
          div.className = "ref-item";
          div.innerHTML = '<span class="ref-term">' + esc(key) + '</span>' +
            '<span class="ref-addr">"' + esc(c.address) + '"</span>' +
            '<span class="ref-def">' + esc(c.description) + '</span>' +
            (c.payload ? '<span class="ref-payload">payload: ' + esc(c.payload) + '</span>' : '');
          cmdContainer.appendChild(div);
        });
      }
      renderCmds("");

      /* Keywords — grouped by category */
      var kwContainer = $("#keywordList");
      var kws = presets.keywords || {};

      var kwCategories = [
        { title: "Sensors \u2014 Acceleration", keys: ["accelX","accelY","accelZ","accelLength","gaccelX","gaccelY","gaccelZ","gaccelLength","limbFwd","limbLat","limbVert","twitch"] },
        { title: "Sensors \u2014 Orientation", keys: ["roll","pitch","yaw","twist","azi","tilt","quatI","quatJ","quatK","quatR"] },
        { title: "Sensors \u2014 Gyroscope", keys: ["gyroX","gyroY","gyroZ","gyroLength"] },
        { title: "Sensors \u2014 Barometer", keys: ["baro"] },
        { title: "Device Commands", keys: ["blackout","restore","save","load","nvs/clear","list","status/config","status/level"] },
        { title: "Message Commands", keys: ["msg","enable","disable","delete","info","save/msg","addMsg","removeMsg","clone","rename","move","direct"] },
        { title: "Scene Commands", keys: ["scene","start","stop","period","override","adrMode","setAll","solo","unsolo","enableAll","save/scene"] },
        { title: "Address Modes", keys: ["fallback","prepend","append"] },
        { title: "Other", keys: ["config string"] }
      ];

      function renderKWs(filter) {
        kwContainer.innerHTML = "";
        var allCatKeys = [];
        kwCategories.forEach(function (cat) {
          var filtered = cat.keys.filter(function (key) {
            if (!kws[key]) return false;
            if (filter && key.toLowerCase().indexOf(filter) === -1 && kws[key].toLowerCase().indexOf(filter) === -1) return false;
            return true;
          });
          if (filtered.length === 0) return;
          var heading = document.createElement("div");
          heading.className = "ref-category-title";
          heading.textContent = cat.title;
          kwContainer.appendChild(heading);
          filtered.forEach(function (key) {
            allCatKeys.push(key);
            var div = document.createElement("div");
            div.className = "ref-item";
            div.innerHTML = '<span class="ref-term">' + esc(key) + '</span> <span class="ref-def">' + esc(kws[key]) + '</span>';
            kwContainer.appendChild(div);
          });
        });
        /* Uncategorized keywords */
        kwCategories.forEach(function (cat) { cat.keys.forEach(function (k) { if (allCatKeys.indexOf(k) === -1) allCatKeys.push(k); }); });
        Object.keys(kws).sort().forEach(function (key) {
          if (allCatKeys.indexOf(key) !== -1) return;
          if (filter && key.toLowerCase().indexOf(filter) === -1 && kws[key].toLowerCase().indexOf(filter) === -1) return;
          var div = document.createElement("div");
          div.className = "ref-item";
          div.innerHTML = '<span class="ref-term">' + esc(key) + '</span> <span class="ref-def">' + esc(kws[key]) + '</span>';
          kwContainer.appendChild(div);
        });
      }
      renderKWs("");

      /* Single input drives all section renders */
      if (refSearch) refSearch.addEventListener("input", function () {
        var f = refSearch.value.trim().toLowerCase();
        renderCmds(f);
        renderKWs(f);
      });

      /* Config keys */
      var ckContainer = $("#configKeyList");
      var cks = presets.config_keys || {};
      Object.keys(cks).forEach(function (key) {
        var div = document.createElement("div");
        div.className = "ref-item";
        div.innerHTML = '<span class="ref-term">' + esc(key) + '</span> <span class="ref-def">' + esc(cks[key]) + '</span>';
        ckContainer.appendChild(div);
      });

      /* Address modes */
      var amContainer = $("#adrModeList");
      var ams = presets.address_modes || {};
      Object.keys(ams).forEach(function (key) {
        var div = document.createElement("div");
        div.className = "ref-item";
        div.innerHTML = '<span class="ref-term">' + esc(key) + '</span> <span class="ref-def">' + esc(ams[key]) + '</span>';
        amContainer.appendChild(div);
      });
    });

  /* ═══════════════════════════════════════════
     DRAGGABLE CARD LAYOUT  (localStorage)
     ═══════════════════════════════════════════ */

  function initCollapsibleCards() {
    $$(".section .card").forEach(function (card) {
      if (card.id === "oriDetailsCard") return;
      var headers = card.querySelectorAll("h2, .card-title-row, .tbl-toolbar");
      headers.forEach(function (hdr) {
        hdr.addEventListener("click", function (e) {
          if (e.target.closest("button, input, select, textarea, a, .col-picker-wrap")) return;
          card.classList.toggle("card-collapsed");
        });
      });
    });
  }

  initCollapsibleCards();

  /* ═══════════════════════════════════════════
     SCENE PREVIEW
     ═══════════════════════════════════════════ */

  function updateScenePreview() {
    var name = ($("#sceneName") ? $("#sceneName").value.trim() : "");
    var adrEl = $("#scenePreviewAdr");
    var cfgEl = $("#scenePreviewCfg");
    if (adrEl) adrEl.textContent = name ? "scene: " + name : "(no scene name)";
    var parts = [];
    var ip = ($("#sceneIP").value || "").trim(); if (ip) parts.push(previewPair("ip", ip));
    var port = ($("#scenePort").value || "").trim(); if (port) parts.push(previewPair("port", port));
    var sceneAdr = ($("#sceneAdr").value || "").trim(); if (sceneAdr) parts.push(previewPair("adr", sceneAdr));
    var low = ($("#sceneLow").value || "").trim(); if (low) parts.push(previewPair("low", low));
    var high = ($("#sceneHigh").value || "").trim(); if (high) parts.push(previewPair("high", high));
    var period = ($("#scenePeriod").value || "").trim(); if (period) parts.push(previewPair("period", period));
    var mode = ($("#sceneAdrMode").value || "").trim(); if (mode) parts.push("adrMode:" + mode);
    var ovParts = [];
    if ($("#ovIP").checked) ovParts.push("ip");
    if ($("#ovPort").checked) ovParts.push("port");
    if ($("#ovAdr").checked) ovParts.push("adr");
    if ($("#ovLow").checked) ovParts.push("low");
    if ($("#ovHigh").checked) ovParts.push("high");
    if (ovParts.length > 0) parts.push("override:" + ovParts.join(", "));
    if (sceneGatePicker) {
      var gc = sceneGatePicker.getConfig();
      if (gc) {
        parts.push(previewPair("gate_src", gc.gate_src));
        parts.push(previewPair("gate_mode", gc.gate_mode));
        if (gc.gate_lo) parts.push(previewPair("gate_lo", gc.gate_lo));
        if (gc.gate_hi) parts.push(previewPair("gate_hi", gc.gate_hi));
      }
    }
    if (cfgEl) cfgEl.textContent = parts.join(", ");
  }

  ["sceneName", "sceneIP", "scenePort", "sceneAdr", "sceneLow", "sceneHigh", "scenePeriod", "sceneAdrMode"].forEach(function (id) {
    var el = $("#" + id);
    if (el) el.addEventListener("input", updateScenePreview);
    if (el) el.addEventListener("change", updateScenePreview);
  });
  ["ovIP", "ovPort", "ovAdr", "ovLow", "ovHigh"].forEach(function (id) {
    var el = $("#" + id);
    if (el) el.addEventListener("change", updateScenePreview);
  });
  ["sceneGateSource", "sceneGateOri", "sceneGateMode", "sceneGateLo", "sceneGateHi"].forEach(function (id) {
    var el = $("#" + id);
    if (el) el.addEventListener("input", updateScenePreview);
    if (el) el.addEventListener("change", updateScenePreview);
  });
  updateScenePreview();

  /* ── Hover tooltips: position fixed to escape overflow clipping ── */
  document.querySelectorAll(".tooltip-wrap").forEach(function (wrap) {
    wrap.addEventListener("mouseenter", function () {
      var body = wrap.querySelector(".tooltip-body");
      if (!body) return;
      var r = wrap.getBoundingClientRect();
      var h  = body.offsetHeight || 150;
      var vh = window.innerHeight || document.documentElement.clientHeight || 800;
      body.style.left = Math.max(8, r.right - 280) + "px";
      if (r.bottom + 6 + h > vh) {
        body.style.top  = "";
        body.style.bottom = (vh - r.top + 6) + "px";
      } else {
        body.style.bottom = "";
        body.style.top  = (r.bottom + 6) + "px";
      }
    });
  });

  /* ═══════════════════════════════════════════
     VERBOSE MODE — confirm before send (per-device)
     ═══════════════════════════════════════════ */

  function isVerboseMode() {
    var dev = getActiveDev();
    return dev && dev.verbose;
  }

  var _origSendCmd = sendCmd;
  sendCmd = function (address, payload) {
    if (isVerboseMode()) {
      return new Promise(function (resolve) {
        var msgText = "Address:\n" + address + "\n\nPayload:\n" + (payload || "(none)");
        showConfirm("Verbose — Send OSC?", msgText, function () {
          resolve(_origSendCmd(address, payload));
        }, "Send", false);
        document.getElementById("confirmCancel").addEventListener("click", function () {
          resolve({ status: "cancelled" });
        }, { once: true });
      });
    }
    return _origSendCmd(address, payload);
  };

  var _origApi = api;
  api = function (endpoint, data, method) {
    if (endpoint === "send" && data && isVerboseMode()) {
      return new Promise(function (resolve) {
        var msgText = "Address:\n" + (data.address || "") + "\n\nPayload:\n" + JSON.stringify(data.args || "") + "\n\nHost: " + (data.host || "") + ":" + (data.port || "");
        showConfirm("Verbose — Send OSC?", msgText, function () {
          resolve(_origApi(endpoint, data, method));
        }, "Send", false);
        document.getElementById("confirmCancel").addEventListener("click", function () {
          resolve({ status: "cancelled" });
        }, { once: true });
      });
    }
    return _origApi(endpoint, data, method);
  };

  /* ═══════════════════════════════════════════
     ORI CONTROLS
     ═══════════════════════════════════════════ */

  /* Ori button handlers */
  var oriButtons = {
    btnOriSave: function () {
      var name = ($("#oriName").value || "").trim();
      if (!name) { toast("Ori name required", "error"); return; }
      sendCmd(addr("/annieData/{device}/ori/save"), name);
    },
    btnOriSaveAuto: function () {
      sendCmd(addr("/annieData/{device}/ori/save"), null);
    },
    btnOriList: function () {
      sendCmd(addr("/annieData/{device}/ori/list"), null);
    },
    btnOriActive: function () {
      sendCmd(addr("/annieData/{device}/ori/active"), null);
    },
    btnOriDelete: function () {
      var name = ($("#oriName").value || "").trim();
      if (!name) { toast("Ori name required", "error"); return; }
      sendCmd(addr("/annieData/{device}/ori/delete"), name).then(function (res) {
        if (res && res.status === "ok") {
          var dev = getActiveDev();
          if (dev) { delete dev.oris[name]; renderOriTable(); refreshAllDropdowns(); }
        }
      });
    },
    btnOriClear2: function () {
      var name = ($("#oriName").value || "").trim();
      if (!name) { toast("Ori name required", "error"); return; }
      sendCmd(addr("/annieData/{device}/ori/reset/" + name), null).then(function (res) {
        if (res && res.status === "ok") {
          toast("Samples cleared: " + name, "success");
          var dev = getActiveDev();
          if (dev && dev.oris[name]) {
            dev.oris[name].samples = 0;
            dev.oris[name].useAxis = false;
            renderOriTable();
          }
        }
      });
    },
    btnOriClear: function () {
      showConfirm("Clear All Orientations", "Clear all saved orientations from the device?", function () {
        sendCmd(addr("/annieData/{device}/ori/clear"), null).then(function (res) {
          if (res.status === "ok") {
            var dev = getActiveDev();
            if (dev) { dev.oris = {}; renderOriTable(); refreshAllDropdowns(); }
            toast("All oris cleared", "success");
          }
        });
      }, "Clear All", true);
    },
    btnOriThreshold: function () {
      var val = ($("#oriThreshold").value || "").trim();
      if (!val) { toast("Threshold value required", "error"); return; }
      sendCmd(addr("/annieData/{device}/ori/threshold"), '"' + val + '"');
    },
    btnOriTolerance: function () {
      var val = ($("#oriTolerance").value || "").trim();
      if (!val) { toast("Tolerance value required", "error"); return; }
      sendCmd(addr("/annieData/{device}/ori/tolerance"), '"' + val + '"');
    },
    btnOriGeneral: function () {
      var name = ($("#oriGeneralName") ? $("#oriGeneralName").value : "").trim();
      if (!name) { toast("Ori name required", "error"); return; }
      sendCmd(addr("/annieData/{device}/ori/general/" + name), null).then(function (res) {
        if (res && res.status === "ok") toast("General ori: " + name, "success");
      });
    },
    btnOriRemoveGeneral: function () {
      sendCmd(addr("/annieData/{device}/ori/general/none"), null).then(function (res) {
        if (res && res.status === "ok") {
          toast("General ori cleared", "success");
          var el = $("#oriGeneralName");
          if (el) el.value = "";
        }
      });
    },
    btnOriStrict: function () {
      sendCmd(addr("/annieData/{device}/ori/strict"), "on");
    },
    btnOriNearest: function () {
      var el = $("#oriStrict");
      if (el) el.checked = false;
      sendCmd(addr("/annieData/{device}/ori/strict"), "off");
    },
    btnOriWatch: function () {
      var btn = $("#btnOriWatch");
      var watching = btn && btn.classList.contains("watching");
      var next = watching ? "off" : "on";
      sendCmd(addr("/annieData/{device}/ori/watch"), next).then(function (res) {
        if (res && res.status === "ok") {
          if (btn) {
            if (next === "on") {
              btn.classList.add("watching");
              btn.textContent = "Unwatch";
            } else {
              btn.classList.remove("watching");
              btn.textContent = "Watch";
            }
          }
        }
      });
    },
  };

  Object.keys(oriButtons).forEach(function (id) {
    var el = $("#" + id);
    if (el) {
      el.addEventListener("click", function () {
        oriButtons[id]();
      });
    }
  });


  /* ═══════════════════════════════════════════
     SHOW MANAGEMENT
     ═══════════════════════════════════════════ */

  function renderShowDeviceTable(names) {
    var tbody = $("#showDeviceTableBody");
    if (!tbody) return;
    if (!names || names.length === 0) {
      tbody.innerHTML = '<tr><td colspan="2"><div class="empty-state"><div class="empty-icon">◑</div><div class="empty-text">No shows saved on device.</div></div></td></tr>';
      return;
    }
    tbody.innerHTML = "";
    names.forEach(function (name) {
      name = name.trim();
      if (!name) return;
      var tr = document.createElement("tr");
      tr.innerHTML =
        '<td>' + esc(name) + '</td>' +
        '<td class="cell-actions">' +
          '<button class="tbl-btn tbl-btn-primary" data-act="load" title="Load this show (requires confirm)">Load</button>' +
          '<button class="tbl-btn tbl-btn-danger" data-act="delete" title="Delete show from device">×</button>' +
        '</td>';
      tr.querySelectorAll(".tbl-btn").forEach(function (btn) {
        btn.addEventListener("click", function () {
          if (btn.dataset.act === "load") {
            if (!getActiveDev()) { toast("Select a device first", "error"); return; }
            showConfirm(
              "Load Show '" + name + "'",
              "Loading replaces all current messages, scenes, and oris on the device. Continue?",
              function () {
                /* Two-step: first send load, then confirm */
                sendCmd(addr("/annieData/{device}/show/load/" + name), null).then(function () {
                  sendCmd(addr("/annieData/{device}/show/load/confirm"), null).then(function (res) {
                    if (res && res.status === "ok") {
                      toast("Loaded show: " + name, "success");
                      /* Re-query everything */
                      sendCmd(addr("/annieData/{device}/list/all"), null);
                      sendCmd(addr("/annieData/{device}/ori/list"), null);
                    }
                  });
                });
              },
              "Load", true
            );
          } else if (btn.dataset.act === "delete") {
            showConfirm("Delete Show '" + name + "'", "Delete '" + name + "' from device NVS?", function () {
              sendCmd(addr("/annieData/{device}/show/delete/" + name), null).then(function (res) {
                if (res && res.status === "ok") {
                  toast("Deleted: " + name, "success");
                  sendCmd(addr("/annieData/{device}/show/list"), null);
                }
              });
            }, "Delete", true);
          }
        });
      });
      tbody.appendChild(tr);
    });
  }

  function renderShowLibraryTable(shows) {
    var tbody = $("#showLibraryTableBody");
    if (!tbody) return;
    if (!shows || shows.length === 0) {
      tbody.innerHTML = '<tr><td colspan="3"><div class="empty-state"><div class="empty-icon">◑</div><div class="empty-text">No shows in library.</div></div></td></tr>';
      return;
    }
    tbody.innerHTML = "";
    shows.forEach(function (s) {
      var tr = document.createElement("tr");
      var savedDate = s.saved ? s.saved.replace("T", " ").substr(0, 16) : "—";
      tr.innerHTML =
        '<td>' + esc(s.name) + '</td>' +
        '<td class="cell-mono" style="font-size:0.8em">' + esc(savedDate) + '</td>' +
        '<td class="cell-actions">' +
          '<button class="tbl-btn tbl-btn-primary" data-act="load" title="Push show to device">Load to Device</button>' +
          '<button class="tbl-btn tbl-btn-danger" data-act="delete" title="Delete from library">×</button>' +
        '</td>';
      tr.querySelectorAll(".tbl-btn").forEach(function (btn) {
        btn.addEventListener("click", function () {
          if (btn.dataset.act === "load") {
            if (!getActiveDev()) { toast("Select a device first", "error"); return; }
            showConfirm(
              "Load Library Show '" + s.name + "'",
              "Push all messages, scenes, and oris to device and save as show '" + s.name + "'?",
              function () {
                fetch("/api/shows/" + encodeURIComponent(s.name)).then(function (r) { return r.json(); })
                  .then(function (showData) {
                    _pushLibraryShowToDevice(showData);
                  })
                  .catch(function (e) { toast("Failed to read library show: " + e, "error"); });
              },
              "Load", true
            );
          } else if (btn.dataset.act === "delete") {
            showConfirm("Delete '" + s.name + "' from Library", "This cannot be undone.", function () {
              fetch("/api/shows/" + encodeURIComponent(s.name), { method: "DELETE" })
                .then(function () { _refreshShowLibrary(); toast("Deleted: " + s.name, "info"); })
                .catch(function (e) { toast("Delete failed: " + e, "error"); });
            }, "Delete", true);
          }
        });
      });
      tbody.appendChild(tr);
    });
  }

  function _refreshShowLibrary() {
    fetch("/api/shows").then(function (r) { return r.json(); })
      .then(function (data) { renderShowLibraryTable(data || []); })
      .catch(function (e) { console.warn("Library fetch failed:", e); });
  }

  function _pushLibraryShowToDevice(showData) {
    if (!showData) return;
    var name = showData.name || "imported";
    /* Push messages */
    (showData.messages || []).forEach(function (m) {
      var cfg = "value:" + m.value + ",ip:" + m.ip + ",port:" + m.port + ",adr:" + m.adr;
      if (m.low  !== undefined) cfg += ",low:" + m.low;
      if (m.high !== undefined) cfg += ",high:" + m.high;
      sendCmd(addr("/annieData/{device}/msg/" + m.name), '"' + cfg + '"');
    });
    /* Push scenes */
    (showData.scenes || []).forEach(function (p) {
      var cfg = "ip:" + p.ip + ",port:" + p.port + ",period:" + p.period;
      sendCmd(addr("/annieData/{device}/scene/" + p.name), '"' + cfg + '"');
      (p.msgs || []).forEach(function (mName) {
        sendCmd(addr("/annieData/{device}/scene/" + p.name + "/addMsg"), '"' + mName + '"');
      });
    });
    /* Push oris */
    (showData.oris || []).forEach(function (o) {
      var rgb = (o.color || [255,255,255]);
      var colorStr = '"' + rgb[0] + "," + rgb[1] + "," + rgb[2] + '"';
      sendCmd(addr("/annieData/{device}/ori/register/" + o.name), colorStr);
      if (o.quaternions && o.quaternions.length > 0) {
        o.quaternions.forEach(function (q) {
          sendCmd(addr("/annieData/{device}/ori/save/" + o.name), null);
        });
      }
    });
    /* Wait for device to finish processing all commands, then save as show */
    sendFlush().then(function () {
      sendCmd(addr("/annieData/{device}/show/save/" + name), null).then(function () {
        toast("Pushed library show '" + name + "' to device", "success");
        sendCmd(addr("/annieData/{device}/list/all"), null);
        sendCmd(addr("/annieData/{device}/ori/list"), null);
        sendCmd(addr("/annieData/{device}/show/list"), null);
      });
    });
  }

  /* Wire up show buttons */
  (function () {
    var btnSaveDevice  = $("#btnShowSaveDevice");
    var btnSaveLibrary = $("#btnShowSaveLibrary");
    var btnListDevice  = $("#btnShowListDevice");
    var btnListLibrary = $("#btnShowListLibrary");

    if (btnSaveDevice) btnSaveDevice.addEventListener("click", function () {
      var name = ($("#showSaveName").value || "").trim();
      if (!name) { toast("Show name required", "error"); return; }
      if (!getActiveDev()) { toast("Select a device first", "error"); return; }
      sendCmd(addr("/annieData/{device}/show/save/" + name), null).then(function (res) {
        if (res && res.status === "ok") {
          toast("Saved show on device: " + name, "success");
          sendCmd(addr("/annieData/{device}/show/list"), null);
        }
      });
    });

    if (btnSaveLibrary) btnSaveLibrary.addEventListener("click", function () {
      var name = ($("#showSaveName").value || "").trim();
      if (!name) { toast("Show name required", "error"); return; }
      var dev = getActiveDev();
      if (!dev) { toast("Select a device first", "error"); return; }
      /* Build show JSON from current Gooey registry */
      var msgs = Object.entries(dev.messages || {}).map(function (kv) { return Object.assign({ name: kv[0] }, kv[1]); });
      var scenes = Object.entries(dev.scenes || {}).map(function (kv) { return Object.assign({ name: kv[0] }, kv[1]); });
      var oris = Object.entries(dev.oris || {}).map(function (kv) {
        return { name: kv[0], color: kv[1].color, samples: kv[1].samples, use_axis: kv[1].useAxis };
      });
      var payload = {
        name: name,
        saved: new Date().toISOString(),
        device: dev.name,
        messages: msgs,
        scenes: scenes,
        oris: oris
      };
      fetch("/api/shows/" + encodeURIComponent(name), {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
      }).then(function (r) { return r.json(); })
        .then(function () {
          toast("Saved to library: " + name, "success");
          _refreshShowLibrary();
        })
        .catch(function (e) { toast("Library save failed: " + e, "error"); });
    });

    if (btnListDevice) btnListDevice.addEventListener("click", function () {
      if (!getActiveDev()) { toast("Select a device first", "error"); return; }
      sendCmd(addr("/annieData/{device}/show/list"), null);
    });

    if (btnListLibrary) btnListLibrary.addEventListener("click", function () {
      _refreshShowLibrary();
    });
  }());

  /* ═══════════════════════════════════════════
     INLINE FIELD VALIDATION
     ═══════════════════════════════════════════ */

  /* ── Assume IP: expand a bare last-octet number to a full IP ── */
  var _assumeIpPrefix = "";
  function expandIp(val) {
    if (!_assumeIpPrefix) return val;
    var trimmed = val.trim();
    /* Count dots in prefix to know how many octets the user must supply */
    var prefixDots = (_assumeIpPrefix.match(/\./g) || []).length;
    var needed = 4 - prefixDots;
    if (needed < 1 || needed > 3) return val;
    /* Build pattern for `needed` dot-separated octet groups */
    var octetGroup = "\\d{1,3}";
    var groups = [];
    for (var i = 0; i < needed; i++) groups.push(octetGroup);
    var re = new RegExp("^" + groups.join("\\.") + "$");
    if (re.test(trimmed)) {
      var octets = trimmed.split(".");
      var valid = octets.every(function (o) { var n = parseInt(o, 10); return n >= 0 && n <= 255; });
      if (valid) return _assumeIpPrefix + trimmed;
    }
    return val;
  }

  function validateField(input, isValid, msg) {
    var hint;
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
    var el = $("#" + fieldId);
    if (!el) return;
    el.addEventListener("blur", function () {
      var v = el.value.trim();
      if (!v) return; /* allow empty */
      /* Expand shorthand: bare last-octet number → prefix + number */
      var expanded = expandIp(v);
      if (expanded !== v) { el.value = expanded; }
    });
  });

  /* ═══════════════════════════════════════════
     COLUMN PICKER (message table)
     ═══════════════════════════════════════════ */

  var COL_PREF_KEY = "gooey_col_prefs";

  function loadColPrefs() {
    try {
      var raw = localStorage.getItem(COL_PREF_KEY);
      return raw ? JSON.parse(raw) : null;
    } catch (e) { return null; }
  }

  function saveColPrefs() {
    var prefs = {};
    $$('#colPickerMenu input[data-col]').forEach(function (cb) {
      prefs[cb.dataset.col] = cb.checked;
    });
    try { localStorage.setItem(COL_PREF_KEY, JSON.stringify(prefs)); } catch (e) {}
  }

  function applyColVisibility() {
    var prefs = loadColPrefs();
    if (!prefs) return;
    var hiddenCount = 0;
    Object.keys(prefs).forEach(function (col) {
      var visible = prefs[col];
      if (!visible) hiddenCount++;
      $$('[data-col="' + col + '"]').forEach(function (el) {
        el.style.display = visible ? "" : "none";
      });
    });
    /* Update caret indicator when columns are hidden */
    var btn = $("#btnColPicker");
    if (btn) {
      btn.classList.toggle("has-hidden", hiddenCount > 0);
      btn.textContent = hiddenCount > 0 ? "▾ " + hiddenCount : "▾";
      btn.title = hiddenCount > 0 ? hiddenCount + " column(s) hidden" : "Choose visible columns";
    }
  }

  /* Init column picker checkboxes from localStorage */
  var savedPrefs = loadColPrefs();
  if (savedPrefs) {
    $$('#colPickerMenu input[data-col]').forEach(function (cb) {
      if (savedPrefs[cb.dataset.col] !== undefined) {
        cb.checked = savedPrefs[cb.dataset.col];
      }
    });
  }
  applyColVisibility();

  /* Toggle column picker menu */
  var btnColPicker = $("#btnColPicker");
  var colPickerMenu = $("#colPickerMenu");
  if (btnColPicker && colPickerMenu) {
    btnColPicker.addEventListener("click", function (e) {
      e.stopPropagation();
      colPickerMenu.classList.toggle("hidden");
    });
    colPickerMenu.querySelectorAll("input[type='checkbox']").forEach(function (cb) {
      cb.addEventListener("change", function () {
        saveColPrefs();
        applyColVisibility();
      });
    });
    document.addEventListener("click", function (e) {
      if (!e.target.closest(".col-picker-wrap")) {
        colPickerMenu.classList.add("hidden");
      }
    });
  }


  /* ═══════════════════════════════════════════
     REFERENCE PANEL — open by default first visit
     ═══════════════════════════════════════════ */

  var REF_SEEN_KEY = "gooey_refPanelSeen";
  try {
    if (!localStorage.getItem(REF_SEEN_KEY)) {
      /* First visit: also show reference panel */
      _activeViews.reference = true;
      updatePanelLayout();
      localStorage.setItem(REF_SEEN_KEY, "1");
    }
  } catch (e) {}

  /* ═══════════════════════════════════════════
     SERIAL MONITOR
     ═══════════════════════════════════════════ */

  (function () {
    var terminal    = $("#serialTerminal");
    var portSelect  = $("#serialPortSelect");
    var baudSelect  = $("#serialBaudSelect");
    var btnConnect  = $("#btnSerialConnect");
    var btnClear    = $("#btnSerialClear");
    var btnRefresh  = $("#btnSerialRefreshPorts");
    var _connected  = false;

    if (!terminal) return;

    function refreshPorts() {
      socket.emit("serial_list_ports");
    }

    socket.on("serial_ports", function (data) {
      portSelect.innerHTML = "";
      if (!data.ports || data.ports.length === 0) {
        portSelect.innerHTML = '<option value="">No ports found</option>';
        return;
      }
      data.ports.forEach(function (p) {
        var opt = document.createElement("option");
        opt.value = p.device;
        opt.textContent = p.device + (p.description && p.description !== p.device ? " — " + p.description : "");
        portSelect.appendChild(opt);
      });
    });

    function appendLine(text, cls) {
      var placeholder = terminal.querySelector(".serial-placeholder");
      if (placeholder) placeholder.remove();
      var line = document.createElement("div");
      line.className = "serial-line" + (cls ? " " + cls : "");
      line.textContent = text;
      terminal.appendChild(line);
      // keep last 500 lines
      while (terminal.children.length > 500) terminal.removeChild(terminal.firstChild);
      terminal.scrollTop = terminal.scrollHeight;
    }

    socket.on("serial_data", function (data) {
      (data.data || "").split(/\r?\n/).forEach(function (line) {
        if (line) appendLine(line);
      });
    });

    socket.on("serial_connected", function (data) {
      _connected = true;
      btnConnect.textContent = "Disconnect";
      btnConnect.classList.add("connected");
      appendLine("── Connected: " + data.port + " @ " + data.baud + " baud ──");
    });

    socket.on("serial_disconnected", function () {
      _connected = false;
      btnConnect.textContent = "Connect";
      btnConnect.classList.remove("connected");
      appendLine("── Disconnected ──");
    });

    socket.on("serial_error", function (data) {
      appendLine("Error: " + (data.message || "unknown"), "stderr");
    });

    btnConnect.addEventListener("click", function () {
      if (_connected) {
        socket.emit("serial_disconnect_port");
      } else {
        var port = portSelect.value;
        if (!port) { toast("Select a serial port first", "warn"); return; }
        socket.emit("serial_connect", { port: port, baud: parseInt(baudSelect.value, 10) });
      }
    });

    btnRefresh.addEventListener("click", refreshPorts);

    btnClear.addEventListener("click", function () {
      terminal.innerHTML = "";
    });

    /* ── Send command ── */
    var sendInput = $("#serialSendInput");
    var btnSend   = $("#btnSerialSend");

    function doSend() {
      if (!_connected) { toast("Not connected to serial port", "warn"); return; }
      var val = sendInput.value;
      if (!val) return;
      socket.emit("serial_send", { data: val });
      appendLine("> " + val);
      sendInput.value = "";
      sendInput.focus();
    }

    if (btnSend) btnSend.addEventListener("click", doSend);
    if (sendInput) {
      sendInput.addEventListener("keydown", function (e) {
        if (e.key === "Enter") { e.preventDefault(); doSend(); }
      });
    }
  }());

  /* ═══════════════════════════════════════════
     GUIDED TOUR — interactive walkthrough overlay
     ═══════════════════════════════════════════ */

  (function () {
    var TOUR_STEPS = [
      {
        sel: ".main-header",
        title: "Header Bar",
        body: "The header is your command centre. It holds device controls, quick-action buttons, connection status, and panel toggles."
      },
      {
        sel: "#btnAddDevice",
        title: "Add a Device",
        body: "Click the <strong>+</strong> button to register a new TheaterGWD device by entering its IP address and OSC port."
      },
      {
        sel: ".hdr-query",
        title: "Query",
        body: "After selecting a device tab, click <strong>Query</strong> to ask the device for its current messages, scenes, and settings."
      },
      {
        sel: ".hdr-port-box",
        title: "Listen Port",
        body: "This is the UDP port the software listens on for OSC replies from your devices. Change it and click <strong>Apply</strong>."
      },
      {
        sel: "#btnBlackoutAll",
        title: "Blackout",
        body: "Emergency stop — immediately halts all OSC output on every connected device. Use <strong>Restore</strong> to resume."
      },
      {
        sel: ".section-nav",
        title: "Main Tabs",
        body: "Switch between the main sections: <strong>Messages</strong>, <strong>Scenes</strong>, <strong>Ori</strong>, <strong>Shows</strong>, and <strong>Advanced</strong>."
      },
      {
        sel: '.nav-btn[data-section="messages"]',
        title: "Messages",
        body: "Define what the device sends — map a sensor reading to an OSC destination (IP, port, address) and set output range scaling."
      },
      {
        sel: '.nav-btn[data-section="scenes"]',
        title: "Scenes",
        body: "Group messages together with shared timing and addressing. Start, stop, or solo scenes to control what streams."
      },
      {
        sel: '.nav-btn[data-section="direct"]',
        title: "Direct",
        body: "The fastest path — pick a sensor, enter a target, and click Send. Gooey creates a message, scene, and starts streaming in one step."
      },
      {
        sel: '.nav-btn[data-section="shows"]',
        title: "Shows",
        body: "Save and load complete device configurations. Use shows to swap between performance setups instantly."
      },
      {
        sel: '.nav-btn[data-section="advanced"]',
        title: "Advanced",
        body: "Power-user tools: raw OSC sends, additional sensor callibration, and hidden tabs like Python scripting and control from mobile devices."
      },
      {
        sel: ".hdr-tools",
        title: "Panel Toggles",
        body: "Open or close the right-side panels: <strong>Feed</strong> (live traffic), <strong>Serial</strong> (USB monitor), <strong>Notifs</strong> (history), and <strong>Ref</strong> (command reference)."
      },
      {
        sel: "#btnFeedToggle",
        title: "Live Feed",
        body: "Toggle the live feed panel. It shows every OSC message sent and received in real time — great for debugging."
      },
      {
        sel: "#btnRefToggle",
        title: "Reference Panel",
        body: "Open the searchable reference for all OSC commands, sensor keywords, config keys, and address modes. You can restart this tour from there."
      }
    ];

    var _backdrop = null;
    var _spotlight = null;
    var _tooltip = null;
    var _current = -1;
    var _active = false;

    function createOverlay() {
      if (_backdrop) return;
      _backdrop = document.createElement("div");
      _backdrop.className = "tour-backdrop";
      _backdrop.addEventListener("click", endTour);

      _spotlight = document.createElement("div");
      _spotlight.className = "tour-spotlight";

      _tooltip = document.createElement("div");
      _tooltip.className = "tour-tooltip";

      document.body.appendChild(_backdrop);
      document.body.appendChild(_spotlight);
      document.body.appendChild(_tooltip);
    }

    function removeOverlay() {
      if (_backdrop) { _backdrop.remove(); _backdrop = null; }
      if (_spotlight) { _spotlight.remove(); _spotlight = null; }
      if (_tooltip) { _tooltip.remove(); _tooltip = null; }
    }

    function showStep(index) {
      if (index < 0 || index >= TOUR_STEPS.length) { endTour(); return; }
      _current = index;
      var step = TOUR_STEPS[index];
      var el = document.querySelector(step.sel);

      if (!el) {
        /* Skip missing elements */
        if (index < TOUR_STEPS.length - 1) showStep(index + 1);
        else endTour();
        return;
      }

      var pad = 6;
      var rect = el.getBoundingClientRect();

      /* Position spotlight */
      _spotlight.style.top = (rect.top - pad) + "px";
      _spotlight.style.left = (rect.left - pad) + "px";
      _spotlight.style.width = (rect.width + pad * 2) + "px";
      _spotlight.style.height = (rect.height + pad * 2) + "px";

      /* Build dots */
      var dots = "";
      for (var d = 0; d < TOUR_STEPS.length; d++) {
        dots += '<span class="tour-dot' + (d === index ? " active" : "") + '"></span>';
      }

      /* Build tooltip content */
      _tooltip.innerHTML =
        '<div class="tour-tooltip-title">' + step.title + '</div>' +
        '<div class="tour-tooltip-body">' + step.body + '</div>' +
        '<div class="tour-tooltip-footer">' +
          '<div class="tour-progress">' + dots + '</div>' +
          '<div class="tour-tooltip-btns">' +
            (index > 0 ? '<button class="tour-btn" id="tourPrev">Back</button>' : '') +
            (index < TOUR_STEPS.length - 1
              ? '<button class="tour-btn tour-btn-primary" id="tourNext">Next</button>'
              : '<button class="tour-btn tour-btn-primary" id="tourDone">Done</button>') +
          '</div>' +
        '</div>';

      /* Wire button events */
      var btnPrev = _tooltip.querySelector("#tourPrev");
      var btnNext = _tooltip.querySelector("#tourNext");
      var btnDone = _tooltip.querySelector("#tourDone");
      if (btnPrev) btnPrev.addEventListener("click", function (e) { e.stopPropagation(); showStep(_current - 1); });
      if (btnNext) btnNext.addEventListener("click", function (e) { e.stopPropagation(); showStep(_current + 1); });
      if (btnDone) btnDone.addEventListener("click", function (e) { e.stopPropagation(); endTour(); });

      /* Position tooltip — prefer below the element, fall back to above */
      var tw = _tooltip.offsetWidth || 320;
      var th = _tooltip.offsetHeight || 200;
      var gap = 12;
      var tx, ty;

      /* Below */
      ty = rect.bottom + gap;
      if (ty + th > window.innerHeight) {
        /* Above */
        ty = rect.top - gap - th;
        if (ty < 0) ty = 10;
      }
      tx = rect.left + rect.width / 2 - tw / 2;
      if (tx < 10) tx = 10;
      if (tx + tw > window.innerWidth - 10) tx = window.innerWidth - tw - 10;

      _tooltip.style.top = ty + "px";
      _tooltip.style.left = tx + "px";

      /* Scroll element into view if needed */
      if (rect.top < 0 || rect.bottom > window.innerHeight) {
        el.scrollIntoView({ behavior: "smooth", block: "center" });
        setTimeout(function () { showStep(index); }, 350);
      }
    }

    function startTour() {
      if (_active) return;
      _active = true;
      createOverlay();
      showStep(0);
    }

    function endTour() {
      _active = false;
      _current = -1;
      removeOverlay();
    }

    /* Keyboard navigation */
    document.addEventListener("keydown", function (e) {
      if (!_active) return;
      if (e.key === "Escape") { endTour(); }
      else if (e.key === "ArrowRight" || e.key === "ArrowDown") { e.preventDefault(); showStep(_current + 1); }
      else if (e.key === "ArrowLeft" || e.key === "ArrowUp") { e.preventDefault(); showStep(_current - 1); }
    });

    /* Launch buttons */
    var btnRef = $("#btnStartTour");
    if (btnRef) btnRef.addEventListener("click", startTour);

    /* Guide button — open /docs/ in new tab (browser) or navigate in-app (Tauri) */
    var btnGuide = $("#btnGuide");
    if (btnGuide) btnGuide.addEventListener("click", function () {
      var url = location.origin + "/docs/";
      window.open(url, "_blank");
    });

    var btnOnboard = $("#btnOnboardTour");
    if (btnOnboard) btnOnboard.addEventListener("click", startTour);

    /* Expose for demo-mode auto-start */
    window._gooeyTour = { start: startTour, end: endTour };
  }());

  /* ═══════════════════════════════════════════
     PYTHON TAB
     ═══════════════════════════════════════════ */
  (function () {
    /* ── Direct auto-start toggle ── */
    (function () {
      var DIRECT_START_KEY = "gooey_direct_autostart";
      var chk = $("#chkDirectAutoStart");
      var saved = null;
      try { saved = localStorage.getItem(DIRECT_START_KEY); } catch (e) {}
      /* Default is checked (true); only override if explicitly saved as "0" */
      if (saved === "0" && chk) chk.checked = false;
      if (chk) chk.addEventListener("change", function () {
        try { localStorage.setItem(DIRECT_START_KEY, chk.checked ? "1" : "0"); } catch (e) {}
      });
    }());

    /* ── Bulk Actions toggle ── */
    (function () {
      var BULK_KEY = "gooey_bulk_actions";
      var chkBulk = $("#chkShowBulkActions");
      function setBulkVisible(on) {
        document.body.classList.toggle("bulk-actions-visible", on);
        try { localStorage.setItem(BULK_KEY, on ? "1" : "0"); } catch (e) {}
      }
      var saved = null;
      try { saved = localStorage.getItem(BULK_KEY); } catch (e) {}
      if (saved === "1") {
        if (chkBulk) chkBulk.checked = true;
        setBulkVisible(true);
      }
      if (chkBulk) chkBulk.addEventListener("change", function () { setBulkVisible(chkBulk.checked); });
    }());

    /* ── Quaternion sensors toggle ── */
    (function () {
      var QUAT_KEY = "gooey_show_quats";
      var chkQuat = $("#chkShowQuats");
      var quatCat = SENSOR_CATEGORIES.filter(function (c) { return c.id === "quaternion"; })[0];
      function setQuatsVisible(on) {
        // Add or remove the quaternion category option from all category dropdowns
        $$("select[id$='Category']").forEach(function (sel) {
          var existing = sel.querySelector('option[value="quaternion"]');
          if (on && !existing && quatCat) {
            var opt = document.createElement("option");
            opt.value = "quaternion"; opt.textContent = quatCat.label;
            sel.appendChild(opt);
          } else if (!on && existing) {
            existing.remove();
            if (sel.value === "quaternion") { sel.value = ""; sel.dispatchEvent(new Event("change")); }
          }
        });
        try { localStorage.setItem(QUAT_KEY, on ? "1" : "0"); } catch (e) {}
      }
      var saved = null;
      try { saved = localStorage.getItem(QUAT_KEY); } catch (e) {}
      if (saved === "1") {
        if (chkQuat) chkQuat.checked = true;
        setQuatsVisible(true);
      }
      if (chkQuat) chkQuat.addEventListener("change", function () { setQuatsVisible(chkQuat.checked); });
    }());

    /* ── Gate controls toggle ── */
    (function () {
      var GATE_KEY = "gooey_show_gate";
      var chkGate = $("#chkShowGate");
      function setGateVisible(on) {
        $$(".gate-section").forEach(function (el) {
          if (el.id && el.id.indexOf("Section") >= 0) el.style.display = on ? "" : "none";
        });
        try { localStorage.setItem(GATE_KEY, on ? "1" : "0"); } catch (e) {}
      }
      var saved = null;
      try { saved = localStorage.getItem(GATE_KEY); } catch (e) {}
      if (saved === "1") {
        if (chkGate) chkGate.checked = true;
        setGateVisible(true);
      }
      if (chkGate) chkGate.addEventListener("change", function () { setGateVisible(chkGate.checked); });
    }());

    /* ── Message values as gate sources toggle ── */
    (function () {
      var MSG_GATE_KEY = "gooey_msg_gate_sources";
      var chkMsg = $("#chkMsgGateSources");
      function refreshGateMsgSources(on) {
        var dev = getActiveDev();
        var names = (on && dev) ? Object.keys(dev.messages) : [];
        if (msgGatePicker) msgGatePicker.refreshMsgSources(names);
        if (sceneGatePicker) sceneGatePicker.refreshMsgSources(names);
        try { localStorage.setItem(MSG_GATE_KEY, on ? "1" : "0"); } catch (e) {}
      }
      var saved = null;
      try { saved = localStorage.getItem(MSG_GATE_KEY); } catch (e) {}
      if (saved === "1") {
        if (chkMsg) chkMsg.checked = true;
        refreshGateMsgSources(true);
      }
      if (chkMsg) chkMsg.addEventListener("change", function () { refreshGateMsgSources(chkMsg.checked); });
      /* Re-populate msg sources whenever the device or messages change */
      window._refreshGateMsgSources = function () {
        if (chkMsg && chkMsg.checked) refreshGateMsgSources(true);
      };
    }());

    /* ── Assume IP ── */
    (function () {
      var ASSUME_IP_KEY        = "gooey_assume_ip";
      var ASSUME_IP_CUSTOM_KEY = "gooey_assume_ip_custom";
      var sel         = $("#assumeIpSelect");
      var customGroup = $("#assumeIpCustomGroup");
      var customInput = $("#assumeIpCustom");

      function getPrefix() {
        if (!sel) return "";
        if (sel.value === "custom") return customInput ? customInput.value.trim() : "";
        return sel.value || "";
      }

      function updateIpLabels(prefix) {
        _assumeIpPrefix = prefix;
        var badge = "";
        if (prefix) {
          var prefixDots = (prefix.match(/\./g) || []).length;
          var needed = 4 - prefixDots;
          var placeholders = ["X", "Y", "Z"].slice(0, needed);
          badge = " (" + prefix + placeholders.join(".") + ")";
        }
        $$(".assume-ip-hint").forEach(function (el) { el.textContent = badge; });
      }

      function applySelection() {
        if (sel && sel.value === "custom") {
          if (customGroup) customGroup.style.display = "";
        } else {
          if (customGroup) customGroup.style.display = "none";
        }
        var prefix = getPrefix();
        updateIpLabels(prefix);
        try {
          localStorage.setItem(ASSUME_IP_KEY, sel ? sel.value : "");
          if (customInput) localStorage.setItem(ASSUME_IP_CUSTOM_KEY, customInput.value.trim());
        } catch (e) {}
      }

      /* Restore from localStorage */
      (function () {
        var savedVal = null, savedCustom = "";
        try {
          savedVal   = localStorage.getItem(ASSUME_IP_KEY);
          savedCustom = localStorage.getItem(ASSUME_IP_CUSTOM_KEY) || "";
        } catch (e) {}
        if (savedVal !== null && sel) {
          sel.value = savedVal;
          if (customInput) customInput.value = savedCustom;
          applySelection();
        }
      }());

      if (sel) sel.addEventListener("change", applySelection);
      if (customInput) {
        customInput.addEventListener("blur", applySelection);
        customInput.addEventListener("change", applySelection);
      }
    }());
    var SCRIPT_DRAFT_KEY = "gooey_script_draft";
    var SCRIPT_NAME_KEY = "gooey_script_name";
    var navBtn = $("#navScript");
    var chkEnable = $("#chkEnableScript");
    var editor = $("#scriptEditor");
    var lineNums = $("#scriptLineNumbers");
    var consoleEl = $("#scriptConsole");
    var btnRun = $("#btnScriptRun");
    var btnStop = $("#btnScriptStop");
    var btnClear = $("#btnScriptConsoleClear");
    var btnSave = $("#btnScriptSave");
    var btnSaveAs = $("#btnScriptSaveAs");
    var btnLoad = $("#btnScriptLoad");
    var btnDelete = $("#btnScriptDelete");
    var btnTemplate = $("#btnScriptTemplate");
    var selLoad = $("#scriptLoadSelect");
    var selTemplate = $("#scriptTemplateSelect");
    var selMode = $("#scriptMode");
    var inputInterval = $("#scriptInterval");
    var inputListenPort = $("#scriptListenPort");

    var btnMirrorFeed = $("#chkScriptMirrorFeed");

    var scriptRunning = false;
    var mirrorToFeed = false;
    var currentScriptName = "";

    /* ── Enable/disable toggle ── */
    function setScriptEnabled(on) {
      if (navBtn) navBtn.style.display = on ? "" : "none";
      try { localStorage.setItem(SCRIPT_KEY, on ? "1" : "0"); } catch (e) {}
    }

    // Restore from localStorage
    (function () {
      var saved = null;
      try { saved = localStorage.getItem(SCRIPT_KEY); } catch (e) {}
      if (saved === "1") {
        if (chkEnable) chkEnable.checked = true;
        setScriptEnabled(true);
      }
    }());

    if (chkEnable) {
      chkEnable.addEventListener("change", function () {
        setScriptEnabled(chkEnable.checked);
        if (!chkEnable.checked) {
          // If currently on the script tab, switch away
          if (navBtn && navBtn.classList.contains("active")) {
            var msgBtn = $(".nav-btn[data-section='messages']");
            if (msgBtn) msgBtn.click();
          }
        }
      });
    }

    /* ── Line numbers ── */
    function updateLineNumbers() {
      if (!editor || !lineNums) return;
      var lines = editor.value.split("\n").length;
      var nums = [];
      for (var i = 1; i <= lines; i++) nums.push(i);
      lineNums.textContent = nums.join("\n");
    }

    if (editor) {
      editor.addEventListener("input", updateLineNumbers);
      editor.addEventListener("scroll", function () {
        if (lineNums) lineNums.scrollTop = editor.scrollTop;
      });

      // Tab key inserts spaces
      editor.addEventListener("keydown", function (e) {
        if (e.key === "Tab") {
          e.preventDefault();
          var start = editor.selectionStart;
          var end = editor.selectionEnd;
          editor.value = editor.value.substring(0, start) + "    " + editor.value.substring(end);
          editor.selectionStart = editor.selectionEnd = start + 4;
          updateLineNumbers();
        }
      });

      // Restore draft
      try {
        var draft = localStorage.getItem(SCRIPT_DRAFT_KEY);
        if (draft) editor.value = draft;
        var savedName = localStorage.getItem(SCRIPT_NAME_KEY);
        if (savedName) currentScriptName = savedName;
      } catch (e) {}
      updateLineNumbers();

      // Auto-save draft
      editor.addEventListener("input", function () {
        try { localStorage.setItem(SCRIPT_DRAFT_KEY, editor.value); } catch (e) {}
      });
    }

    /* ── Mirror to Feed toggle ── */
    if (btnMirrorFeed) {
      btnMirrorFeed.addEventListener("change", function () {
        mirrorToFeed = btnMirrorFeed.checked;
      });
    }

    /* ── Console output ── */
    function appendConsole(text, level) {
      if (!consoleEl) return;
      var line = document.createElement("div");
      line.className = "script-console-line" + (level === "error" ? " error" : level === "warn" ? " warn" : "");
      line.textContent = text;
      consoleEl.appendChild(line);
      // Auto-scroll
      consoleEl.scrollTop = consoleEl.scrollHeight;
      // Limit lines
      while (consoleEl.children.length > 500) {
        consoleEl.removeChild(consoleEl.firstChild);
      }
    }

    socket.on("script_output", function (data) {
      var prefix = data.time ? "[" + data.time + "] " : "";
      appendConsole(prefix + data.text, data.level);
      // Mirror to Live Feed if enabled
      if (mirrorToFeed) {
        var feedLog = $("#feedLog");
        if (feedLog) {
          var entry = document.createElement("div");
          entry.className = "feed-entry";
          entry.innerHTML =
            '<span class="feed-time">' + (data.time || "") + '</span> '
            + '<span class="feed-dir ' + (data.level === "error" ? "recv" : "send") + '">[py]</span> '
            + '<span class="feed-addr">' + data.text.replace(/</g, "&lt;") + '</span>';
          feedLog.appendChild(entry);
          if ($("#feedAutoScroll") && $("#feedAutoScroll").checked) {
            feedLog.scrollTop = feedLog.scrollHeight;
          }
        }
      }
    });

    socket.on("script_stopped", function () {
      scriptRunning = false;
      if (btnRun) btnRun.disabled = false;
      if (btnStop) btnStop.disabled = true;
      if (editor) editor.readOnly = false;
    });

    /* ── Run / Stop ── */
    if (btnRun) {
      btnRun.addEventListener("click", function () {
        if (!editor) return;
        var code = editor.value.trim();
        if (!code) { toast("No script to run", "error"); return; }
        var interval = inputInterval ? parseInt(inputInterval.value, 10) || 50 : 50;
        if (interval < 10) {
          toast("Minimum interval is 10ms — clamped to 10ms", "info");
          interval = 10;
          if (inputInterval) inputInterval.value = 10;
        }
        scriptRunning = true;
        btnRun.disabled = true;
        if (btnStop) btnStop.disabled = false;
        editor.readOnly = true;
        socket.emit("script_run", {
          code: code,
          mode: selMode ? selMode.value : "loop",
          interval: interval,
          listen_port: inputListenPort ? parseInt(inputListenPort.value, 10) || null : null,
          device_id:   activeDeviceId || null,
          device_host: devHost(),
          device_port: devPort(),
          device_name: devName(),
        });
      });
    }

    if (btnStop) {
      btnStop.addEventListener("click", function () {
        socket.emit("script_stop");
      });
    }

    if (btnClear) {
      btnClear.addEventListener("click", function () {
        if (consoleEl) consoleEl.innerHTML = "";
      });
    }

    /* ── Save / Load / Delete ── */
    function refreshScriptList() {
      api("scripts", null, "GET").then(function (res) {
        if (!selLoad || !res.scripts) return;
        selLoad.innerHTML = '<option value="">-- select --</option>';
        res.scripts.forEach(function (s) {
          var opt = document.createElement("option");
          opt.value = s.name;
          opt.textContent = s.name;
          if (s.name === currentScriptName) opt.selected = true;
          selLoad.appendChild(opt);
        });
      });
    }

    if (btnSave) {
      btnSave.addEventListener("click", function () {
        if (!editor) return;
        var name = currentScriptName || prompt("Script name:");
        if (!name) return;
        name = name.trim();
        if (!name) return;
        api("scripts/" + encodeURIComponent(name), { code: editor.value }).then(function (res) {
          if (res.status === "ok") {
            currentScriptName = name;
            try { localStorage.setItem(SCRIPT_NAME_KEY, name); } catch (e) {}
            toast("Saved: " + name, "info");
            refreshScriptList();
          }
        });
      });
    }

    if (btnSaveAs) {
      btnSaveAs.addEventListener("click", function () {
        if (!editor) return;
        var name = prompt("Save script as:", currentScriptName || "");
        if (!name) return;
        name = name.trim();
        if (!name) return;
        api("scripts/" + encodeURIComponent(name), { code: editor.value }).then(function (res) {
          if (res.status === "ok") {
            currentScriptName = name;
            try { localStorage.setItem(SCRIPT_NAME_KEY, name); } catch (e) {}
            toast("Saved: " + name, "info");
            refreshScriptList();
          }
        });
      });
    }

    if (btnLoad) {
      btnLoad.addEventListener("click", function () {
        if (!selLoad) return;
        var name = selLoad.value;
        if (!name) { toast("Select a script first", "error"); return; }
        api("scripts/" + encodeURIComponent(name), null, "GET").then(function (res) {
          if (res.status === "ok" && editor) {
            editor.value = res.code;
            currentScriptName = name;
            try {
              localStorage.setItem(SCRIPT_DRAFT_KEY, res.code);
              localStorage.setItem(SCRIPT_NAME_KEY, name);
            } catch (e) {}
            updateLineNumbers();
            toast("Loaded: " + name, "info");
          }
        });
      });
    }

    if (btnDelete) {
      btnDelete.addEventListener("click", function () {
        if (!selLoad) return;
        var name = selLoad.value;
        if (!name) { toast("Select a script first", "error"); return; }
        showConfirm("Delete Python Script", "Delete \"" + name + "\"? This cannot be undone.", function () {
          api("scripts/" + encodeURIComponent(name), null, "DELETE").then(function (res) {
            if (res.status === "ok") {
              if (currentScriptName === name) {
                currentScriptName = "";
                try { localStorage.removeItem(SCRIPT_NAME_KEY); } catch (e) {}
              }
              toast("Deleted: " + name, "info");
              refreshScriptList();
            }
          });
        });
      });
    }

    /* ── Templates ── */
    var TEMPLATES = {
      threshold_gate:
        "# Threshold Gate\n"
        + "# Only send when a sensor exceeds a threshold\n\n"
        + 'accel = sensor("accelLength")\n'
        + "threshold = 0.5\n\n"
        + "if accel > threshold:\n"
        + '    osc_send("192.168.1.50", 7000, "/light/intensity", accel)\n'
        + '    print(f"Sent: {accel:.3f}")\n',

      multi_sensor:
        "# Multi-Sensor Combiner\n"
        + "# Combine accelerometer and gyroscope into an 'energy' metric\n\n"
        + 'accel = sensor("accelLength")\n'
        + 'gyro = sensor("gyroLength")\n\n'
        + "# Weighted combination\n"
        + "energy = accel * 0.6 + gyro * 0.4\n\n"
        + "# Smooth with exponential moving average\n"
        + 'if "smooth" not in state:\n'
        + '    state["smooth"] = 0.0\n'
        + 'state["smooth"] = state["smooth"] * 0.8 + energy * 0.2\n\n'
        + 'osc_send("192.168.1.50", 7000, "/energy", state["smooth"])\n'
        + 'print(f"energy={state[\'smooth\']:.3f}")\n',

      timed_crossfade:
        "# Timed Crossfade\n"
        + "# Smoothly transition between two values over time\n\n"
        + "duration = 5.0  # seconds\n"
        + "t = elapsed() % (duration * 2)  # ping-pong\n\n"
        + "if t > duration:\n"
        + "    t = duration * 2 - t  # reverse\n\n"
        + "fade = t / duration  # 0 to 1\n\n"
        + "val_a = 0\n"
        + "val_b = 255\n"
        + "result = val_a + (val_b - val_a) * fade\n\n"
        + 'osc_send("192.168.1.50", 7000, "/crossfade", result)\n'
        + 'print(f"fade={fade:.2f}  result={result:.1f}")\n',

      state_machine:
        "# State Machine\n"
        + "# Switch between idle/active/cooldown based on motion\n\n"
        + 'if "mode" not in state:\n'
        + '    state["mode"] = "idle"\n'
        + '    state["timer"] = 0\n\n'
        + 'accel = sensor("accelLength")\n'
        + "d = dt()\n\n"
        + 'if state["mode"] == "idle":\n'
        + "    if accel > 0.7:\n"
        + '        state["mode"] = "active"\n'
        + '        print("-> ACTIVE")\n\n'
        + 'elif state["mode"] == "active":\n'
        + '    osc_send("192.168.1.50", 7000, "/active", accel)\n'
        + "    if accel < 0.3:\n"
        + '        state["mode"] = "cooldown"\n'
        + '        state["timer"] = 2.0  # 2 second cooldown\n'
        + '        print("-> COOLDOWN")\n\n'
        + 'elif state["mode"] == "cooldown":\n'
        + '    state["timer"] -= d\n'
        + '    if state["timer"] <= 0:\n'
        + '        state["mode"] = "idle"\n'
        + '        print("-> IDLE")\n',
    };

    if (btnTemplate) {
      btnTemplate.addEventListener("click", function () {
        if (!selTemplate || !editor) return;
        var key = selTemplate.value;
        if (!key || !TEMPLATES[key]) { toast("Select a template first", "error"); return; }
        if (editor.value.trim()) {
          showConfirm("Load Template", "Replace current editor contents with template?", function () {
            editor.value = TEMPLATES[key];
            try { localStorage.setItem(SCRIPT_DRAFT_KEY, editor.value); } catch (e) {}
            updateLineNumbers();
          }, "Replace", true);
        } else {
          editor.value = TEMPLATES[key];
          try { localStorage.setItem(SCRIPT_DRAFT_KEY, editor.value); } catch (e) {}
          updateLineNumbers();
        }
      });
    }

    /* ── Check running status on connect ── */
    socket.on("connect", function () {
      socket.emit("script_status");
    });
    socket.on("script_status_reply", function (data) {
      scriptRunning = data.running;
      if (btnRun) btnRun.disabled = data.running;
      if (btnStop) btnStop.disabled = !data.running;
      if (editor) editor.readOnly = data.running;
    });

    /* ── Refresh script list when navigating to the tab ── */
    var origNavHandler = null;
    if (navBtn) {
      navBtn.addEventListener("click", function () {
        refreshScriptList();
      });
    }

    // Initial load if enabled
    try {
      if (localStorage.getItem(SCRIPT_KEY) === "1") refreshScriptList();
    } catch (e) {}
  }());

})();
