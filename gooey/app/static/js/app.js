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

  /* ── Toast ── */
  function toast(msg, type) {
    type = type || "info";
    var el = document.createElement("div");
    el.className = "toast toast-" + type;
    el.textContent = msg;
    $("#toastContainer").appendChild(el);
    setTimeout(function () {
      el.style.opacity = "0";
      el.style.transform = "translateX(20px)";
      el.style.transition = "all 0.2s ease-out";
      setTimeout(function () { el.remove(); }, 250);
    }, 3000);
  }

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
      devices[id] = { host: host, port: parseInt(port, 10), name: name, messages: {}, patches: {} };
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
    refreshAllDropdowns();
  }

  function getActiveDev() {
    return devices[activeDeviceId] || null;
  }

  function devHost() { var d = getActiveDev(); return d ? d.host : "127.0.0.1"; }
  function devPort() { var d = getActiveDev(); return d ? d.port : 8000; }
  function devName() { var d = getActiveDev(); return d ? d.name : "bart"; }

  /* ── Device tab rendering ── */
  function renderDeviceTabs() {
    var strip = $("#deviceStrip");
    var addBtn = $("#btnAddDevice");
    /* remove existing tabs */
    $$(".device-tab").forEach(function (t) { t.remove(); });
    Object.keys(devices).forEach(function (id) {
      var d = devices[id];
      var tab = document.createElement("button");
      tab.className = "device-tab" + (id === activeDeviceId ? " active" : "");
      tab.dataset.deviceId = id;
      tab.innerHTML = '<span class="dev-dot"></span>' + d.name + " <span class='dev-remove' title='Remove'>✕</span>";
      tab.addEventListener("click", function (e) {
        if (e.target.classList.contains("dev-remove")) {
          removeDevice(id);
          return;
        }
        activeDeviceId = id;
        renderDeviceTabs();
        renderMsgTable();
        renderPatchTable();
        refreshAllDropdowns();
      });
      strip.insertBefore(tab, addBtn);
    });
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
  }

  /* ── Add-device button ── */
  $("#btnAddDevice").addEventListener("click", function () {
    var host = prompt("Device IP address:", "192.168.1.100");
    if (!host) return;
    var port = prompt("Device port:", "8000");
    if (!port) return;
    var name = prompt("Device name (unique identifier):", "bart");
    if (!name) return;
    addDevice(host.trim(), parseInt(port, 10), name.trim());
    renderMsgTable();
    renderPatchTable();
    toast("Device added: " + name, "success");
  });

  /* Initialize with one default device */
  addDevice("127.0.0.1", 8000, "bart");

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

    /* ── Parse list replies ── */
    var listMatch = text.match(/list\/(?:msgs|patches|all):\s*(.+)/i);
    if (listMatch) {
      var names = listMatch[1].split(/[,\s]+/).map(function (s) { return s.trim(); }).filter(Boolean);
      var isMsgList = text.match(/list\/msgs/i) || text.match(/list\/all/i);
      var isPatchList = text.match(/list\/patches/i) || text.match(/list\/all/i);
      names.forEach(function (n) {
        if (isMsgList && !dev.messages[n]) {
          dev.messages[n] = {};
        }
        if (isPatchList && !dev.patches[n]) {
          dev.patches[n] = {};
        }
      });
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

    /* ── Parse verbose list lines (key:val pairs with a leading name) ── */
    var verboseMatch = text.match(/^\[(?:INFO|DEBUG)\]\s+(\S+)\s*[:=]\s*(.*)/i);
    if (verboseMatch) {
      var vName = verboseMatch[1];
      var vRest = verboseMatch[2];
      if (vRest.indexOf("value:") !== -1 || vRest.indexOf("ip:") !== -1 || vRest.indexOf("adr:") !== -1) {
        var vParams = parseConfigString(vRest);
        dev.messages[vName] = Object.assign(dev.messages[vName] || {}, vParams);
        renderMsgTable();
        refreshAllDropdowns();
      } else if (vRest.indexOf("period:") !== -1 || vRest.indexOf("adrMode:") !== -1 || vRest.indexOf("msgs:") !== -1) {
        var vpParams = parseConfigString(vRest);
        dev.patches[vName] = Object.assign(dev.patches[vName] || {}, vpParams);
        renderPatchTable();
        refreshAllDropdowns();
      }
    }
  }

  /** Parse a config string like "value:accelX ip:192.168.1.50 port:9000" into an object. */
  function parseConfigString(str) {
    var result = {};
    var parts = str.split(/[\s,]+/);
    parts.forEach(function (p) {
      var idx = p.indexOf(":");
      if (idx > 0) {
        result[p.substring(0, idx).trim()] = p.substring(idx + 1).trim();
      }
    });
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
      tbody.innerHTML = '<tr><td colspan="10" class="empty-state">No messages tracked. Query the device or add one below.</td></tr>';
      return;
    }
    Object.keys(dev.messages).forEach(function (name) {
      var m = dev.messages[name];
      var tr = document.createElement("tr");
      tr.dataset.msgName = name;
      tr.innerHTML =
        '<td class="cell-name">' + esc(name) + '</td>' +
        '<td class="cell-mono">' + esc(m.value || "") + '</td>' +
        '<td class="cell-mono">' + esc(m.ip || "") + '</td>' +
        '<td class="cell-mono">' + esc(m.port || "") + '</td>' +
        '<td class="cell-mono">' + esc(m.adr || m.addr || m.address || "") + '</td>' +
        '<td class="cell-mono">' + esc(m.low || m.min || "") + '</td>' +
        '<td class="cell-mono">' + esc(m.high || m.max || "") + '</td>' +
        '<td class="cell-mono">' + esc(m.patch || "") + '</td>' +
        '<td>' + (m.enabled === "false" ? "❌" : "✅") + '</td>' +
        '<td class="cell-actions">' +
          '<button class="tbl-btn" data-act="info">ℹ️</button>' +
          '<button class="tbl-btn tbl-btn-success" data-act="enable">✅</button>' +
          '<button class="tbl-btn" data-act="disable">🔇</button>' +
          '<button class="tbl-btn" data-act="save">💾</button>' +
          '<button class="tbl-btn tbl-btn-danger" data-act="delete">🗑</button>' +
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
  }

  function populateMsgForm(name, m) {
    $("#msgName").value = name;
    $("#msgValue").value = m.value || "";
    $("#msgIP").value = m.ip || "";
    $("#msgPort").value = m.port || "9000";
    $("#msgAdr").value = m.adr || m.addr || m.address || "";
    $("#msgLow").value = m.low || m.min || "";
    $("#msgHigh").value = m.high || m.max || "";
    $("#msgPatch").value = m.patch || "";
    updateMsgPreview();
    /* scroll to form */
    $$(".nav-btn")[1].click(); // switch to messages tab
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
      tbody.innerHTML = '<tr><td colspan="6" class="empty-state">No patches tracked. Query the device or create one below.</td></tr>';
      return;
    }
    Object.keys(dev.patches).forEach(function (name) {
      var p = dev.patches[name];
      var msgsStr = p.msgs || "";
      if (typeof msgsStr === "string") msgsStr = msgsStr.replace(/\+/g, ", ");
      var tr = document.createElement("tr");
      tr.dataset.patchName = name;
      tr.innerHTML =
        '<td class="cell-name">' + esc(name) + '</td>' +
        '<td class="cell-mono">' + esc(p.period || "50") + ' ms</td>' +
        '<td class="cell-mono">' + esc(p.adrMode || p.adrmode || "fallback") + '</td>' +
        '<td class="cell-mono">' + esc(p.override || "—") + '</td>' +
        '<td class="cell-mono" style="max-width:140px;overflow:hidden;text-overflow:ellipsis" title="' + esc(msgsStr) + '">' + esc(msgsStr || "—") + '</td>' +
        '<td class="cell-actions">' +
          '<button class="tbl-btn tbl-btn-success" data-act="start">▶</button>' +
          '<button class="tbl-btn tbl-btn-stop" data-act="stop">⏹</button>' +
          '<button class="tbl-btn" data-act="info">ℹ️</button>' +
          '<button class="tbl-btn" data-act="enableAll">✅</button>' +
          '<button class="tbl-btn" data-act="unsolo">🔊</button>' +
          '<button class="tbl-btn" data-act="save">💾</button>' +
          '<button class="tbl-btn tbl-btn-danger" data-act="delete">🗑</button>' +
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
    $("#patchAdrMode").value = p.adrMode || p.adrmode || "fallback";
    var ov = (p.override || "").split("+");
    $("#ovIP").checked = ov.indexOf("ip") !== -1;
    $("#ovPort").checked = ov.indexOf("port") !== -1;
    $("#ovAdr").checked = ov.indexOf("adr") !== -1;
    $("#ovLow").checked = ov.indexOf("low") !== -1;
    $("#ovHigh").checked = ov.indexOf("high") !== -1;
    $$(".nav-btn")[2].click(); // patches tab
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
    ["#patchNameList", "#patchNameList2", "#patchNameList3", "#patchNameList4"].forEach(function (sel) {
      var dl = $(sel);
      if (!dl) return;
      dl.innerHTML = "";
      patchNames.forEach(function (n) {
        var o = document.createElement("option");
        o.value = n;
        dl.appendChild(o);
      });
    });
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
    parseReplyIntoRegistry(entry);
  });

  /* ═══════════════════════════════════════════
     DASHBOARD
     ═══════════════════════════════════════════ */

  $$(".qbtn[data-cmd]").forEach(function (btn) {
    btn.addEventListener("click", function () {
      var confirmMsg = btn.dataset.confirm;
      if (confirmMsg && !window.confirm(confirmMsg)) return;
      var cmd = btn.dataset.cmd;
      var template = CMD_ADDRESSES[cmd];
      if (!template) return;
      var address = addr(template);
      var payload = btn.dataset.payload || null;
      sendCmd(address, payload).then(function (res) {
        if (res.status === "ok") toast("Sent: " + cmd, "success");
      });
    });
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
    var parts = [];
    var v = $("#msgValue").value;   if (v) parts.push("value:" + v);
    var ip = $("#msgIP").value.trim(); if (ip) parts.push("ip:" + ip);
    var port = $("#msgPort").value; if (port) parts.push("port:" + port);
    var a = $("#msgAdr").value.trim(); if (a) parts.push("adr:" + a);
    var lo = $("#msgLow").value.trim(); if (lo) parts.push("low:" + lo);
    var hi = $("#msgHigh").value.trim(); if (hi) parts.push("high:" + hi);
    var pa = $("#msgPatch").value.trim(); if (pa) parts.push("patch:" + pa);
    $("#msgPreview").value = parts.join(", ");
  }

  ["msgValue", "msgIP", "msgPort", "msgAdr", "msgLow", "msgHigh", "msgPatch"].forEach(function (id) {
    var el = $("#" + id);
    if (el) el.addEventListener("input", updateMsgPreview);
  });
  updateMsgPreview();

  /* Apply message (create/update) */
  $("#btnMsgApply").addEventListener("click", function () {
    var name = ($("#msgName").value || "").trim();
    if (!name) { toast("Message name required", "error"); return; }
    var cfg = ($("#msgPreview").value || "").trim();
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
    ["msgName", "msgIP", "msgAdr", "msgLow", "msgHigh", "msgPatch"].forEach(function (id) {
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

    /* Build override string from checkboxes */
    var ovParts = [];
    if ($("#ovIP").checked) ovParts.push("ip");
    if ($("#ovPort").checked) ovParts.push("port");
    if ($("#ovAdr").checked) ovParts.push("adr");
    if ($("#ovLow").checked) ovParts.push("low");
    if ($("#ovHigh").checked) ovParts.push("high");

    /* Send individual commands for each setting */
    var promises = [];
    if (period) {
      promises.push(sendCmd(addr("/annieData/{device}/patch/{name}/period", name), period));
    }
    if (mode) {
      promises.push(sendCmd(addr("/annieData/{device}/patch/{name}/adrMode", name), mode));
    }
    if (ovParts.length > 0) {
      promises.push(sendCmd(addr("/annieData/{device}/patch/{name}/override", name), ovParts.join("+")));
    }
    if (promises.length === 0) {
      /* Just create the patch */
      sendCmd(addr("/annieData/{device}/patch/{name}", name), null).then(function (res) {
        if (res.status === "ok") toast("Created patch: " + name, "success");
      });
    } else {
      Promise.all(promises).then(function () {
        toast("Patch config applied: " + name, "success");
        var dev = getActiveDev();
        if (dev) {
          dev.patches[name] = Object.assign(dev.patches[name] || {}, {
            period: period, adrMode: mode, override: ovParts.join("+")
          });
          renderPatchTable();
          refreshAllDropdowns();
        }
      });
    }
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
     LISTEN (receive replies)
     ═══════════════════════════════════════════ */

  var isListening = false;

  $("#btnReplyListen").addEventListener("click", function () {
    var port = parseInt($("#replyPort").value, 10) || 9000;
    if (isListening) {
      api("recv/stop", { id: "reply-listener" }).then(function () {
        isListening = false;
        $("#btnReplyListen").classList.remove("active");
        $("#listenDot").classList.remove("on");
        toast("Stopped listening", "info");
      });
    } else {
      api("recv/start", { port: port, id: "reply-listener" }).then(function (res) {
        if (res.status === "ok") {
          isListening = true;
          $("#btnReplyListen").classList.add("active");
          $("#listenDot").classList.add("on");
          toast("Listening on port " + port, "success");
        }
      });
    }
  });

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

})();
