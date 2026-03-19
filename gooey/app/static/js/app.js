/* ==============================================
   TheaterGWD Control Center — Frontend
   ============================================== */

(function () {
  "use strict";

  const MAX_LOG_ENTRIES = 500;

  // ---- Socket.IO ----
  const socket = io({ transports: ["websocket", "polling"] });

  // ---- DOM helpers ----
  const $ = (sel) => document.querySelector(sel);
  const $$ = (sel) => document.querySelectorAll(sel);

  // ---- Connection status ----
  socket.on("connect", () => {
    $("#wsDot").className = "ws-dot connected";
    $("#wsLabel").textContent = "Connected";
  });

  socket.on("disconnect", () => {
    $("#wsDot").className = "ws-dot error";
    $("#wsLabel").textContent = "Disconnected";
  });

  // ---- Section navigation ----
  $$(".nav-btn").forEach((btn) => {
    btn.addEventListener("click", () => {
      $$(".nav-btn").forEach((b) => b.classList.remove("active"));
      $$(".section").forEach((s) => s.classList.remove("active"));
      btn.classList.add("active");
      const sec = $(`#sec-${btn.dataset.section}`);
      if (sec) sec.classList.add("active");
    });
  });

  // ---- Toast ----
  function toast(msg, type) {
    type = type || "info";
    const container = $("#toastContainer");
    const el = document.createElement("div");
    el.className = "toast toast-" + type;
    el.textContent = msg;
    container.appendChild(el);
    setTimeout(() => {
      el.style.opacity = "0";
      el.style.transform = "translateX(20px)";
      el.style.transition = "all 0.2s ease-out";
      setTimeout(() => el.remove(), 250);
    }, 3000);
  }

  // ---- API helper ----
  function api(endpoint, data) {
    return fetch("/api/" + endpoint, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(data),
    })
      .then((r) => r.json())
      .then((res) => {
        if (res.status === "error") toast(res.message, "error");
        return res;
      })
      .catch((err) => {
        toast("Request failed: " + err.message, "error");
        return { status: "error", message: err.message };
      });
  }

  // ---- Device connection helpers ----
  function devHost() { return $("#deviceHost").value.trim() || "127.0.0.1"; }
  function devPort() { return parseInt($("#devicePort").value, 10) || 8000; }
  function devName() { return $("#deviceName").value.trim() || "bart"; }

  /**
   * Send a TheaterGWD command.
   * @param {string} address - OSC address (with {device}/{name} placeholders resolved)
   * @param {string|null} payload - Config string payload (sent as single string arg)
   */
  function sendCmd(address, payload) {
    var data = {
      host: devHost(),
      port: devPort(),
      address: address,
    };
    // CRITICAL FIX: Send config-string payloads as a single-element array
    // so the backend does NOT split them by spaces. TheaterGWD expects one
    // string argument for config strings like "value:accelX, ip:192.168.1.50".
    if (payload) {
      data.args = [payload];
    }
    return api("send", data);
  }

  /** Build an address from a template, substituting {device} and {name}. */
  function addr(template, name) {
    var a = template.replace("{device}", devName());
    if (name !== undefined) a = a.replace("{name}", name);
    return a;
  }

  // ---- Log rendering ----
  function renderLogEntry(entry) {
    var div = document.createElement("div");
    div.className = "log-entry";

    var tagClass = { send: "log-tag-send", recv: "log-tag-recv", bridge: "log-tag-bridge" };

    var argsStr = (entry.args || [])
      .map(function (a) {
        if (a.type === "s") return '"' + a.value + '"';
        return a.value;
      })
      .join(" ");

    var destInfo = "";
    if (entry.dest) destInfo = " → " + entry.dest;
    if (entry.source && entry.dest) destInfo = " " + entry.source + " → " + entry.dest;
    else if (entry.source) destInfo = " ← " + entry.source;

    div.innerHTML = [
      '<span class="log-time">' + entry.time + "</span>",
      '<span class="log-tag ' + (tagClass[entry.direction] || "") + '">' + entry.direction + "</span>",
      '<span class="log-address">' + entry.address + "</span>",
      argsStr ? '<span class="log-args">(' + argsStr + ")</span>" : "",
      destInfo ? '<span class="log-dest">' + destInfo + "</span>" : "",
    ].join("");

    return div;
  }

  function appendToFeed(entry) {
    var feedEl = $("#feedLog");
    var filterText = ($("#feedFilter").value || "").trim().toLowerCase();
    if (filterText && !entry.address.toLowerCase().includes(filterText)) return;
    feedEl.appendChild(renderLogEntry(entry));
    while (feedEl.children.length > MAX_LOG_ENTRIES) {
      feedEl.removeChild(feedEl.firstChild);
    }
    if ($("#feedAutoScroll").checked) {
      feedEl.scrollTop = feedEl.scrollHeight;
    }
  }

  // ---- Message counter ----
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

  // ---- Real-time messages ----
  socket.on("osc_message", function (entry) {
    msgCount++;
    rateCounter++;
    appendToFeed(entry);
  });

  // ==================================================================
  //  DASHBOARD
  // ==================================================================

  // Quick command buttons (data-cmd)
  var CMD_ADDRESSES = {
    blackout: "/annieData/{device}/blackout",
    restore: "/annieData/{device}/restore",
    save: "/annieData/{device}/save",
    load: "/annieData/{device}/load",
    nvs_clear: "/annieData/{device}/nvs/clear",
    list_messages: "/annieData/{device}/list/msgs",
    list_patches: "/annieData/{device}/list/patches",
    list_all: "/annieData/{device}/list/all",
  };

  $$(".qbtn[data-cmd]").forEach(function (btn) {
    btn.addEventListener("click", function () {
      var cmd = btn.dataset.cmd;
      var template = CMD_ADDRESSES[cmd];
      if (!template) return;
      var address = addr(template);
      var payload = btn.dataset.payload || null;
      sendCmd(address, payload).then(function (res) {
        if (res.status === "ok") toast("Sent: " + address, "success");
      });
    });
  });

  // Status config
  $("#btnStatusConfig").addEventListener("click", function () {
    var ip = $("#statusIP").value.trim();
    var port = $("#statusPort").value.trim();
    var adr = $("#statusAdr").value.trim();
    if (!ip) { toast("Enter your IP address", "error"); return; }
    var parts = [];
    if (ip) parts.push("ip:" + ip);
    if (port) parts.push("port:" + port);
    if (adr) parts.push("adr:" + adr);
    var payload = parts.join(", ");
    var address = addr("/annieData/{device}/status/config");
    sendCmd(address, payload).then(function (res) {
      if (res.status === "ok") toast("Status config sent", "success");
    });
  });

  $("#btnStatusLevel").addEventListener("click", function () {
    var level = $("#statusLevel").value;
    if (!level) { toast("Select a level first", "error"); return; }
    var address = addr("/annieData/{device}/status/level");
    sendCmd(address, level).then(function (res) {
      if (res.status === "ok") toast("Status level set to " + level, "success");
    });
  });

  // ==================================================================
  //  MESSAGES
  // ==================================================================

  // Config preview builder for message creation
  function updateMsgPreview() {
    var parts = [];
    var val = ($("#msgValue") || {}).value || "";
    var ip = ($("#msgIP") || {}).value || "";
    var port = ($("#msgPort") || {}).value || "";
    var adrVal = ($("#msgAdr") || {}).value || "";
    var low = (($("#msgLow") || {}).value || "").trim();
    var high = (($("#msgHigh") || {}).value || "").trim();
    var patch = (($("#msgPatch") || {}).value || "").trim();
    if (val) parts.push("value:" + val);
    if (ip) parts.push("ip:" + ip);
    if (port) parts.push("port:" + port);
    if (adrVal) parts.push("adr:" + adrVal);
    if (low) parts.push("low:" + low);
    if (high) parts.push("high:" + high);
    if (patch) parts.push("patch:" + patch);
    var el = $("#msgPreview");
    if (el) el.value = parts.join(", ");
  }

  ["msgValue", "msgIP", "msgPort", "msgAdr", "msgLow", "msgHigh", "msgPatch"].forEach(function (id) {
    var el = $("#" + id);
    if (el) {
      el.addEventListener("input", updateMsgPreview);
      el.addEventListener("change", updateMsgPreview);
    }
  });
  updateMsgPreview();

  // Create message
  $("#btnMsgCreate").addEventListener("click", function () {
    var name = ($("#msgName").value || "").trim();
    if (!name) { toast("Enter a message name", "error"); return; }
    var payload = ($("#msgPreview").value || "").trim();
    if (!payload) { toast("Configure at least one field", "error"); return; }
    var address = addr("/annieData/{device}/msg/{name}", name);
    sendCmd(address, payload).then(function (res) {
      if (res.status === "ok") toast("Created message: " + name, "success");
    });
  });

  // Message actions (info, enable, disable, delete, save)
  $$(".qbtn[data-msg-action]").forEach(function (btn) {
    btn.addEventListener("click", function () {
      var action = btn.dataset.msgAction;
      var name = ($("#msgActionName").value || "").trim();
      if (!name) { toast("Enter a message name", "error"); return; }

      var address, payload = null;
      if (action === "save") {
        address = addr("/annieData/{device}/save/msg");
        payload = name;
      } else {
        address = addr("/annieData/{device}/msg/{name}/" + action, name);
      }
      sendCmd(address, payload).then(function (res) {
        if (res.status === "ok") toast(action + ": " + name, "success");
      });
    });
  });

  // Clone / Rename message
  $("#btnMsgClone").addEventListener("click", function () {
    var src = ($("#msgSrcName").value || "").trim();
    var dest = ($("#msgDestName").value || "").trim();
    if (!src || !dest) { toast("Enter both source and destination names", "error"); return; }
    var address = addr("/annieData/{device}/clone/msg");
    sendCmd(address, src + ", " + dest).then(function (res) {
      if (res.status === "ok") toast("Cloned: " + src + " → " + dest, "success");
    });
  });

  $("#btnMsgRename").addEventListener("click", function () {
    var src = ($("#msgSrcName").value || "").trim();
    var dest = ($("#msgDestName").value || "").trim();
    if (!src || !dest) { toast("Enter both old and new names", "error"); return; }
    var address = addr("/annieData/{device}/rename/msg");
    sendCmd(address, src + ", " + dest).then(function (res) {
      if (res.status === "ok") toast("Renamed: " + src + " → " + dest, "success");
    });
  });

  // ==================================================================
  //  PATCHES
  // ==================================================================

  function getPatchName() {
    return ($("#patchName").value || "").trim();
  }

  // Patch quick actions
  $$(".qbtn[data-patch-action]").forEach(function (btn) {
    btn.addEventListener("click", function () {
      var action = btn.dataset.patchAction;
      var name = getPatchName();
      if (!name) { toast("Enter a patch name", "error"); return; }

      var address, payload = null;
      if (action === "save") {
        address = addr("/annieData/{device}/save/patch");
        payload = name;
      } else {
        address = addr("/annieData/{device}/patch/{name}/" + action, name);
      }
      sendCmd(address, payload).then(function (res) {
        if (res.status === "ok") toast(action + ": " + name, "success");
      });
    });
  });

  // Patch period
  $("#btnPatchPeriod").addEventListener("click", function () {
    var name = getPatchName();
    if (!name) { toast("Enter a patch name", "error"); return; }
    var period = ($("#patchPeriod").value || "").trim();
    if (!period) { toast("Enter a period value", "error"); return; }
    var address = addr("/annieData/{device}/patch/{name}/period", name);
    sendCmd(address, period).then(function (res) {
      if (res.status === "ok") toast("Period set to " + period + "ms", "success");
    });
  });

  // Patch address mode
  $("#btnPatchAdrMode").addEventListener("click", function () {
    var name = getPatchName();
    if (!name) { toast("Enter a patch name", "error"); return; }
    var mode = $("#patchAdrMode").value;
    var address = addr("/annieData/{device}/patch/{name}/adrMode", name);
    sendCmd(address, mode).then(function (res) {
      if (res.status === "ok") toast("Address mode: " + mode, "success");
    });
  });

  // Patch override
  $("#btnPatchOverride").addEventListener("click", function () {
    var name = getPatchName();
    if (!name) { toast("Enter a patch name", "error"); return; }
    var flags = ($("#patchOverride").value || "").trim();
    if (!flags) { toast("Enter override flags", "error"); return; }
    var address = addr("/annieData/{device}/patch/{name}/override", name);
    sendCmd(address, flags).then(function (res) {
      if (res.status === "ok") toast("Override set", "success");
    });
  });

  // Add/Remove message to/from patch
  $("#btnPatchAddMsg").addEventListener("click", function () {
    var name = getPatchName();
    var msgs = ($("#patchMsgNames").value || "").trim();
    if (!name || !msgs) { toast("Enter patch name and message name(s)", "error"); return; }
    var address = addr("/annieData/{device}/patch/{name}/addMsg", name);
    sendCmd(address, msgs).then(function (res) {
      if (res.status === "ok") toast("Added to " + name, "success");
    });
  });

  $("#btnPatchRemoveMsg").addEventListener("click", function () {
    var name = getPatchName();
    var msgs = ($("#patchMsgNames").value || "").trim();
    if (!name || !msgs) { toast("Enter patch name and message name", "error"); return; }
    var address = addr("/annieData/{device}/patch/{name}/removeMsg", name);
    sendCmd(address, msgs).then(function (res) {
      if (res.status === "ok") toast("Removed from " + name, "success");
    });
  });

  // Solo
  $("#btnPatchSolo").addEventListener("click", function () {
    var name = getPatchName();
    var msgs = ($("#patchMsgNames").value || "").trim();
    if (!name || !msgs) { toast("Enter patch name and message to solo", "error"); return; }
    var address = addr("/annieData/{device}/patch/{name}/solo", name);
    sendCmd(address, msgs).then(function (res) {
      if (res.status === "ok") toast("Solo: " + msgs, "success");
    });
  });

  // Move message to patch
  $("#btnPatchMove").addEventListener("click", function () {
    var name = getPatchName();
    var msgs = ($("#patchMsgNames").value || "").trim();
    if (!name || !msgs) { toast("Enter patch name and message name", "error"); return; }
    var address = addr("/annieData/{device}/move");
    sendCmd(address, msgs + ", " + name).then(function (res) {
      if (res.status === "ok") toast("Moved " + msgs + " to " + name, "success");
    });
  });

  // setAll
  $("#btnPatchSetAll").addEventListener("click", function () {
    var name = getPatchName();
    if (!name) { toast("Enter a patch name", "error"); return; }
    var config = ($("#patchSetAllConfig").value || "").trim();
    if (!config) { toast("Enter a config string", "error"); return; }
    var address = addr("/annieData/{device}/patch/{name}/setAll", name);
    sendCmd(address, config).then(function (res) {
      if (res.status === "ok") toast("setAll applied", "success");
    });
  });

  // Clone / Rename patch
  $("#btnPatchClone").addEventListener("click", function () {
    var src = ($("#patchSrcName").value || "").trim();
    var dest = ($("#patchDestName").value || "").trim();
    if (!src || !dest) { toast("Enter both names", "error"); return; }
    var address = addr("/annieData/{device}/clone/patch");
    sendCmd(address, src + ", " + dest).then(function (res) {
      if (res.status === "ok") toast("Cloned: " + src + " → " + dest, "success");
    });
  });

  $("#btnPatchRename").addEventListener("click", function () {
    var src = ($("#patchSrcName").value || "").trim();
    var dest = ($("#patchDestName").value || "").trim();
    if (!src || !dest) { toast("Enter both names", "error"); return; }
    var address = addr("/annieData/{device}/rename/patch");
    sendCmd(address, src + ", " + dest).then(function (res) {
      if (res.status === "ok") toast("Renamed: " + src + " → " + dest, "success");
    });
  });

  // ==================================================================
  //  DIRECT
  // ==================================================================

  function updateDirectPreview() {
    var parts = [];
    var val = ($("#directValue") || {}).value || "";
    var ip = ($("#directIP") || {}).value || "";
    var port = ($("#directPort") || {}).value || "";
    var adrVal = ($("#directAdr") || {}).value || "";
    var low = (($("#directLow") || {}).value || "").trim();
    var high = (($("#directHigh") || {}).value || "").trim();
    var period = (($("#directPeriod") || {}).value || "").trim();
    if (val) parts.push("value:" + val);
    if (ip) parts.push("ip:" + ip);
    if (port) parts.push("port:" + port);
    if (adrVal) parts.push("adr:" + adrVal);
    if (low) parts.push("low:" + low);
    if (high) parts.push("high:" + high);
    if (period) parts.push("period:" + period);
    var el = $("#directPreview");
    if (el) el.value = parts.join(", ");
  }

  ["directValue", "directIP", "directPort", "directAdr", "directLow", "directHigh", "directPeriod"].forEach(function (id) {
    var el = $("#" + id);
    if (el) {
      el.addEventListener("input", updateDirectPreview);
      el.addEventListener("change", updateDirectPreview);
    }
  });
  updateDirectPreview();

  $("#btnDirectSend").addEventListener("click", function () {
    var name = ($("#directName").value || "").trim();
    if (!name) { toast("Enter a name", "error"); return; }
    var payload = ($("#directPreview").value || "").trim();
    if (!payload) { toast("Configure at least one field", "error"); return; }
    var address = addr("/annieData/{device}/direct/{name}", name);
    sendCmd(address, payload).then(function (res) {
      if (res.status === "ok") toast("Direct sent: " + name, "success");
    });
  });

  $("#btnDirectCopy").addEventListener("click", function () {
    var text = ($("#directPreview").value || "");
    if (navigator.clipboard) {
      navigator.clipboard.writeText(text).then(function () {
        toast("Config copied", "success");
      });
    } else {
      toast("Clipboard not available", "error");
    }
  });

  // ==================================================================
  //  ADVANCED: Raw OSC
  // ==================================================================

  var rawRepeating = false;

  $("#btnRawSend").addEventListener("click", function () {
    api("send", {
      host: $("#rawHost").value,
      port: parseInt($("#rawPort").value, 10),
      address: $("#rawAddress").value,
      args: $("#rawArgs").value || null,
    }).then(function (res) {
      if (res.status === "ok") toast("Sent", "success");
    });
  });

  $("#btnRawRepeat").addEventListener("click", function () {
    api("send/repeat", {
      host: $("#rawHost").value,
      port: parseInt($("#rawPort").value, 10),
      address: $("#rawAddress").value,
      args: $("#rawArgs").value || null,
      interval: parseInt($("#rawInterval").value, 10) || 1000,
      id: "raw-repeat",
    }).then(function (res) {
      if (res.status === "ok") {
        rawRepeating = true;
        $("#btnRawRepeat").disabled = true;
        $("#btnRawStop").disabled = false;
        toast("Repeat started", "info");
      }
    });
  });

  $("#btnRawStop").addEventListener("click", function () {
    api("send/stop", { id: "raw-repeat" }).then(function (res) {
      if (res.status === "ok") {
        rawRepeating = false;
        $("#btnRawRepeat").disabled = false;
        $("#btnRawStop").disabled = true;
        toast("Repeat stopped", "info");
      }
    });
  });

  // JSON batch send
  $("#btnJsonSend").addEventListener("click", function () {
    var messages;
    try {
      messages = JSON.parse($("#jsonInput").value);
    } catch (err) {
      toast("Invalid JSON: " + err.message, "error");
      return;
    }
    api("send/json", {
      host: $("#jsonHost").value,
      port: parseInt($("#jsonPort").value, 10),
      messages: messages,
      interval: parseInt($("#jsonInterval").value, 10) || 0,
    }).then(function (res) {
      if (res.status === "ok") toast("Sent " + messages.length + " messages", "success");
    });
  });

  // ==================================================================
  //  ADVANCED: Bridge
  // ==================================================================

  var activeBridges = {};

  function updateBridgeList() {
    var list = $("#activeBridges");
    var items = Object.entries(activeBridges);
    if (items.length === 0) {
      list.innerHTML = "";
      return;
    }
    list.innerHTML = "";
    items.forEach(function (pair) {
      var id = pair[0], info = pair[1];
      var row = document.createElement("div");
      row.className = "active-item";
      row.innerHTML =
        '<span><span class="active-item-dot"></span>' +
        ":" + info.in_port + " → " + info.out_host + ":" + info.out_port +
        (info.filter ? " (" + info.filter + ")" : "") +
        "</span>";
      var btn = document.createElement("button");
      btn.className = "btn btn-small btn-stop";
      btn.textContent = "Stop";
      btn.addEventListener("click", function () { stopBridge(id); });
      row.appendChild(btn);
      list.appendChild(row);
    });
  }

  function stopBridge(id) {
    api("bridge/stop", { id: id }).then(function (res) {
      if (res.status === "ok") {
        delete activeBridges[id];
        updateBridgeList();
        toast("Bridge stopped", "info");
        if (Object.keys(activeBridges).length === 0) {
          $("#btnBridgeStop").disabled = true;
        }
      }
    });
  }

  $("#btnBridgeStart").addEventListener("click", function () {
    var inPort = parseInt($("#bridgeInPort").value, 10);
    var outHost = $("#bridgeOutHost").value;
    var outPort = parseInt($("#bridgeOutPort").value, 10);
    var filter = $("#bridgeFilter").value;
    var id = "bridge-" + inPort + "-" + outPort;
    api("bridge/start", {
      in_port: inPort,
      out_host: outHost,
      out_port: outPort,
      filter: filter,
      id: id,
    }).then(function (res) {
      if (res.status === "ok") {
        activeBridges[id] = {
          in_port: inPort,
          out_host: outHost,
          out_port: outPort,
          filter: filter,
        };
        updateBridgeList();
        $("#btnBridgeStop").disabled = false;
        toast("Bridge started", "success");
      }
    });
  });

  $("#btnBridgeStop").addEventListener("click", function () {
    Object.keys(activeBridges).forEach(function (id) { stopBridge(id); });
  });

  // ==================================================================
  //  REPLY LISTENER (top bar)
  // ==================================================================

  var replyListening = false;

  $("#btnReplyListen").addEventListener("click", function () {
    if (replyListening) {
      // Stop
      var port = parseInt($("#replyPort").value, 10);
      api("recv/stop", { id: "reply-listener" }).then(function (res) {
        if (res.status === "ok") {
          replyListening = false;
          $("#btnReplyListen").classList.remove("active");
          $("#listenDot").classList.remove("on");
          toast("Reply listener stopped", "info");
        }
      });
    } else {
      // Start
      var port = parseInt($("#replyPort").value, 10);
      api("recv/start", { port: port, filter: "", id: "reply-listener" }).then(function (res) {
        if (res.status === "ok") {
          replyListening = true;
          $("#btnReplyListen").classList.add("active");
          $("#listenDot").classList.add("on");
          toast("Listening on port " + port, "success");
        }
      });
    }
  });

  // ==================================================================
  //  FEED CONTROLS
  // ==================================================================

  $("#btnFeedClear").addEventListener("click", function () {
    $("#feedLog").innerHTML = "";
    msgCount = 0;
    rateCounter = 0;
    api("log/clear", {});
  });

  // Re-filter on filter input change
  $("#feedFilter").addEventListener("input", function () {
    // We only filter new messages; existing ones stay.
    // For a full re-filter, we'd need to re-render from the log.
  });

  // ==================================================================
  //  REFERENCE TAB
  // ==================================================================

  fetch("/api/presets/theater-gwd")
    .then(function (r) { return r.json(); })
    .then(function (data) {
      if (!data.presets) return;
      var p = data.presets;

      // Commands reference
      if (p.commands) {
        renderCommandRef(p.commands);
      }

      // Keywords
      if (p.keywords) {
        renderKeywords(p.keywords);
      }

      // Config keys
      if (p.config_keys) {
        renderSimpleRef("configKeyList", p.config_keys);
      }

      // Address modes
      if (p.address_modes) {
        renderSimpleRef("adrModeList", p.address_modes);
      }
    })
    .catch(function () {});

  function renderCommandRef(commands) {
    var list = $("#cmdList");
    if (!list) return;
    var entries = Object.entries(commands).sort(function (a, b) {
      return a[0].localeCompare(b[0]);
    });
    list.innerHTML = "";
    entries.forEach(function (pair) {
      var key = pair[0], cmd = pair[1];
      var div = document.createElement("div");
      div.className = "ref-item";
      var html = '<span class="ref-term">' + key + "</span>";
      if (cmd.address) html += '<span class="ref-addr">' + cmd.address + "</span>";
      if (cmd.description) html += '<span class="ref-def">' + cmd.description + "</span>";
      if (cmd.payload) html += '<span class="ref-payload">payload: ' + cmd.payload + "</span>";
      div.innerHTML = html;
      list.appendChild(div);
    });

    // Search
    var search = $("#cmdSearch");
    if (search) {
      search.addEventListener("input", function () {
        var q = search.value.trim().toLowerCase();
        list.querySelectorAll(".ref-item").forEach(function (item) {
          item.style.display = item.textContent.toLowerCase().includes(q) ? "" : "none";
        });
      });
    }
  }

  function renderKeywords(keywords) {
    var list = $("#keywordList");
    if (!list) return;
    var entries = Object.entries(keywords).sort(function (a, b) {
      return a[0].localeCompare(b[0]);
    });
    list.innerHTML = "";
    entries.forEach(function (pair) {
      var div = document.createElement("div");
      div.className = "ref-item";
      div.innerHTML =
        '<span class="ref-term">' + pair[0] + "</span> " +
        '<span class="ref-def">' + pair[1] + "</span>";
      list.appendChild(div);
    });

    var search = $("#keywordSearch");
    if (search) {
      search.addEventListener("input", function () {
        var q = search.value.trim().toLowerCase();
        list.querySelectorAll(".ref-item").forEach(function (item) {
          item.style.display = item.textContent.toLowerCase().includes(q) ? "" : "none";
        });
      });
    }
  }

  function renderSimpleRef(listId, obj) {
    var list = $("#" + listId);
    if (!list) return;
    var entries = Object.entries(obj).sort(function (a, b) {
      return a[0].localeCompare(b[0]);
    });
    list.innerHTML = "";
    entries.forEach(function (pair) {
      var div = document.createElement("div");
      div.className = "ref-item";
      div.innerHTML =
        '<span class="ref-term">' + pair[0] + "</span> " +
        '<span class="ref-def">' + pair[1] + "</span>";
      list.appendChild(div);
    });
  }

  // ==================================================================
  //  INITIAL STATE
  // ==================================================================

  // Load existing log
  fetch("/api/log")
    .then(function (r) { return r.json(); })
    .then(function (data) {
      if (data.log) {
        data.log.forEach(function (entry) {
          appendToFeed(entry);
          msgCount++;
        });
      }
    })
    .catch(function () {});

  // Restore active state
  fetch("/api/status")
    .then(function (r) { return r.json(); })
    .then(function (data) {
      if (data.bridges) {
        Object.entries(data.bridges).forEach(function (pair) {
          activeBridges[pair[0]] = pair[1];
        });
        updateBridgeList();
        if (Object.keys(data.bridges).length > 0) {
          $("#btnBridgeStop").disabled = false;
        }
      }
      if (data.receivers && data.receivers["reply-listener"]) {
        replyListening = true;
        $("#btnReplyListen").classList.add("active");
        $("#listenDot").classList.add("on");
      }
    })
    .catch(function () {});

})();
