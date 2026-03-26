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

  /* ── Section nav ── */
  $$(".nav-btn").forEach(function (btn) {
    btn.addEventListener("click", function () {
      $$(".nav-btn").forEach(function (b) { b.classList.remove("active"); });
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
  var _unseenNotifs = 0;

  function updateNotifBadge() {
    var badge = $("#notifBadge");
    if (!badge) return;
    if (_unseenNotifs > 0) {
      badge.textContent = _unseenNotifs;
      badge.style.display = "";
    } else {
      badge.style.display = "none";
    }
  }

  function renderNotifDropdown() {
    var dd = $("#notifDropdown");
    if (!dd) return;
    dd.innerHTML = "";
    if (_toastHistory.length === 0) {
      dd.innerHTML = '<div class="notif-item"><span class="notif-msg" style="color:var(--text-light)">No recent notifications</span></div>';
      return;
    }
    _toastHistory.slice().reverse().forEach(function (item) {
      var div = document.createElement("div");
      div.className = "notif-item";
      div.innerHTML = '<span class="notif-time">' + item.time + '</span><span class="notif-msg notif-type-' + item.type + '">' + item.msg + '</span>';
      dd.appendChild(div);
    });
  }

  var _notifDropdownOpen = false;
  var btnNotifToggle = $("#btnNotifToggle");
  if (btnNotifToggle) {
    btnNotifToggle.addEventListener("click", function (e) {
      e.stopPropagation();
      _notifDropdownOpen = !_notifDropdownOpen;
      var dd = $("#notifDropdown");
      if (dd) {
        if (_notifDropdownOpen) {
          _unseenNotifs = 0;
          updateNotifBadge();
          renderNotifDropdown();
          dd.classList.remove("hidden");
        } else {
          dd.classList.add("hidden");
        }
      }
    });
  }
  document.addEventListener("click", function (e) {
    if (!e.target.closest("#notifDropdown") && !e.target.closest("#btnNotifToggle")) {
      var dd = $("#notifDropdown");
      if (dd && !dd.classList.contains("hidden")) {
        dd.classList.add("hidden");
        _notifDropdownOpen = false;
      }
    }
  });

  /* ── showToast (public alias: toast) ── */
  function showToast(msg, type, duration) {
    type = type || "info";
    /* Add to history (max 10) */
    var now = new Date();
    var timeStr = now.getHours().toString().padStart(2, "0") + ":" + now.getMinutes().toString().padStart(2, "0") + ":" + now.getSeconds().toString().padStart(2, "0");
    _toastHistory.push({ msg: msg, type: type, time: timeStr });
    if (_toastHistory.length > 10) _toastHistory.shift();
    _unseenNotifs++;
    updateNotifBadge();

    var el = document.createElement("div");
    el.className = "toast toast-" + type;
    /* For errors: show close button, no auto-dismiss */
    if (type === "error") {
      var closeBtn = document.createElement("button");
      closeBtn.className = "toast-close";
      closeBtn.textContent = "✕";
      closeBtn.addEventListener("click", function () {
        el.style.opacity = "0";
        el.style.transform = "translateX(20px)";
        el.style.transition = "all 0.2s ease-out";
        setTimeout(function () { el.remove(); }, 250);
      });
      el.appendChild(closeBtn);
      el.appendChild(document.createTextNode(msg));
    } else {
      el.textContent = msg;
      var dismissAfter = (duration !== undefined && duration > 0) ? duration : 3000;
      if (dismissAfter > 0) {
        setTimeout(function () {
          el.style.opacity = "0";
          el.style.transform = "translateX(20px)";
          el.style.transition = "all 0.2s ease-out";
          setTimeout(function () { el.remove(); }, 250);
        }, dismissAfter);
      }
    }
    $("#toastContainer").appendChild(el);
  }

  /* Backward-compatible alias */
  function toast(msg, type) { showToast(msg, type); }

  /* ── Confirm modal ── */
  function showConfirm(title, body, onConfirm, okLabel, danger) {
    if (okLabel === undefined) okLabel = "Confirm";
    if (danger === undefined) danger = true;
    var modal = document.getElementById("confirmModal");
    if (!modal) { if (onConfirm) onConfirm(); return; }
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
  var devices = {};        // { id: { host, port, name, messages:{}, patches:{} } }
  var activeDeviceId = "";  // current tab

  function generateDeviceId(host, port, name) {
    return name + "@" + host + ":" + port;
  }

  function addDevice(host, port, name) {
    var id = generateDeviceId(host, port, name);
    if (!devices[id]) {
      devices[id] = { host: host, port: parseInt(port, 10), name: name, messages: {}, patches: {}, oris: {} };
    }
    activeDeviceId = id;
    renderDeviceTabs();
    refreshAllDropdowns();
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
    renderPatchTable();
    renderOriTable();
    refreshAllDropdowns();
  }

  function getActiveDev() {
    return devices[activeDeviceId] || null;
  }

  function devHost() { var d = getActiveDev(); return d ? d.host : "127.0.0.1"; }
  function devPort() { var d = getActiveDev(); return d ? d.port : 8000; }
  function devName() { var d = getActiveDev(); return d ? d.name : ""; }

  /* ── Device tab rendering ── */
  function renderDeviceTabs() {
    var container = $("#hdrDevices");
    var devActions = $("#hdrDevActions");
    /* Remove existing tabs */
    $$(".hdr-dev-tab").forEach(function (t) { t.remove(); });
    var devCount = Object.keys(devices).length;
    Object.keys(devices).forEach(function (id) {
      var d = devices[id];
      var isActive = (id === activeDeviceId);
      var btn = document.createElement("button");
      btn.className = "hdr-dev-tab" + (isActive ? " active" : "");
      btn.dataset.deviceId = id;
      /* Build button content: status dot + sanitised device name + caret */
      var dot = document.createElement("span");
      dot.className = "dev-dot";
      dot.title = isActive ? "Active device" : "Inactive";
      btn.appendChild(dot);
      btn.appendChild(document.createTextNode(d.name));
      var caret = document.createElement("span");
      caret.className = "tab-caret";
      caret.textContent = "▾";
      btn.appendChild(caret);
      btn.addEventListener("click", function (e) {
        /* Select device as active */
        activeDeviceId = id;
        renderDeviceTabs();
        renderMsgTable();
        renderPatchTable();
        renderOriTable();
        refreshAllDropdowns();
        refreshQueryDeviceSelect();
        /* Show per-device dropdown menu */
        openDevDropdown(btn, id);
      });
      container.insertBefore(btn, devActions);
    });
    /* Show/hide welcome banner based on device count */
    var wb = $("#welcomeBanner");
    if (wb) wb.style.display = devCount === 0 ? "" : "none";
    /* Update onboarding steps */
    updateOnboarding();
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
  ["statusIP", "msgIP", "directIP", "rawHost", "bridgeOutHost", "patchIP"].forEach(function (fieldId) {
    var el = $("#" + fieldId);
    if (!el) return;
    el.addEventListener("blur", function () {
      if (el.value.trim().toLowerCase() === "me") {
        resolveIp("me", function (ip) { el.value = ip; });
      }
    });
  });

  /* ── Edit existing device ── */
  function editDevice(id) {
    var d = devices[id];
    var host = prompt("Device IP (enter 'me' for this computer):", d.host);
    if (host === null) return;
    var port = prompt("Device port:", d.port);
    if (port === null) return;
    var name = prompt("Device name:", d.name);
    if (name === null) return;
    resolveIp(host, function (resolvedHost) {
      var wasActive = (activeDeviceId === id);
      delete devices[id];
      if (wasActive) activeDeviceId = "";
      addDevice(resolvedHost, parseInt(port, 10), name.trim());
      renderMsgTable();
      renderPatchTable();
      renderOriTable();
      toast("Device updated: " + name.trim(), "success");
    });
  }

  /* ── Add-device button ── */
  $("#btnAddDevice").addEventListener("click", function () {
    var host = prompt("Device IP address (enter 'me' for this computer):", "192.168.1.100");
    if (!host) return;
    var port = prompt("Device port:", "8000");
    if (!port) return;
    var name = prompt("Device name (unique identifier):", "");
    if (!name) return;
    resolveIp(host, function (resolvedHost) {
      addDevice(resolvedHost, parseInt(port, 10), name.trim());
      renderMsgTable();
      renderPatchTable();
      renderOriTable();
      toast("Device added: " + name.trim(), "success");
    });
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
  }

  function closeDevDropdown() {
    $("#devDropdown").style.display = "none";
    _dropdownDeviceId = "";
  }

  /* Close when clicking outside the dropdown or a device tab */
  document.addEventListener("click", function (e) {
    if (!e.target.closest("#devDropdown") && !e.target.closest(".hdr-dev-tab")) {
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
  $("#devDdQuery").addEventListener("click", function () {
    var id = _dropdownDeviceId;
    if (!id || !devices[id]) { closeDevDropdown(); return; }
    var d = devices[id];
    sendToDevice(id, "/annieData/" + d.name + "/list/all", "verbose").then(function (res) {
      if (res.status === "ok") toast("Querying " + d.name + "…", "info");
    });
    closeDevDropdown();
  });

  $("#devDdStatusConfig").addEventListener("click", function () {
    closeDevDropdown();
    /* Switch to dashboard tab and scroll to status config card */
    $(".nav-btn[data-section='dashboard']").click();
    var card = $('[data-card-id="status-config"]');
    if (card) card.scrollIntoView({ behavior: "smooth", block: "start" });
  });

  $("#devDdSave").addEventListener("click", function () {
    var id = _dropdownDeviceId;
    if (!id || !devices[id]) { closeDevDropdown(); return; }
    var d = devices[id];
    sendToDevice(id, "/annieData/" + d.name + "/save", null).then(function (res) {
      if (res.status === "ok") toast("Saved: " + d.name, "success");
    });
    closeDevDropdown();
  });

  $("#devDdLoad").addEventListener("click", function () {
    var id = _dropdownDeviceId;
    if (!id || !devices[id]) { closeDevDropdown(); return; }
    var d = devices[id];
    sendToDevice(id, "/annieData/" + d.name + "/load", null).then(function (res) {
      if (res.status === "ok") toast("Loaded: " + d.name, "success");
    });
    closeDevDropdown();
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

  $("#devDdDedupOn").addEventListener("click", function () {
    var id = _dropdownDeviceId;
    if (!id || !devices[id]) { closeDevDropdown(); return; }
    var d = devices[id];
    sendToDevice(id, "/annieData/" + d.name + "/dedup", "on").then(function (res) {
      if (res.status === "ok") toast("Dedup on: " + d.name, "success");
    });
    closeDevDropdown();
  });

  $("#devDdDedupOff").addEventListener("click", function () {
    var id = _dropdownDeviceId;
    if (!id || !devices[id]) { closeDevDropdown(); return; }
    var d = devices[id];
    sendToDevice(id, "/annieData/" + d.name + "/dedup", "off").then(function (res) {
      if (res.status === "ok") toast("Dedup off: " + d.name, "success");
    });
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
    dedup:          "/annieData/{device}/dedup",
    save:           "/annieData/{device}/save",
    load:           "/annieData/{device}/load",
    nvs_clear:      "/annieData/{device}/nvs/clear",
    list_messages:  "/annieData/{device}/list/msgs",
    list_patches:   "/annieData/{device}/list/patches",
    list_all:       "/annieData/{device}/list/all",
  };

  /* ═══════════════════════════════════════════
     REPLY PARSING  — auto-populate registry
     ═══════════════════════════════════════════ */

  /**
   * Parse incoming OSC messages from the device. The device sends status
   * replies that include message and patch info. We try to extract names
   * and parameters from them to keep the local registry in sync.
   *
   * Typical reply patterns:
   *   /annieData/{device}/status   "msg: accelX | value:accelX ip:192.168.1.50 ..."
   *   /annieData/{device}/status   "patch: sensors | period:50 adrMode:fallback msgs:accelX+accelY"
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
       The device sends replies to /reply/{dev}/list/msgs (or /patches, /all)
       with a multi-line payload: "Messages (N):\n  name1\n  name2\n..."
       Detect by address; fall back to text-pattern for legacy status messages. */
    var listAddr = entry.address || "";
    var isListReply = /\/list\/(msgs|messages|patches|all)/i.test(listAddr);
    var isLegacyList = !isListReply && text.match(/list\/(?:msgs|patches|all):\s*(.+)/i);
    if (isListReply || isLegacyList) {
      if (isLegacyList) {
        var legacyMatch = text.match(/list\/(?:msgs|patches|all):\s*(.+)/i);
        var names = legacyMatch[1].split(/[,\s]+/).map(function (s) { return s.trim(); }).filter(Boolean);
        var isMsgList = text.match(/list\/msgs/i) || text.match(/list\/all/i);
        var isPatchList = text.match(/list\/patches/i) || text.match(/list\/all/i);
        names.forEach(function (n) {
          if (isMsgList   && !dev.messages[n]) dev.messages[n] = {};
          if (isPatchList && !dev.patches[n])  dev.patches[n]  = {};
        });
      } else {
        /* Multi-line reply format from the device. For /list/all, track which
           block we are in; for /list/msgs or /list/patches use the address. */
        var isAllList  = /\/list\/all/i.test(listAddr);
        var isMsgList  = /\/list\/msgs/i.test(listAddr);
        var curBlock = isAllList ? "" : (isMsgList ? "msg" : "patch");
        text.split(/\n/).forEach(function (line) {
          var trimmed = line.trim();
          if (!trimmed) return;
          if (/^messages\s*\(\d+\):/i.test(trimmed)) { curBlock = "msg";   return; }
          if (/^patches\s*\(\d+\):/i.test(trimmed))  { curBlock = "patch"; return; }
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
          if (curBlock === "patch") {
            var pParams = parseConfigString(trimmed);
            // Extract send period from "[RUNNING, 50ms, …]" or "[STOPPED, 50ms, …]".
            var periodM = trimmed.match(/\[(?:RUNNING|STOPPED),\s*(\d+)ms/i);
            if (periodM) pParams.period = periodM[1];
            dev.patches[n] = Object.assign(dev.patches[n] || {}, pParams);
          }
        });
      }
      renderMsgTable();
      renderPatchTable();
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

    /* ── Parse patch info reply ── */
    var patchMatch = text.match(/patch:\s*(\S+)\s*\|\s*(.*)/i);
    if (patchMatch) {
      var pName = patchMatch[1];
      var pParams = parseConfigString(patchMatch[2]);
      dev.patches[pName] = Object.assign(dev.patches[pName] || {}, pParams);
      renderPatchTable();
      refreshAllDropdowns();
      return;
    }

    /* ── Parse ori list reply ──
       Address contains /ori/list.  Payload format:
       "light1, spot [R3] (*), light2"  or  "(none)" */
    if (/\/ori\/list/i.test(listAddr)) {
      dev.oris = {};
      if (text !== "(none)") {
        var oriParts = text.split(/,\s*/);
        oriParts.forEach(function (part) {
          part = part.trim();
          if (!part) return;
          var om = part.match(/^(\S+)\s*(?:\[R(\d+)\])?\s*(\(\*\))?/);
          if (om) {
            var oName = om[1];
            var samples = om[2] ? parseInt(om[2], 10) : 1;
            dev.oris[oName] = {
              name: oName,
              type: samples >= 2 ? "range" : "point",
              samples: samples,
              color: null,
              active: !!om[3]
            };
          }
        });
      }
      renderOriTable();
      refreshAllDropdowns();
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
      toast("Active ori: " + text, "info");
      return;
    }

    /* ── Parse ori color reply ──
       "name: r,g,b" */
    if (/\/ori\/color/i.test(listAddr)) {
      var colorMatch = text.match(/^(\S+):\s*(\d+),(\d+),(\d+)/);
      if (colorMatch && dev.oris[colorMatch[1]]) {
        dev.oris[colorMatch[1]].color = [
          parseInt(colorMatch[2], 10),
          parseInt(colorMatch[3], 10),
          parseInt(colorMatch[4], 10)
        ];
        renderOriTable();
      }
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
        dev.patches[vName] = Object.assign(dev.patches[vName] || {}, vpParams);
        renderPatchTable();
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

  function renderMsgTable() {
    var dev = getActiveDev();
    var tbody = $("#msgTableBody");
    tbody.innerHTML = "";
    if (!dev || Object.keys(dev.messages).length === 0) {
      tbody.innerHTML = '<tr><td colspan="11"><div class="empty-state"><div class="empty-icon">📭</div><div class="empty-text">No messages tracked yet.</div><div class="empty-sub">Query the device to load existing messages, or create one below.</div><button class="btn btn-sm" onclick="document.getElementById(\'btnQueryVerbose\').click()">⚡ Query Device Now</button></div></td></tr>';
      updateOnboarding();
      return;
    }
    Object.keys(dev.messages).forEach(function (name) {
      var m = dev.messages[name];
      var oriStr = "";
      if (m.ori_only || m.orionly) oriStr = "only:" + (m.ori_only || m.orionly);
      else if (m.ori_not || m.orinot) oriStr = "not:" + (m.ori_not || m.orinot);
      else if (m.ternori) oriStr = "tern:" + m.ternori;
      var tr = document.createElement("tr");
      tr.dataset.msgName = name;
      tr.innerHTML =
        '<td class="cell-name" data-label="Name">' + esc(name) + '</td>' +
        '<td class="cell-mono" data-label="Sensor">' + esc(m.value || m.val || "") + '</td>' +
        '<td class="cell-mono" data-label="IP">' + esc(m.ip || "") + '</td>' +
        '<td class="cell-mono" data-label="Port">' + esc(m.port || "") + '</td>' +
        '<td class="cell-mono" data-label="Address">' + esc(m.adr || m.addr || m.address || "") + '</td>' +
        '<td class="cell-mono" data-label="Low" data-col="low">' + esc(m.low || m.min || "") + '</td>' +
        '<td class="cell-mono" data-label="High" data-col="high">' + esc(m.high || m.max || "") + '</td>' +
        '<td class="cell-mono" data-label="Patch" data-col="patch">' + esc(m.patch || "") + '</td>' +
        '<td class="cell-mono ori-section" data-label="Ori" data-col="ori">' + esc(oriStr || "—") + '</td>' +
        '<td data-label="Enabled">' + (m.enabled === "false" ? "❌" : "✅") + '</td>' +
        '<td class="cell-actions" data-label="Actions">' +
          '<button class="tbl-btn" data-act="info" aria-label="Info">ℹ️</button>' +
          '<button class="tbl-btn tbl-btn-success" data-act="enable" aria-label="Toggle enabled">✅</button>' +
          '<button class="tbl-btn" data-act="disable" aria-label="Mute">🔇</button>' +
          '<button class="tbl-btn" data-act="save" aria-label="Save to device">💾</button>' +
          '<button class="tbl-btn tbl-btn-danger" data-act="delete" aria-label="Delete">🗑</button>' +
        '</td>';
      /* Row click → populate edit form */
      tr.querySelector(".cell-name").addEventListener("click", function () {
        populateMsgForm(name, m);
      });
      /* Action buttons */
      tr.querySelectorAll(".tbl-btn").forEach(function (btn) {
        btn.addEventListener("click", function () {
          msgAction(btn.dataset.act, name);
        });
      });
      tbody.appendChild(tr);
    });
    applyColVisibility();
    updateOnboarding();
  }

  function populateMsgForm(name, m) {
    $("#msgName").value = name;
    $("#msgValue").value = m.value || m.val || "";
    $("#msgIP").value = m.ip || "";
    $("#msgPort").value = m.port || "9000";
    $("#msgAdr").value = m.adr || m.addr || m.address || "";
    $("#msgLow").value = m.low || m.min || "";
    $("#msgHigh").value = m.high || m.max || "";
    $("#msgPatch").value = m.patch || "";
    $("#msgOriOnly").value = m.ori_only || m.orionly || "";
    $("#msgOriNot").value = m.ori_not || m.orinot || "";
    $("#msgTernori").value = m.ternori || "";
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
      case "save":    sendCmd(addr("/annieData/{device}/save/msg"), name); return;
      case "delete":  template = "/annieData/{device}/msg/{name}/delete"; break;
      default: return;
    }
    sendCmd(addr(template, name), null).then(function (res) {
      if (res.status === "ok") toast(act + ": " + name, "success");
    });
  }

  /* ═══════════════════════════════════════════
     PATCH TABLE
     ═══════════════════════════════════════════ */

  function renderPatchTable() {
    var dev = getActiveDev();
    var tbody = $("#patchTableBody");
    tbody.innerHTML = "";
    if (!dev || Object.keys(dev.patches).length === 0) {
      tbody.innerHTML = '<tr><td colspan="6"><div class="empty-state"><div class="empty-icon">📦</div><div class="empty-text">No patches tracked yet.</div><div class="empty-sub">Query the device or create a patch below.</div><button class="btn btn-sm" onclick="document.getElementById(\'btnQueryVerbose\').click()">⚡ Query Device Now</button></div></td></tr>';
      return;
    }
    Object.keys(dev.patches).forEach(function (name) {
      var p = dev.patches[name];
      var msgsStr = p.msgs || "";
      if (typeof msgsStr === "string") msgsStr = msgsStr.replace(/\+/g, ", ");
      var tr = document.createElement("tr");
      tr.dataset.patchName = name;
      tr.innerHTML =
        '<td class="cell-name" data-label="Name">' + esc(name) + '</td>' +
        '<td class="cell-mono" data-label="Period">' + esc(p.period || "50") + ' ms</td>' +
        '<td class="cell-mono" data-label="Adr Mode">' + esc(p.adrMode || p.adrmode || p.adr_mode || "fallback") + '</td>' +
        '<td class="cell-mono" data-label="Overrides">' + esc(p.override || "—") + '</td>' +
        '<td class="cell-mono" data-label="Messages" style="max-width:140px;overflow:hidden;text-overflow:ellipsis" title="' + esc(msgsStr) + '">' + esc(msgsStr || "—") + '</td>' +
        '<td class="cell-actions" data-label="Actions">' +
          '<button class="tbl-btn tbl-btn-success" data-act="start" aria-label="Start patch">▶</button>' +
          '<button class="tbl-btn tbl-btn-stop" data-act="stop" aria-label="Stop patch">⏹</button>' +
          '<button class="tbl-btn" data-act="info" aria-label="Info">ℹ️</button>' +
          '<button class="tbl-btn" data-act="enableAll" aria-label="Enable all messages">✅</button>' +
          '<button class="tbl-btn" data-act="unsolo" aria-label="Unsolo">🔊</button>' +
          '<button class="tbl-btn" data-act="save" aria-label="Save to device">💾</button>' +
          '<button class="tbl-btn tbl-btn-danger" data-act="delete" aria-label="Delete">🗑</button>' +
        '</td>';
      tr.querySelector(".cell-name").addEventListener("click", function () {
        populatePatchForm(name, p);
      });
      tr.querySelectorAll(".tbl-btn").forEach(function (btn) {
        btn.addEventListener("click", function () {
          patchAction(btn.dataset.act, name);
        });
      });
      tbody.appendChild(tr);
    });
  }

  function populatePatchForm(name, p) {
    $("#patchName").value = name;
    $("#patchPeriod").value = p.period || "50";
    $("#patchAdrMode").value = p.adrMode || p.adrmode || p.adr_mode || "fallback";
    $("#patchIP").value = p.ip || "";
    $("#patchPort").value = p.port || "9000";
    $("#patchAdr").value = p.adr || "";
    $("#patchLow").value = p.low || "";
    $("#patchHigh").value = p.high || "";
    /* Accept both legacy "+" and canonical comma-separated override replies. */
    var ov = (p.override || "").split(/[+,]/).map(function (s) { return s.trim(); }).filter(Boolean);
    $("#ovIP").checked = ov.indexOf("ip") !== -1;
    $("#ovPort").checked = ov.indexOf("port") !== -1;
    $("#ovAdr").checked = ov.indexOf("adr") !== -1;
    $("#ovLow").checked = ov.indexOf("low") !== -1;
    $("#ovHigh").checked = ov.indexOf("high") !== -1;
    /* scroll to form — switch to patches tab */
    $(".nav-btn[data-section='patches']").click();
    $("#patchName").focus();
  }

  function patchAction(act, name) {
    var template;
    switch (act) {
      case "start":     template = "/annieData/{device}/patch/{name}/start"; break;
      case "stop":      template = "/annieData/{device}/patch/{name}/stop"; break;
      case "info":      template = "/annieData/{device}/patch/{name}/info"; break;
      case "enableAll": template = "/annieData/{device}/patch/{name}/enableAll"; break;
      case "unsolo":    template = "/annieData/{device}/patch/{name}/unsolo"; break;
      case "save":      sendCmd(addr("/annieData/{device}/save/patch"), name); return;
      case "delete":    template = "/annieData/{device}/patch/{name}/delete"; break;
      default: return;
    }
    sendCmd(addr(template, name), null).then(function (res) {
      if (res.status === "ok") toast(act + ": " + name, "success");
    });
  }

  /* ═══════════════════════════════════════════
     ORI TABLE
     ═══════════════════════════════════════════ */

  function renderOriTable() {
    var dev = getActiveDev();
    var tbody = $("#oriTableBody");
    if (!tbody) return;
    tbody.innerHTML = "";
    if (!dev || Object.keys(dev.oris).length === 0) {
      tbody.innerHTML = '<tr><td colspan="6"><div class="empty-state"><div class="empty-icon">🧭</div><div class="empty-text">No orientations tracked yet.</div><div class="empty-sub">Save an orientation below or query the device.</div></div></td></tr>';
      return;
    }
    Object.keys(dev.oris).forEach(function (name) {
      var o = dev.oris[name];
      var typeBadge = o.type === "range"
        ? '<span class="ori-badge ori-badge-range">Range [R' + o.samples + ']</span>'
        : '<span class="ori-badge ori-badge-point">Point</span>';
      var colorHtml = "—";
      if (o.color) {
        var rgb = "rgb(" + o.color[0] + "," + o.color[1] + "," + o.color[2] + ")";
        colorHtml = '<span class="ori-color-dot" style="background:' + rgb + '" title="' + o.color.join(",") + '"></span>';
      }
      var activeHtml = o.active ? '<span class="ori-badge ori-badge-active">Active</span>' : "—";
      var tr = document.createElement("tr");
      tr.dataset.oriName = name;
      tr.innerHTML =
        '<td class="cell-name" data-label="Name">' + esc(name) + '</td>' +
        '<td data-label="Type">' + typeBadge + '</td>' +
        '<td class="cell-mono" data-label="Samples">' + o.samples + '</td>' +
        '<td data-label="Color">' + colorHtml + '</td>' +
        '<td data-label="Active">' + activeHtml + '</td>' +
        '<td class="cell-actions" data-label="Actions">' +
          '<button class="tbl-btn" data-act="info" title="Show details" aria-label="Info">ℹ️</button>' +
          '<button class="tbl-btn" data-act="reset" title="Reset range to point" aria-label="Reset">↺</button>' +
          '<button class="tbl-btn" data-act="select" title="Select for button editing" aria-label="Select">🎯</button>' +
          '<button class="tbl-btn tbl-btn-danger" data-act="delete" title="Delete ori" aria-label="Delete">🗑</button>' +
        '</td>';
      tr.querySelector(".cell-name").addEventListener("click", function () {
        /* Populate color form with this ori's name */
        var colorNameEl = $("#oriColorName");
        if (colorNameEl) colorNameEl.value = name;
        var oriNameEl = $("#oriName");
        if (oriNameEl) oriNameEl.value = name;
        if (o.color) {
          $("#oriColorR").value = o.color[0];
          $("#oriColorG").value = o.color[1];
          $("#oriColorB").value = o.color[2];
          updateOriColorPreview();
        }
      });
      tr.querySelectorAll(".tbl-btn").forEach(function (btn) {
        btn.addEventListener("click", function () {
          oriAction(btn.dataset.act, name);
        });
      });
      tbody.appendChild(tr);
    });
  }

  function oriAction(act, name) {
    switch (act) {
      case "info":
        sendCmd(addr("/annieData/{device}/ori/info/" + name), null);
        break;
      case "reset":
        sendCmd(addr("/annieData/{device}/ori/reset/" + name), null).then(function (res) {
          if (res.status === "ok") {
            toast("Reset: " + name, "success");
            if (getActiveDev() && getActiveDev().oris[name]) {
              getActiveDev().oris[name].type = "point";
              getActiveDev().oris[name].samples = 1;
              renderOriTable();
            }
          }
        });
        break;
      case "select":
        sendCmd(addr("/annieData/{device}/ori/select/" + name), null).then(function (res) {
          if (res.status === "ok") toast("Selected: " + name, "success");
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
    /* Parse: "name: samples=N center=[...] half_w=[...] (ACTIVE)" or
       "name: samples=1 point q=(...) euler=[...]" */
    var html = "";
    var nm = text.match(/^(\S+):/);
    if (nm) html += '<div class="ori-detail-row"><span class="ori-detail-label">Name</span><span>' + esc(nm[1]) + '</span></div>';
    var sm = text.match(/samples=(\d+)/);
    if (sm) html += '<div class="ori-detail-row"><span class="ori-detail-label">Samples</span><span>' + esc(sm[1]) + '</span></div>';
    var cm = text.match(/center=\[([^\]]+)\]/);
    if (cm) html += '<div class="ori-detail-row"><span class="ori-detail-label">Center</span><span class="cell-mono">[' + esc(cm[1]) + ']</span></div>';
    var hw = text.match(/half_w=\[([^\]]+)\]/);
    if (hw) html += '<div class="ori-detail-row"><span class="ori-detail-label">Half Width</span><span class="cell-mono">[' + esc(hw[1]) + ']</span></div>';
    var qm = text.match(/q=\(([^)]+)\)/);
    if (qm) html += '<div class="ori-detail-row"><span class="ori-detail-label">Quaternion</span><span class="cell-mono">(' + esc(qm[1]) + ')</span></div>';
    var em = text.match(/euler=\[([^\]]+)\]/);
    if (em) html += '<div class="ori-detail-row"><span class="ori-detail-label">Euler</span><span class="cell-mono">[' + esc(em[1]) + ']</span></div>';
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

    /* Patch name datalists */
    var patchNames = Object.keys(dev.patches);
    ["#patchNameList", "#patchNameList2", "#patchNameList3", "#patchNameList4", "#patchNameListSetAll"].forEach(function (sel) {
      var dl = $(sel);
      if (!dl) return;
      dl.innerHTML = "";
      patchNames.forEach(function (n) {
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
    msgCount++;
    rateCounter++;
    appendToFeed(entry);
    /* Auto-parse replies into registry */
    var prevMsgCount = 0, prevPatchCount = 0;
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
      prevPatchCount = Object.keys(preDev.patches || {}).length;
    }
    parseReplyIntoRegistry(entry);
    /* Show query feedback toast after list/all replies add new data */
    if (matchedDevId && devices[matchedDevId] && /\/list\/(all|msgs|messages)/i.test(entry.address || "")) {
      var postDev = devices[matchedDevId];
      var newMsgCount = Object.keys(postDev.messages || {}).length;
      var newPatchCount = Object.keys(postDev.patches || {}).length;
      if ((newMsgCount > 0 || newPatchCount > 0) && (newMsgCount !== prevMsgCount || newPatchCount !== prevPatchCount)) {
        showToast("Loaded " + newMsgCount + " message" + (newMsgCount !== 1 ? "s" : "") + " and " + newPatchCount + " patch" + (newPatchCount !== 1 ? "es" : "") + " from device.", "success");
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
    sendCmd(addr("/annieData/{device}/status/config"), cfg).then(function (res) {
      if (res.status === "ok") toast("Status config applied", "success");
    });
  });

  $("#btnStatusLevel").addEventListener("click", function () {
    var lvl = $("#statusLevel").value;
    if (!lvl) return;
    sendCmd(addr("/annieData/{device}/status/level"), lvl).then(function (res) {
      if (res.status === "ok") toast("Level set: " + lvl, "success");
    });
  });

  /* ═══════════════════════════════════════════
     MESSAGES
     ═══════════════════════════════════════════ */

  /* Config preview update */
  function updateMsgPreview() {
    var a = ($("#msgAdr") ? $("#msgAdr").value.trim() : "");
    var adrEl = $("#msgPreviewAdr");
    var cfgEl = $("#msgPreviewCfg");
    if (adrEl) adrEl.textContent = a ? "adr: " + a : "(no address)";
    var parts = [];
    var v = $("#msgValue").value; if (v) parts.push("value:" + v);
    var ip = $("#msgIP").value.trim(); if (ip) parts.push("ip:" + ip);
    var port = $("#msgPort").value; if (port) parts.push("port:" + port);
    var lo = $("#msgLow").value.trim(); if (lo) parts.push("low:" + lo);
    var hi = $("#msgHigh").value.trim(); if (hi) parts.push("high:" + hi);
    var pa = $("#msgPatch").value.trim(); if (pa) parts.push("patch:" + pa);
    var oo = $("#msgOriOnly").value.trim(); if (oo) parts.push("ori_only:" + oo);
    var on = $("#msgOriNot").value.trim(); if (on) parts.push("ori_not:" + on);
    var tn = $("#msgTernori").value.trim(); if (tn) parts.push("ternori:" + tn);
    if (cfgEl) cfgEl.textContent = parts.join(", ");
  }

  ["msgValue", "msgIP", "msgPort", "msgAdr", "msgLow", "msgHigh", "msgPatch", "msgOriOnly", "msgOriNot", "msgTernori"].forEach(function (id) {
    var el = $("#" + id);
    if (el) el.addEventListener("input", updateMsgPreview);
  });
  updateMsgPreview();

  /* Apply message (create/update) */
  $("#btnMsgApply").addEventListener("click", function () {
    var name = ($("#msgName").value || "").trim();
    if (!name) { toast("Message name required", "error"); return; }
    var parts = [];
    var a = ($("#msgAdr") ? $("#msgAdr").value.trim() : ""); if (a) parts.push("adr:" + a);
    var v = $("#msgValue").value; if (v) parts.push("value:" + v);
    var ip = $("#msgIP").value.trim(); if (ip) parts.push("ip:" + ip);
    var port = $("#msgPort").value; if (port) parts.push("port:" + port);
    var lo = $("#msgLow").value.trim(); if (lo) parts.push("low:" + lo);
    var hi = $("#msgHigh").value.trim(); if (hi) parts.push("high:" + hi);
    var pa = $("#msgPatch").value.trim(); if (pa) parts.push("patch:" + pa);
    var oo = $("#msgOriOnly").value.trim(); if (oo) parts.push("ori_only:" + oo);
    var on = $("#msgOriNot").value.trim(); if (on) parts.push("ori_not:" + on);
    var tn = $("#msgTernori").value.trim(); if (tn) parts.push("ternori:" + tn);
    var cfg = parts.join(", ");
    var address = addr("/annieData/{device}/msg/{name}", name);
    sendCmd(address, cfg || null).then(function (res) {
      if (res.status === "ok") {
        toast("Applied: " + name, "success");
        /* Update local registry */
        var dev = getActiveDev();
        if (dev) {
          dev.messages[name] = parseConfigString(cfg);
          renderMsgTable();
          refreshAllDropdowns();
        }
      }
    });
  });

  /* Clear form */
  $("#btnMsgClear").addEventListener("click", function () {
    ["msgName", "msgIP", "msgAdr", "msgLow", "msgHigh", "msgPatch", "msgOriOnly", "msgOriNot", "msgTernori"].forEach(function (id) {
      $("#" + id).value = "";
    });
    $("#msgValue").value = "";
    $("#msgPort").value = "9000";
    updateMsgPreview();
  });

  /* Clone / Rename */
  $("#btnMsgClone").addEventListener("click", function () {
    var src = ($("#msgSrcName").value || "").trim();
    var dest = ($("#msgDestName").value || "").trim();
    if (!src || !dest) { toast("Both names required", "error"); return; }
    sendCmd(addr("/annieData/{device}/clone/msg"), src + ", " + dest).then(function (res) {
      if (res.status === "ok") toast("Cloned: " + src + " → " + dest, "success");
    });
  });

  $("#btnMsgRename").addEventListener("click", function () {
    var src = ($("#msgSrcName").value || "").trim();
    var dest = ($("#msgDestName").value || "").trim();
    if (!src || !dest) { toast("Both names required", "error"); return; }
    sendCmd(addr("/annieData/{device}/rename/msg"), src + ", " + dest).then(function (res) {
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
     PATCHES
     ═══════════════════════════════════════════ */

  /* Apply patch config */
  $("#btnPatchApply").addEventListener("click", function () {
    var name = ($("#patchName").value || "").trim();
    if (!name) { toast("Patch name required", "error"); return; }
    var period = ($("#patchPeriod").value || "").trim();
    var mode = ($("#patchAdrMode").value || "").trim();
    var ip = ($("#patchIP").value || "").trim();
    var port = ($("#patchPort").value || "").trim();
    var patchAdr = ($("#patchAdr").value || "").trim();
    var low = ($("#patchLow").value || "").trim();
    var high = ($("#patchHigh").value || "").trim();

    /* Build override string from checkboxes */
    var ovParts = [];
    if ($("#ovIP").checked) ovParts.push("ip");
    if ($("#ovPort").checked) ovParts.push("port");
    if ($("#ovAdr").checked) ovParts.push("adr");
    if ($("#ovLow").checked) ovParts.push("low");
    if ($("#ovHigh").checked) ovParts.push("high");

    /* Build assign config and send primary patch config command */
    var cfgParts = [];
    if (ip) cfgParts.push("ip:" + ip);
    if (port) cfgParts.push("port:" + port);
    if (patchAdr) cfgParts.push("adr:" + patchAdr);
    if (low) cfgParts.push("low:" + low);
    if (high) cfgParts.push("high:" + high);
    var cfg = cfgParts.join(", ");

    /* Send commands for patch settings */
    var promises = [];
    promises.push(sendCmd(addr("/annieData/{device}/patch/{name}", name), cfg || null));
    // Wrap period in quotes so python-osc sends it as OSC string type 's'.
    // Without quotes, _coerce_arg converts "50" → int 50 → type 'i', and the
    // firmware's nextAsString() returns "" on an integer arg → period gets
    // clamped to 1ms regardless of the value sent.
    if (period) promises.push(sendCmd(addr("/annieData/{device}/patch/{name}/period", name), '"' + period + '"'));
    if (mode) promises.push(sendCmd(addr("/annieData/{device}/patch/{name}/adrMode", name), mode));
    promises.push(sendCmd(addr("/annieData/{device}/patch/{name}/override", name), ovParts.length ? ovParts.join(", ") : "none"));

    Promise.all(promises).then(function () {
      toast("Patch config applied: " + name, "success");
      var dev = getActiveDev();
      if (dev) {
        dev.patches[name] = Object.assign(dev.patches[name] || {}, {
          ip: ip, port: port, adr: patchAdr, low: low, high: high,
          period: period, adrMode: mode, override: ovParts.join(", ")
        });
        renderPatchTable();
        refreshAllDropdowns();
      }
    });
  });

  /* Start/Stop */
  $("#btnPatchStart").addEventListener("click", function () {
    var name = ($("#patchName").value || "").trim();
    if (!name) { toast("Patch name required", "error"); return; }
    sendCmd(addr("/annieData/{device}/patch/{name}/start", name), null).then(function (res) {
      if (res.status === "ok") toast("Started: " + name, "success");
    });
  });

  $("#btnPatchStop").addEventListener("click", function () {
    var name = ($("#patchName").value || "").trim();
    if (!name) { toast("Patch name required", "error"); return; }
    sendCmd(addr("/annieData/{device}/patch/{name}/stop", name), null).then(function (res) {
      if (res.status === "ok") toast("Stopped: " + name, "success");
    });
  });

  /* Add/Remove/Solo/Move messages */
  $("#btnPatchAddMsg").addEventListener("click", function () {
    var pname = ($("#patchMsgPatch").value || "").trim();
    var mnames = ($("#patchMsgNames").value || "").trim();
    if (!pname || !mnames) { toast("Patch and message name(s) required", "error"); return; }
    sendCmd(addr("/annieData/{device}/patch/{name}/addMsg", pname), mnames).then(function (res) {
      if (res.status === "ok") toast("Added to " + pname, "success");
    });
  });

  $("#btnPatchRemoveMsg").addEventListener("click", function () {
    var pname = ($("#patchMsgPatch").value || "").trim();
    var mnames = ($("#patchMsgNames").value || "").trim();
    if (!pname || !mnames) { toast("Patch and message name required", "error"); return; }
    sendCmd(addr("/annieData/{device}/patch/{name}/removeMsg", pname), mnames).then(function (res) {
      if (res.status === "ok") toast("Removed from " + pname, "success");
    });
  });

  $("#btnPatchSolo").addEventListener("click", function () {
    var pname = ($("#patchMsgPatch").value || "").trim();
    var mname = ($("#patchMsgNames").value || "").trim();
    if (!pname || !mname) { toast("Patch and message name required", "error"); return; }
    sendCmd(addr("/annieData/{device}/patch/{name}/solo", pname), mname).then(function (res) {
      if (res.status === "ok") toast("Solo: " + mname + " in " + pname, "success");
    });
  });

  $("#btnPatchMove").addEventListener("click", function () {
    var mname = ($("#patchMsgNames").value || "").trim();
    var pname = ($("#patchMsgPatch").value || "").trim();
    if (!pname || !mname) { toast("Message and patch name required", "error"); return; }
    sendCmd(addr("/annieData/{device}/move"), mname + ", " + pname).then(function (res) {
      if (res.status === "ok") toast("Moved: " + mname + " → " + pname, "success");
    });
  });

  $("#btnPatchSetAll").addEventListener("click", function () {
    var pname = ($("#patchSetAllPatch").value || "").trim();
    var cfg = ($("#patchSetAllCfg").value || "").trim();
    if (!pname || !cfg) { toast("Patch and config string required", "error"); return; }
    sendCmd(addr("/annieData/{device}/patch/{name}/setAll", pname), cfg).then(function (res) {
      if (res.status === "ok") toast("setAll applied: " + pname, "success");
    });
  });

  /* Clone / Rename */
  $("#btnPatchClone").addEventListener("click", function () {
    var src = ($("#patchSrcName").value || "").trim();
    var dest = ($("#patchDestName").value || "").trim();
    if (!src || !dest) { toast("Both names required", "error"); return; }
    sendCmd(addr("/annieData/{device}/clone/patch"), src + ", " + dest).then(function (res) {
      if (res.status === "ok") toast("Cloned: " + src + " → " + dest, "success");
    });
  });

  $("#btnPatchRename").addEventListener("click", function () {
    var src = ($("#patchSrcName").value || "").trim();
    var dest = ($("#patchDestName").value || "").trim();
    if (!src || !dest) { toast("Both names required", "error"); return; }
    sendCmd(addr("/annieData/{device}/rename/patch"), src + ", " + dest).then(function (res) {
      if (res.status === "ok") {
        toast("Renamed: " + src + " → " + dest, "success");
        var dev = getActiveDev();
        if (dev && dev.patches[src]) {
          dev.patches[dest] = dev.patches[src];
          delete dev.patches[src];
          renderPatchTable();
          refreshAllDropdowns();
        }
      }
    });
  });

  /* ═══════════════════════════════════════════
     DIRECT
     ═══════════════════════════════════════════ */

  function updateDirectPreview() {
    var parts = [];
    var v = $("#directValue").value; if (v) parts.push("value:" + v);
    var ip = $("#directIP").value.trim(); if (ip) parts.push("ip:" + ip);
    var port = $("#directPort").value; if (port) parts.push("port:" + port);
    var a = $("#directAdr").value.trim(); if (a) parts.push("adr:" + a);
    var lo = $("#directLow").value.trim(); if (lo) parts.push("low:" + lo);
    var hi = $("#directHigh").value.trim(); if (hi) parts.push("high:" + hi);
    var per = $("#directPeriod").value; if (per) parts.push("period:" + per);
    $("#directPreview").value = parts.join(", ");
  }

  ["directValue", "directIP", "directPort", "directAdr", "directLow", "directHigh", "directPeriod"].forEach(function (id) {
    var el = $("#" + id);
    if (el) el.addEventListener("input", updateDirectPreview);
  });
  updateDirectPreview();

  $("#btnDirectSend").addEventListener("click", function () {
    var name = ($("#directName").value || "").trim() || "quickSend";
    var cfg = ($("#directPreview").value || "").trim();
    if (!cfg) { toast("At least one config field required", "error"); return; }
    sendCmd(addr("/annieData/{device}/direct/{name}", name), cfg).then(function (res) {
      if (res.status === "ok") toast("Direct: " + name, "success");
    });
  });

  $("#btnDirectCopy").addEventListener("click", function () {
    var cfg = ($("#directPreview").value || "").trim();
    if (navigator.clipboard) {
      navigator.clipboard.writeText(cfg).then(function () { toast("Copied!", "success"); });
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
     PANEL STATE MANAGEMENT  (hidden / feed / reference)
     ═══════════════════════════════════════════ */

  var _panelState = "hidden"; // "hidden" | "feed" | "reference"

  function showPanel(view) {
    _panelState = view;
    var panel = $("#panelRight");
    if (view === "hidden") {
      panel.classList.add("panel-hidden");
      panel.classList.remove("ref-mode");
    } else if (view === "feed") {
      panel.classList.remove("panel-hidden");
      panel.classList.remove("ref-mode");
    } else if (view === "reference") {
      panel.classList.remove("panel-hidden");
      panel.classList.add("ref-mode");
    }
  }

  /* Feed toggle button */
  $("#btnFeedToggle").addEventListener("click", function () {
    if (_panelState === "feed") showPanel("hidden");
    else showPanel("feed");
  });

  /* Reference toggle button */
  $("#btnRefToggle").addEventListener("click", function () {
    if (_panelState === "reference") showPanel("hidden");
    else showPanel("reference");
  });

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
  showPanel("feed");

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
        renderPatchTable();
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

  /** Rebuild the header query-device <select> from current device list. */
  function refreshQueryDeviceSelect() {
    var sel = $("#queryDeviceSelect");
    var cur = sel.value;
    sel.innerHTML = '<option value="">all devices</option>';
    Object.keys(devices).forEach(function (id) {
      var d = devices[id];
      var opt = document.createElement("option");
      opt.value = id;
      opt.textContent = d.name;
      sel.appendChild(opt);
    });
    if (cur && devices[cur]) sel.value = cur;
  }

  /* ═══════════════════════════════════════════
     QUERY BUTTON  (header — always verbose)
     ═══════════════════════════════════════════ */

  $("#btnQueryDevice").addEventListener("click", function () {
    var selectedId = ($("#queryDeviceSelect").value || "").trim();
    if (selectedId) {
      /* Query one specific device */
      var d = devices[selectedId];
      if (!d) { toast("Device not found", "error"); return; }
      sendToDevice(selectedId, "/annieData/" + d.name + "/list/all", "verbose").then(function (res) {
        if (res.status === "ok") toast("Querying " + d.name + "…", "info");
      });
    } else {
      /* Query all configured devices */
      var ids = Object.keys(devices);
      if (!ids.length) { toast("No devices configured", "error"); return; }
      ids.forEach(function (id) {
        var d = devices[id];
        sendToDevice(id, "/annieData/" + d.name + "/list/all", "verbose");
      });
      toast("Querying " + ids.length + " device(s)…", "info");
    }
    showPanel("feed");
  });

  /* ═══════════════════════════════════════════
     AUTO QUERY  (float period; tracks active query in header)
     ═══════════════════════════════════════════ */

  var _autoQueryTimer = null;
  var _autoQueryDeviceId = ""; // "" = all devices

  /** Show a compact summary of the active auto-query in the header. */
  function updateAutoQueryList() {
    var container = $("#autoQueryList");
    if (!container) return;
    if (!_autoQueryTimer) { container.textContent = ""; return; }
    var period = parseFloat($("#autoQueryPeriod").value) || 5;
    var label = _autoQueryDeviceId && devices[_autoQueryDeviceId]
      ? devices[_autoQueryDeviceId].name
      : "all devices";
    container.textContent = "▶ " + label + " every " + period + "s (verbose)";
  }

  function startAutoQuery() {
    stopAutoQuery();
    var period = parseFloat($("#autoQueryPeriod").value) || 5;
    /* Clamp to at least 100 ms — prevents hammering the device on very
       small decimal inputs while still allowing sub-second periods. */
    var intervalMs = Math.max(100, period * 1000);
    _autoQueryDeviceId = ($("#queryDeviceSelect").value || "").trim();

    _autoQueryTimer = setInterval(function () {
      if (_autoQueryDeviceId) {
        var d = devices[_autoQueryDeviceId];
        if (!d) return;
        sendToDevice(_autoQueryDeviceId, "/annieData/" + d.name + "/list/all", "verbose");
      } else {
        Object.keys(devices).forEach(function (id) {
          var d = devices[id];
          if (!d) return;
          sendToDevice(id, "/annieData/" + d.name + "/list/all", "verbose");
        });
      }
    }, intervalMs);
    updateAutoQueryList();
  }

  function stopAutoQuery() {
    if (_autoQueryTimer) { clearInterval(_autoQueryTimer); _autoQueryTimer = null; }
    updateAutoQueryList();
  }

  $("#autoQueryEnabled").addEventListener("change", function () {
    if (this.checked) startAutoQuery(); else stopAutoQuery();
  });

  /* Restart auto-query when period or device changes */
  ["autoQueryPeriod", "queryDeviceSelect"].forEach(function (id) {
    var el = $("#" + id);
    if (el) el.addEventListener("change", function () {
      if ($("#autoQueryEnabled").checked) startAutoQuery();
    });
  });

  /* ═══════════════════════════════════════════
     REFERENCE — populate from presets
     ═══════════════════════════════════════════ */

  fetch("/api/presets/theater-gwd")
    .then(function (r) { return r.json(); })
    .then(function (data) {
      var presets = data.presets;
      if (!presets) return;

      /* Command list */
      var cmdContainer = $("#cmdList");
      var cmds = presets.commands || {};
      var cmdSearch = $("#cmdSearch");

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
      cmdSearch.addEventListener("input", function () { renderCmds(cmdSearch.value.trim().toLowerCase()); });

      /* Keywords */
      var kwContainer = $("#keywordList");
      var kws = presets.keywords || {};
      var kwSearch = $("#keywordSearch");

      function renderKWs(filter) {
        kwContainer.innerHTML = "";
        Object.keys(kws).sort().forEach(function (key) {
          if (filter && key.toLowerCase().indexOf(filter) === -1 && kws[key].toLowerCase().indexOf(filter) === -1) return;
          var div = document.createElement("div");
          div.className = "ref-item";
          div.innerHTML = '<span class="ref-term">' + esc(key) + '</span> <span class="ref-def">' + esc(kws[key]) + '</span>';
          kwContainer.appendChild(div);
        });
      }
      renderKWs("");
      kwSearch.addEventListener("input", function () { renderKWs(kwSearch.value.trim().toLowerCase()); });

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

  var CARD_ORDER_KEY = "gooey_card_order";

  function saveCardOrder() {
    var order = {};
    $$(".section").forEach(function (sec) {
      var ids = [];
      sec.querySelectorAll(".card[data-card-id]").forEach(function (c) {
        ids.push(c.dataset.cardId);
      });
      if (ids.length) order[sec.id] = ids;
    });
    try { localStorage.setItem(CARD_ORDER_KEY, JSON.stringify(order)); } catch (e) { /* ignore */ }
  }

  function restoreCardOrder() {
    var raw;
    try { raw = localStorage.getItem(CARD_ORDER_KEY); } catch (e) { return; }
    if (!raw) return;
    var order;
    try { order = JSON.parse(raw); } catch (e) { return; }
    Object.keys(order).forEach(function (secId) {
      var sec = $("#" + secId);
      if (!sec) return;
      var ids = order[secId];
      ids.forEach(function (id) {
        var card = sec.querySelector('.card[data-card-id="' + id + '"]');
        if (card) sec.appendChild(card);
      });
    });
  }

  function initDraggableCards() {
    var dragCard = null;

    $$(".card[data-card-id]").forEach(function (card) {
      card.setAttribute("draggable", "true");

      card.addEventListener("dragstart", function (e) {
        dragCard = card;
        card.classList.add("dragging");
        e.dataTransfer.effectAllowed = "move";
        e.dataTransfer.setData("text/plain", card.dataset.cardId);
      });

      card.addEventListener("dragend", function () {
        card.classList.remove("dragging");
        $$(".card.drag-over").forEach(function (c) { c.classList.remove("drag-over"); });
        dragCard = null;
      });

      card.addEventListener("dragover", function (e) {
        if (!dragCard || dragCard === card) return;
        if (dragCard.parentElement !== card.parentElement) return;
        e.preventDefault();
        e.dataTransfer.dropEffect = "move";
        card.classList.add("drag-over");
      });

      card.addEventListener("dragleave", function () {
        card.classList.remove("drag-over");
      });

      card.addEventListener("drop", function (e) {
        e.preventDefault();
        card.classList.remove("drag-over");
        if (!dragCard || dragCard === card) return;
        if (dragCard.parentElement !== card.parentElement) return;
        var parent = card.parentElement;
        var allCards = Array.from(parent.querySelectorAll(".card[data-card-id]"));
        var srcIdx = allCards.indexOf(dragCard);
        var tgtIdx = allCards.indexOf(card);
        if (srcIdx < tgtIdx) {
          parent.insertBefore(dragCard, card.nextSibling);
        } else {
          parent.insertBefore(dragCard, card);
        }
        saveCardOrder();
      });
    });
  }

  restoreCardOrder();
  initDraggableCards();

  /* ═══════════════════════════════════════════
     PATCH PREVIEW
     ═══════════════════════════════════════════ */

  function updatePatchPreview() {
    var name = ($("#patchName") ? $("#patchName").value.trim() : "");
    var adrEl = $("#patchPreviewAdr");
    var cfgEl = $("#patchPreviewCfg");
    if (adrEl) adrEl.textContent = name ? "patch: " + name : "(no patch name)";
    var parts = [];
    var ip = ($("#patchIP").value || "").trim(); if (ip) parts.push("ip:" + ip);
    var port = ($("#patchPort").value || "").trim(); if (port) parts.push("port:" + port);
    var patchAdr = ($("#patchAdr").value || "").trim(); if (patchAdr) parts.push("adr:" + patchAdr);
    var low = ($("#patchLow").value || "").trim(); if (low) parts.push("low:" + low);
    var high = ($("#patchHigh").value || "").trim(); if (high) parts.push("high:" + high);
    var period = ($("#patchPeriod").value || "").trim(); if (period) parts.push("period:" + period);
    var mode = ($("#patchAdrMode").value || "").trim(); if (mode) parts.push("adrMode:" + mode);
    var ovParts = [];
    if ($("#ovIP").checked) ovParts.push("ip");
    if ($("#ovPort").checked) ovParts.push("port");
    if ($("#ovAdr").checked) ovParts.push("adr");
    if ($("#ovLow").checked) ovParts.push("low");
    if ($("#ovHigh").checked) ovParts.push("high");
    if (ovParts.length > 0) parts.push("override:" + ovParts.join(", "));
    if (cfgEl) cfgEl.textContent = parts.join(", ");
  }

  ["patchName", "patchIP", "patchPort", "patchAdr", "patchLow", "patchHigh", "patchPeriod", "patchAdrMode"].forEach(function (id) {
    var el = $("#" + id);
    if (el) el.addEventListener("input", updatePatchPreview);
    if (el) el.addEventListener("change", updatePatchPreview);
  });
  ["ovIP", "ovPort", "ovAdr", "ovLow", "ovHigh"].forEach(function (id) {
    var el = $("#" + id);
    if (el) el.addEventListener("change", updatePatchPreview);
  });
  updatePatchPreview();

  /* ═══════════════════════════════════════════
     DEBUG MODE — confirm before send
     ═══════════════════════════════════════════ */

  var _origSendCmd = sendCmd;
  sendCmd = function (address, payload) {
    var dbg = $("#debugMode");
    if (dbg && dbg.checked) {
      return new Promise(function (resolve) {
        var msgText = "Address: " + address + "\nPayload: " + (payload || "(none)");
        showConfirm("DEBUG — Send OSC?", msgText, function () {
          resolve(_origSendCmd(address, payload));
        }, "Send", false);
        /* If cancelled, resolve with cancelled status */
        document.getElementById("confirmCancel").addEventListener("click", function () {
          resolve({ status: "cancelled" });
        }, { once: true });
      });
    }
    return _origSendCmd(address, payload);
  };

  var _origApi = api;
  api = function (endpoint, data, method) {
    if (endpoint === "send" && data) {
      var dbg = $("#debugMode");
      if (dbg && dbg.checked) {
        return new Promise(function (resolve) {
          var msgText = "Address: " + (data.address || "") + "\nArgs: " + JSON.stringify(data.args || "") + "\nHost: " + (data.host || "") + ":" + (data.port || "");
          showConfirm("DEBUG — Send OSC?", msgText, function () {
            resolve(_origApi(endpoint, data, method));
          }, "Send", false);
          document.getElementById("confirmCancel").addEventListener("click", function () {
            resolve({ status: "cancelled" });
          }, { once: true });
        });
      }
    }
    return _origApi(endpoint, data, method);
  };

  /* ═══════════════════════════════════════════
     STAR TAB LOGIC
     ═══════════════════════════════════════════ */

  var STAR_KEY = "gooey_starred_cards";

  function getStarred() {
    try {
      var raw = localStorage.getItem(STAR_KEY);
      return raw ? JSON.parse(raw) : [];
    } catch (e) { return []; }
  }

  function setStarred(arr) {
    try { localStorage.setItem(STAR_KEY, JSON.stringify(arr)); } catch (e) { /* ignore */ }
  }

  function isStarred(cardId) {
    return getStarred().indexOf(cardId) !== -1;
  }

  function toggleStar(cardId) {
    var starred = getStarred();
    var idx = starred.indexOf(cardId);
    if (idx !== -1) {
      starred.splice(idx, 1);
    } else {
      starred.push(cardId);
    }
    setStarred(starred);
    refreshStarIcons();
    renderStarredSection();
  }

  function refreshStarIcons() {
    $$(".card[data-card-id] .card-star").forEach(function (star) {
      var card = star.closest(".card");
      if (!card) return;
      var cardId = card.dataset.cardId;
      if (isStarred(cardId)) {
        star.textContent = "★";
        star.classList.add("starred");
      } else {
        star.textContent = "☆";
        star.classList.remove("starred");
      }
    });
  }

  function renderStarredSection() {
    var sec = $("#sec-starred");
    if (!sec) return;
    var starred = getStarred();
    sec.innerHTML = "";
    if (starred.length === 0) {
      sec.innerHTML = '<p class="hint-text" style="padding: 20px; text-align: center;">Star cards from any tab to pin them here. Click the ☆ icon in the top-right of any card.</p>';
      return;
    }
    starred.forEach(function (cardId) {
      var orig = document.querySelector('.card[data-card-id="' + cardId + '"]');
      if (!orig) return;
      var clone = orig.cloneNode(true);
      clone.removeAttribute("draggable");
      clone.classList.remove("dragging", "drag-over");
      /* Remove the star icon from clone to avoid duplication confusion */
      var cloneStar = clone.querySelector(".card-star");
      if (cloneStar) cloneStar.remove();
      /* Strip all id attributes from the clone and its descendants to
         prevent duplicate IDs in the DOM.  Without this, querySelector("#id")
         finds the clone (which appears earlier in the DOM) instead of the
         original, breaking handlers bound by ID. */
      clone.removeAttribute("id");
      clone.querySelectorAll("[id]").forEach(function (el) {
        el.removeAttribute("id");
      });
      sec.appendChild(clone);
    });
  }

  /* Add star icons to all cards with data-card-id */
  function addStarIcons() {
    $$(".card[data-card-id]").forEach(function (card) {
      if (card.querySelector(".card-star")) return;
      var star = document.createElement("span");
      star.className = "card-star";
      star.title = "Star this card";
      star.textContent = isStarred(card.dataset.cardId) ? "★" : "☆";
      if (isStarred(card.dataset.cardId)) star.classList.add("starred");
      star.addEventListener("click", function (e) {
        e.stopPropagation();
        toggleStar(card.dataset.cardId);
      });
      card.insertBefore(star, card.firstChild);
    });
  }

  addStarIcons();
  refreshStarIcons();
  renderStarredSection();

  /* ═══════════════════════════════════════════
     ORI CONTROLS
     ═══════════════════════════════════════════ */

  /* Ori mode toggle — controls ori column/field visibility in Messages */
  var oriModeEl = $("#oriMode");
  if (oriModeEl) {
    oriModeEl.addEventListener("change", function () {
      if (oriModeEl.checked) {
        document.body.classList.remove("ori-hidden");
      } else {
        document.body.classList.add("ori-hidden");
      }
    });
  }

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
      sendCmd(addr("/annieData/{device}/ori/delete"), name);
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
    btnOriStrict: function () {
      var checked = $("#oriStrict").checked;
      sendCmd(addr("/annieData/{device}/ori/strict"), checked ? "on" : "off");
    },
    btnOriColor: function () {
      var name = ($("#oriColorName").value || "").trim();
      if (!name) { toast("Ori name required", "error"); return; }
      var r = parseInt($("#oriColorR").value || "0", 10);
      var g = parseInt($("#oriColorG").value || "0", 10);
      var b = parseInt($("#oriColorB").value || "0", 10);
      sendCmd(addr("/annieData/{device}/ori/color/" + name), '"' + r + "," + g + "," + b + '"').then(function (res) {
        if (res.status === "ok") {
          toast("Color set: " + name, "success");
          var dev = getActiveDev();
          if (dev && dev.oris[name]) {
            dev.oris[name].color = [r, g, b];
            renderOriTable();
          }
        }
      });
    },
    btnOriSelect: function () {
      var name = ($("#oriColorName").value || "").trim();
      if (!name) { toast("Ori name required", "error"); return; }
      sendCmd(addr("/annieData/{device}/ori/select/" + name), null).then(function (res) {
        if (res.status === "ok") toast("Selected for buttons: " + name, "success");
      });
    }
  };

  Object.keys(oriButtons).forEach(function (id) {
    var el = $("#" + id);
    if (el) {
      el.addEventListener("click", function () {
        oriButtons[id]();
      });
    }
  });

  /* ── Ori color picker sync ── */
  function updateOriColorPreview() {
    var r = parseInt($("#oriColorR").value || "0", 10);
    var g = parseInt($("#oriColorG").value || "0", 10);
    var b = parseInt($("#oriColorB").value || "0", 10);
    var hex = "#" + ((1 << 24) + (r << 16) + (g << 8) + b).toString(16).slice(1);
    var picker = $("#oriColorPicker");
    if (picker) picker.value = hex;
    var preview = $("#oriColorPreview");
    if (preview) preview.style.background = hex;
  }

  var colorPicker = $("#oriColorPicker");
  if (colorPicker) {
    colorPicker.addEventListener("input", function () {
      var hex = colorPicker.value;
      var r = parseInt(hex.substr(1, 2), 16);
      var g = parseInt(hex.substr(3, 2), 16);
      var b = parseInt(hex.substr(5, 2), 16);
      $("#oriColorR").value = r;
      $("#oriColorG").value = g;
      $("#oriColorB").value = b;
      var preview = $("#oriColorPreview");
      if (preview) preview.style.background = hex;
    });
  }

  ["oriColorR", "oriColorG", "oriColorB"].forEach(function (id) {
    var el = $("#" + id);
    if (el) el.addEventListener("input", updateOriColorPreview);
  });

  /* ═══════════════════════════════════════════
     INLINE FIELD VALIDATION
     ═══════════════════════════════════════════ */

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

  /* Port validation */
  ["statusPort", "msgPort", "patchPort", "directPort", "rawPort", "bridgeInPort", "bridgeOutPort", "replyPort"].forEach(function (fieldId) {
    var el = $("#" + fieldId);
    if (!el) return;
    el.addEventListener("blur", function () {
      var v = parseInt(el.value, 10);
      validateField(el, v >= 1 && v <= 65535, "Port must be 1–65535");
    });
  });

  /* IP validation */
  ["statusIP", "msgIP", "patchIP", "directIP", "rawHost", "bridgeOutHost"].forEach(function (fieldId) {
    var el = $("#" + fieldId);
    if (!el) return;
    el.addEventListener("blur", function () {
      var v = el.value.trim();
      if (!v) return; /* allow empty */
      validateField(el, /^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}$/.test(v), "Enter a valid IP address");
    });
  });

  /* Name validation */
  ["msgName", "patchName"].forEach(function (fieldId) {
    var el = $("#" + fieldId);
    if (!el) return;
    el.addEventListener("blur", function () {
      var v = el.value.trim();
      if (!v) validateField(el, false, "Name is required");
      else validateField(el, true, "");
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
    Object.keys(prefs).forEach(function (col) {
      var visible = prefs[col];
      /* Toggle header th */
      $$('[data-col="' + col + '"]').forEach(function (el) {
        el.style.display = visible ? "" : "none";
      });
    });
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
     ONBOARDING BANNER
     ═══════════════════════════════════════════ */

  var ONBOARD_DISMISSED_KEY = "gooey_onboard_dismissed";

  function updateOnboarding() {
    var banner = $("#onboardBanner");
    if (!banner) return;
    /* If permanently dismissed, keep hidden */
    try { if (localStorage.getItem(ONBOARD_DISMISSED_KEY)) { banner.classList.add("hidden"); return; } } catch (e) {}

    var devCount = Object.keys(devices).length;
    var hasMsgs = false;
    var hasPatches = false;
    Object.keys(devices).forEach(function (id) {
      var d = devices[id];
      if (Object.keys(d.messages || {}).length > 0) hasMsgs = true;
      if (Object.keys(d.patches || {}).length > 0) hasPatches = true;
    });

    /* Show banner if incomplete */
    if (!hasPatches) {
      banner.classList.remove("hidden");
    } else {
      banner.classList.add("hidden");
    }

    /* Mark completed steps */
    var s1 = $("#onboard1"), s2 = $("#onboard2"), s3 = $("#onboard3"), s4 = $("#onboard4");
    if (s1) { if (devCount > 0) s1.classList.add("done"); else s1.classList.remove("done"); }
    if (s2) { if (hasMsgs || hasPatches) s2.classList.add("done"); else s2.classList.remove("done"); }
    if (s3) { if (hasMsgs) s3.classList.add("done"); else s3.classList.remove("done"); }
    if (s4) { if (hasPatches) s4.classList.add("done"); else s4.classList.remove("done"); }
  }

  var onboardDismiss = $("#onboardDismiss");
  if (onboardDismiss) {
    onboardDismiss.addEventListener("click", function () {
      try { localStorage.setItem(ONBOARD_DISMISSED_KEY, "1"); } catch (e) {}
      var banner = $("#onboardBanner");
      if (banner) banner.classList.add("hidden");
    });
  }

  /* Initial onboarding check */
  updateOnboarding();

  /* ═══════════════════════════════════════════
     QUICK REF CARD dismiss + open panel link
     ═══════════════════════════════════════════ */

  var QR_DISMISSED_KEY = "gooey_qr_dismissed";
  var quickRefCard = $("#quickRefCard");
  if (quickRefCard) {
    try { if (localStorage.getItem(QR_DISMISSED_KEY)) quickRefCard.style.display = "none"; } catch (e) {}
    var qrDismiss = $("#quickRefDismiss");
    if (qrDismiss) {
      qrDismiss.addEventListener("click", function () {
        quickRefCard.style.display = "none";
        try { localStorage.setItem(QR_DISMISSED_KEY, "1"); } catch (e) {}
      });
    }
    var openRefPanel = $("#openRefPanel");
    if (openRefPanel) {
      openRefPanel.addEventListener("click", function (e) {
        e.preventDefault();
        showPanel("reference");
      });
    }
  }

  /* ═══════════════════════════════════════════
     REFERENCE PANEL — open by default first visit
     ═══════════════════════════════════════════ */

  var REF_SEEN_KEY = "gooey_refPanelSeen";
  try {
    if (!localStorage.getItem(REF_SEEN_KEY)) {
      /* First visit: show reference panel instead of feed */
      showPanel("reference");
      localStorage.setItem(REF_SEEN_KEY, "1");
    }
  } catch (e) {}

})();
