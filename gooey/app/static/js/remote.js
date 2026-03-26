/* ============================================================================
   TheaterGWD — Mobile Remote  (remote.js)
   Socket.IO client + screen navigation + OSC proxy
   ============================================================================ */

const App = (() => {
  /* ── State ──────────────────────────────────────────────────────────────── */
  let socket = null;
  let config = { host: "", port: 8000, device: "annieData", listen_port: 9000 };
  let connected = false;
  let screenStack = ["screenConnect"];
  let pendingResolve = null;   // for osc() Promise
  let pendingTimer  = null;
  let _currentMsgName   = null;
  let _currentPatchName = null;
  const monitorItems = [];

  /* ── Persistence ─────────────────────────────────────────────────────────── */
  function loadConfig() {
    try {
      const s = localStorage.getItem("remoteConfig");
      if (s) Object.assign(config, JSON.parse(s));
    } catch {}
  }
  function saveConfig() {
    try { localStorage.setItem("remoteConfig", JSON.stringify(config)); } catch {}
  }

  /* ── Screen navigation ──────────────────────────────────────────────────── */
  function go(id) {
    const cur = screenStack[screenStack.length - 1];
    if (cur === id) return;
    const curEl = document.getElementById(cur);
    const nextEl = document.getElementById(id);
    if (!nextEl) return;
    curEl.classList.add("slide-back");
    nextEl.classList.remove("hidden", "slide-back");
    screenStack.push(id);
    setTimeout(() => curEl.classList.add("hidden"), 230);
  }

  function back() {
    if (screenStack.length < 2) return;
    const cur  = screenStack.pop();
    const prev = screenStack[screenStack.length - 1];
    const curEl  = document.getElementById(cur);
    const prevEl = document.getElementById(prev);
    prevEl.classList.remove("hidden", "slide-back");
    curEl.classList.add("hidden");
    curEl.classList.remove("slide-back");
  }

  /* ── Toast / overlay / confirm ──────────────────────────────────────────── */
  const toastEl = document.getElementById("toast");
  let toastTimer;
  function toast(msg, ms = 2200) {
    toastEl.textContent = msg;
    toastEl.classList.add("show");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => toastEl.classList.remove("show"), ms);
  }

  const overlayEl = document.getElementById("overlay");
  const overlayMsg = document.getElementById("overlay-msg");
  function showLoading(msg = "Waiting…") { overlayMsg.textContent = msg; overlayEl.classList.remove("hidden"); }
  function hideLoading()                  { overlayEl.classList.add("hidden"); }

  function confirm(msg, cb) {
    document.getElementById("confirmMsg").textContent = msg;
    document.getElementById("confirmSheet").classList.remove("hidden");
    const ok  = document.getElementById("confirmOk");
    const can = document.getElementById("confirmCancel");
    function cleanup() {
      document.getElementById("confirmSheet").classList.add("hidden");
      ok.onclick = null; can.onclick = null;
    }
    ok.onclick  = () => { cleanup(); cb(); };
    can.onclick = () => cleanup();
  }

  /* ── WS dot sync ────────────────────────────────────────────────────────── */
  function setDot(ok) {
    ["wsDot","wsDot2"].forEach(id => {
      const d = document.getElementById(id);
      if (!d) return;
      d.className = "ws-dot" + (ok === true ? " ok" : ok === false ? " err" : "");
    });
  }

  /* ── Socket.IO ──────────────────────────────────────────────────────────── */
  function initSocket() {
    socket = io({ transports: ["websocket"], reconnectionDelay: 1500 });

    socket.on("connect",    () => setDot(true));
    socket.on("disconnect", () => { setDot(false); connected = false; });

    socket.on("remote_configured", d => {
      hideLoading();
      connected = true;
      document.getElementById("mainTitle").textContent = d.device || config.device;
      go("screenMain");
      toast("Connected");
    });

    socket.on("remote_error", d => {
      hideLoading();
      const s = document.getElementById("connectStatus");
      const m = document.getElementById("connectStatusMsg");
      s.style.display = "flex"; m.textContent = d.message || "Error";
      if (pendingResolve) { pendingResolve(null); pendingResolve = null; clearTimeout(pendingTimer); }
    });

    socket.on("remote_reply", d => {
      // Feed monitor
      addMonitorItem(d);
      // Resolve pending osc() call
      if (pendingResolve) {
        clearTimeout(pendingTimer);
        const pr = pendingResolve; pendingResolve = null;
        pr(d);
      }
    });
  }

  /* ── Configure session ──────────────────────────────────────────────────── */
  function configure(cfg) {
    Object.assign(config, cfg);
    saveConfig();
    showLoading("Connecting…");
    socket.emit("remote_configure", {
      host: config.host, port: config.port,
      device: config.device, listen_port: config.listen_port,
    });
  }

  /* ── OSC helpers ────────────────────────────────────────────────────────── */
  function addr(rest) {
    return `/annieData/${config.device}/${rest}`;
  }

  function _sendNoReply(rest, args) {
    if (!connected) return;
    socket.emit("remote_send", { address: addr(rest), args: args || null });
  }

  function osc(rest, args, timeoutMs = 4000) {
    return new Promise(resolve => {
      if (!connected) { resolve(null); return; }
      pendingResolve = resolve;
      socket.emit("remote_send", { address: addr(rest), args: args || null });
      pendingTimer = setTimeout(() => {
        if (pendingResolve) { pendingResolve(null); pendingResolve = null; }
      }, timeoutMs);
    });
  }

  function replyText(reply) {
    if (!reply) return "(no reply / timeout)";
    return reply.args.map(a => a.value).join(" ") || "(empty reply)";
  }

  function _sendAndShow(rest, args, title) {
    showLoading("Waiting for reply…");
    osc(rest, args).then(reply => {
      hideLoading();
      document.getElementById("resultTitle").textContent = title || "Reply";
      document.getElementById("resultAddr").textContent = addr(rest);
      document.getElementById("resultBox").textContent = replyText(reply);
      go("screenResult");
    });
  }

  /* ── Parse names from list reply ─────────────────────────────────────────── */
  function parseNames(reply) {
    const text = replyText(reply);
    // Space-separated tokens that look like names (word chars, dash, dot)
    return text.split(/[\s,]+/)
      .map(s => s.trim())
      .filter(s => /^[\w.\-]{1,32}$/.test(s) &&
                   !["ok","true","false","msgs","patches","none","error"].includes(s));
  }

  /* ── Monitor ─────────────────────────────────────────────────────────────── */
  function addMonitorItem(d) {
    const args = (d.args || []).map(a => a.value).join("  ");
    monitorItems.push({ address: d.address, args });
    if (monitorItems.length > 200) monitorItems.shift();
    const list = document.getElementById("monitorList");
    const el = document.createElement("div");
    el.className = "monitor-item";
    el.innerHTML = `<div class="monitor-item-addr">${esc(d.address)}</div>` +
                   (args ? `<div class="monitor-item-args">${esc(args)}</div>` : "");
    list.prepend(el);
    // Trim DOM
    while (list.children.length > 200) list.removeChild(list.lastChild);
  }

  function esc(s) {
    return String(s).replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;");
  }

  /* ── Messages screen ─────────────────────────────────────────────────────── */
  function loadMessages() {
    const body = document.getElementById("msgListBody");
    body.innerHTML = `<div class="card"><div class="row"><div class="row-text"><div class="row-sub">Loading…</div></div></div></div>`;
    showLoading("Fetching messages…");
    osc("list/msgs").then(reply => {
      hideLoading();
      const names = parseNames(reply);
      if (!names.length) {
        body.innerHTML = `<div class="card"><div class="row"><div class="row-text"><div class="row-sub">No messages found</div></div></div></div>`;
        return;
      }
      body.innerHTML = `<div class="section-label">Tap to manage</div>`;
      const card = document.createElement("div"); card.className = "card";
      names.forEach(name => {
        const row = document.createElement("div"); row.className = "row";
        row.innerHTML = `<div class="row-icon">📨</div><div class="row-text"><div class="row-title">${esc(name)}</div></div><div class="row-chev">›</div>`;
        row.addEventListener("click", () => openMsgDetail(name));
        card.appendChild(row);
      });
      body.appendChild(card);
    });
  }

  function openMsgDetail(name) {
    _currentMsgName = name;
    document.getElementById("msgDetailTitle").textContent = name;
    const body = document.getElementById("msgDetailBody");
    body.innerHTML = "";
    const card = document.createElement("div"); card.className = "card";
    const actions = [
      { label:"Info",    icon:"ℹ️",  fn: () => _sendAndShow(`msg/${name}/info`, null, `${name} info`) },
      { label:"Enable",  icon:"✅",  cls:"success", fn: () => { _sendNoReply(`msg/${name}/enable`); toast("Enabled"); back(); } },
      { label:"Disable", icon:"🔇",  cls:"warn",    fn: () => { _sendNoReply(`msg/${name}/disable`); toast("Disabled"); back(); } },
      { label:"Delete",  icon:"🗑",  cls:"danger",  fn: () => confirm(`Delete message "${name}"?`, () => { _sendNoReply(`msg/${name}/delete`); toast("Deleted"); back(); }) },
    ];
    actions.forEach(a => {
      const row = document.createElement("div"); row.className = "row" + (a.cls ? ` ${a.cls}` : "");
      row.innerHTML = `<div class="row-icon">${a.icon}</div><div class="row-text"><div class="row-title">${a.label}</div></div>`;
      row.addEventListener("click", a.fn);
      card.appendChild(row);
    });
    body.appendChild(card);
    go("screenMsgDetail");
  }

  /* ── Patches screen ──────────────────────────────────────────────────────── */
  function loadPatches() {
    const body = document.getElementById("patchListBody");
    body.innerHTML = `<div class="card"><div class="row"><div class="row-text"><div class="row-sub">Loading…</div></div></div></div>`;
    showLoading("Fetching patches…");
    osc("list/patches").then(reply => {
      hideLoading();
      const names = parseNames(reply);
      if (!names.length) {
        body.innerHTML = `<div class="card"><div class="row"><div class="row-text"><div class="row-sub">No patches found</div></div></div></div>`;
        return;
      }
      body.innerHTML = `<div class="section-label">Tap to manage</div>`;
      const card = document.createElement("div"); card.className = "card";
      names.forEach(name => {
        const row = document.createElement("div"); row.className = "row";
        row.innerHTML = `<div class="row-icon">🗂</div><div class="row-text"><div class="row-title">${esc(name)}</div></div><div class="row-chev">›</div>`;
        row.addEventListener("click", () => openPatchDetail(name));
        card.appendChild(row);
      });
      body.appendChild(card);
    });
  }

  function openPatchDetail(name) {
    _currentPatchName = name;
    document.getElementById("patchDetailTitle").textContent = name;
    const body = document.getElementById("patchDetailBody");
    body.innerHTML = "";
    const card = document.createElement("div"); card.className = "card";
    const actions = [
      { label:"Start",      icon:"▶️", cls:"success", fn: () => { _sendNoReply(`patch/${name}/start`); toast("Started"); } },
      { label:"Stop",       icon:"⏹", cls:"warn",    fn: () => { _sendNoReply(`patch/${name}/stop`);  toast("Stopped"); } },
      { label:"Info",       icon:"ℹ️",                fn: () => _sendAndShow(`patch/${name}/info`, null, `${name} info`) },
      { label:"Enable All", icon:"✅",                fn: () => { _sendNoReply(`patch/${name}/enableAll`); toast("All enabled"); } },
      { label:"Delete",     icon:"🗑", cls:"danger",  fn: () => confirm(`Delete patch "${name}"?`, () => { _sendNoReply(`patch/${name}/delete`); toast("Deleted"); back(); }) },
    ];
    actions.forEach(a => {
      const row = document.createElement("div"); row.className = "row" + (a.cls ? ` ${a.cls}` : "");
      row.innerHTML = `<div class="row-icon">${a.icon}</div><div class="row-text"><div class="row-title">${a.label}</div></div>`;
      row.addEventListener("click", a.fn);
      card.appendChild(row);
    });
    body.appendChild(card);
    go("screenPatchDetail");
  }

  /* ── Settings screen ─────────────────────────────────────────────────────── */
  function openSettings() {
    document.getElementById("sHost").value        = config.host;
    document.getElementById("sPort").value        = config.port;
    document.getElementById("sListenPort").value  = config.listen_port;
    document.getElementById("sDevice").value      = config.device;
    go("screenSettings");
  }

  /* ── Init ────────────────────────────────────────────────────────────────── */
  function init() {
    loadConfig();

    // Pre-fill connect form
    if (config.host) document.getElementById("cfgHost").value = config.host;
    document.getElementById("cfgPort").value        = config.port;
    document.getElementById("cfgListenPort").value  = config.listen_port;
    document.getElementById("cfgDevice").value      = config.device;

    initSocket();

    /* ── Connect screen ── */
    document.getElementById("btnConnect").addEventListener("click", () => {
      const host = document.getElementById("cfgHost").value.trim();
      const port = parseInt(document.getElementById("cfgPort").value, 10);
      const listen_port = parseInt(document.getElementById("cfgListenPort").value, 10);
      const device = document.getElementById("cfgDevice").value.trim() || "annieData";
      if (!host) { toast("Enter device IP"); return; }
      document.getElementById("connectStatus").style.display = "none";
      configure({ host, port, device, listen_port });
    });

    /* ── Main menu ── */
    document.getElementById("btnDisconnect").addEventListener("click", () => {
      confirm("Disconnect from device?", () => {
        socket.emit("remote_disconnect_session");
        connected = false;
        screenStack = ["screenConnect"];
        document.querySelectorAll(".screen").forEach(s => s.classList.add("hidden"));
        document.getElementById("screenConnect").classList.remove("hidden");
      });
    });
    document.getElementById("menuMessages").addEventListener("click", () => { go("screenMessages"); loadMessages(); });
    document.getElementById("menuPatches").addEventListener("click",  () => { go("screenPatches");  loadPatches();  });
    document.getElementById("menuQuick").addEventListener("click",    () => go("screenQuick"));
    document.getElementById("menuMonitor").addEventListener("click",  () => go("screenMonitor"));
    document.getElementById("menuSettings").addEventListener("click", () => openSettings());

    /* ── Back buttons ── */
    ["backMessages","backPatches","backMsgDetail","backPatchDetail",
     "backQuick","backMonitor","backSettings","backResult"].forEach(id => {
      document.getElementById(id).addEventListener("click", back);
    });

    /* ── Messages ── */
    document.getElementById("btnRefreshMsgs").addEventListener("click", loadMessages);

    /* ── Patches ── */
    document.getElementById("btnRefreshPatches").addEventListener("click", loadPatches);

    /* ── Quick actions ── */
    document.getElementById("qBlackout").addEventListener("click", () =>
      confirm("Blackout? All patches will stop.", () => { _sendNoReply("blackout"); toast("Blackout sent"); }));
    document.getElementById("qRestore").addEventListener("click",  () =>
      confirm("Restore all previous patches?", () => { _sendNoReply("restore"); toast("Restore sent"); }));
    document.getElementById("qSave").addEventListener("click",     () => { _sendNoReply("save"); toast("Save sent"); });
    document.getElementById("qLoad").addEventListener("click",     () =>
      confirm("Reload config from NVS?", () => { _sendNoReply("load"); toast("Load sent"); }));
    document.getElementById("qListAll").addEventListener("click",  () => _sendAndShow("list/all", "verbose", "All"));
    document.getElementById("qNvsClear").addEventListener("click", () =>
      confirm("ERASE all NVS data? This cannot be undone.", () => { _sendNoReply("nvs/clear"); toast("NVS cleared"); back(); }));

    /* ── Monitor ── */
    document.getElementById("btnClearMonitor").addEventListener("click", () => {
      document.getElementById("monitorList").innerHTML = "";
      monitorItems.length = 0;
    });

    /* ── Settings ── */
    document.getElementById("btnSaveSettings").addEventListener("click", () => {
      const host        = document.getElementById("sHost").value.trim();
      const port        = parseInt(document.getElementById("sPort").value, 10);
      const listen_port = parseInt(document.getElementById("sListenPort").value, 10);
      const device      = document.getElementById("sDevice").value.trim() || "annieData";
      if (!host) { toast("Enter device IP"); return; }
      back(); // go back to main
      configure({ host, port, device, listen_port });
    });
  }

  return { init };
})();

document.addEventListener("DOMContentLoaded", App.init);
