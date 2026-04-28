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
  let _currentSceneName = null;
  const monitorItems = [];

  /* ── Tailwind class tokens ─────────────────────────────────────────────── */
  const _tw = {
    card:       "bg-[#1a1a1a] rounded-[10px] overflow-hidden",
    cardLabel:  "text-[10px] uppercase tracking-wider text-[#aaa] px-3.5 pt-2.5 pb-1",
    row:        "flex items-center gap-2.5 min-h-[52px] px-3.5 border-t border-[#333] cursor-pointer select-none active:bg-[#242424]",
    rowFirst:   "flex items-center gap-2.5 min-h-[52px] px-3.5 cursor-pointer select-none active:bg-[#242424]",
    rowIcon:    "text-xl shrink-0 w-7 text-center",
    rowText:    "flex-1 overflow-hidden",
    rowTitle:   "text-[13px] whitespace-nowrap overflow-hidden text-ellipsis",
    rowSub:     "text-[11px] text-[#aaa] whitespace-nowrap overflow-hidden text-ellipsis",
    rowChev:    "text-[#aaa] text-base shrink-0",
    sectionLbl: "text-[10px] uppercase tracking-wider text-[#aaa] px-1 pt-2",
    tagList:    "flex flex-wrap gap-2 px-3.5 py-3",
    tag:        "bg-[#242424] border-[1.5px] border-[#333] rounded-full px-3.5 py-1.5 text-xs cursor-pointer whitespace-nowrap active:bg-[#DAC7FF] active:text-black",
    textInput:  "w-full bg-[#242424] border-[1.5px] border-[#333] rounded-[7px] text-[#f0f0f0] font-inherit text-sm px-3 py-2.5 outline-none appearance-none focus:border-[#DAC7FF]",
    btn:        "flex items-center justify-center gap-2 min-h-[52px] rounded-[10px] border-none font-inherit text-sm font-semibold cursor-pointer w-full px-4 active:brightness-125",
    btnPrimary: "bg-[#DAC7FF] text-black",
    btnStop:    "bg-[#D64541] text-white",
    btnSecondary: "bg-[#242424] text-[#f0f0f0]",
    btnRow:     "flex gap-2",
    recNameRow: "px-3.5 py-2",
    recStatus:  "flex items-center gap-2 px-3.5 py-2 flex-wrap",
    recDot:     "w-[9px] h-[9px] rounded-full bg-[#D64541] animate-[rec-blink_0.8s_step-start_infinite] shrink-0",
    clsDanger:  "text-[#D64541]",
    clsSuccess: "text-[#4CAF50]",
    clsWarn:    "text-[#f59e0b]",
  };
  const _titleColor = { danger: _tw.clsDanger, success: _tw.clsSuccess, warn: _tw.clsWarn };

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
  const _wsDotBase = "w-[9px] h-[9px] rounded-full shrink-0";
  function setDot(ok) {
    ["wsDot","wsDot2"].forEach(id => {
      const d = document.getElementById(id);
      if (!d) return;
      d.className = _wsDotBase + (ok === true ? " bg-[#4CAF50]" : ok === false ? " bg-[#D64541]" : " bg-[#90849c]");
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
                   !["ok","true","false","msgs","scenes","none","error"].includes(s));
  }

  /* ── Monitor ─────────────────────────────────────────────────────────────── */
  function addMonitorItem(d) {
    const args = (d.args || []).map(a => a.value).join("  ");
    monitorItems.push({ address: d.address, args });
    if (monitorItems.length > 200) monitorItems.shift();
    const list = document.getElementById("monitorList");
    const el = document.createElement("div");
    el.className = "bg-[#1a1a1a] rounded-md px-3 py-2 text-[11px]";
    el.innerHTML = `<div class="text-[#DAC7FF]">${esc(d.address)}</div>` +
                   (args ? `<div class="text-[#aaa]">${esc(args)}</div>` : "");
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
    body.innerHTML = `<div class="${_tw.card}"><div class="${_tw.rowFirst}"><div class="${_tw.rowText}"><div class="${_tw.rowSub}">Loading…</div></div></div></div>`;
    showLoading("Fetching messages…");
    osc("list/msgs").then(reply => {
      hideLoading();
      const names = parseNames(reply);
      if (!names.length) {
        body.innerHTML = `<div class="${_tw.card}"><div class="${_tw.rowFirst}"><div class="${_tw.rowText}"><div class="${_tw.rowSub}">No messages found</div></div></div></div>`;
        return;
      }
      body.innerHTML = `<div class="${_tw.sectionLbl}">Tap to manage</div>`;
      const card = document.createElement("div"); card.className = _tw.card;
      names.forEach((name, i) => {
        const row = document.createElement("div"); row.className = i === 0 ? _tw.rowFirst : _tw.row;
        row.innerHTML = `<div class="${_tw.rowIcon}">📨</div><div class="${_tw.rowText}"><div class="${_tw.rowTitle}">${esc(name)}</div></div><div class="${_tw.rowChev}">›</div>`;
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
    const card = document.createElement("div"); card.className = _tw.card;
    const actions = [
      { label:"Info",    icon:"ℹ️",  fn: () => _sendAndShow(`msg/${name}/info`, null, `${name} info`) },
      { label:"Enable",  icon:"✅",  cls:"success", fn: () => { _sendNoReply(`msg/${name}/enable`); toast("Enabled"); back(); } },
      { label:"Disable", icon:"🔇",  cls:"warn",    fn: () => { _sendNoReply(`msg/${name}/disable`); toast("Disabled"); back(); } },
      { label:"Delete",  icon:"🗑",  cls:"danger",  fn: () => confirm(`Delete message "${name}"?`, () => { _sendNoReply(`msg/${name}/delete`); toast("Deleted"); back(); }) },
    ];
    actions.forEach((a, i) => {
      const row = document.createElement("div"); row.className = i === 0 ? _tw.rowFirst : _tw.row;
      const titleCls = _tw.rowTitle + (a.cls && _titleColor[a.cls] ? ` ${_titleColor[a.cls]}` : "");
      row.innerHTML = `<div class="${_tw.rowIcon}">${a.icon}</div><div class="${_tw.rowText}"><div class="${titleCls}">${a.label}</div></div>`;
      row.addEventListener("click", a.fn);
      card.appendChild(row);
    });
    body.appendChild(card);
    go("screenMsgDetail");
  }

  /* ── Scenes screen ──────────────────────────────────────────────────────── */
  function loadScenes() {
    const body = document.getElementById("sceneListBody");
    body.innerHTML = `<div class="${_tw.card}"><div class="${_tw.rowFirst}"><div class="${_tw.rowText}"><div class="${_tw.rowSub}">Loading…</div></div></div></div>`;
    showLoading("Fetching scenes…");
    osc("list/scenes").then(reply => {
      hideLoading();
      const names = parseNames(reply);
      if (!names.length) {
        body.innerHTML = `<div class="${_tw.card}"><div class="${_tw.rowFirst}"><div class="${_tw.rowText}"><div class="${_tw.rowSub}">No scenes found</div></div></div></div>`;
        return;
      }
      body.innerHTML = `<div class="${_tw.sectionLbl}">Tap to manage</div>`;
      const card = document.createElement("div"); card.className = _tw.card;
      names.forEach((name, i) => {
        const row = document.createElement("div"); row.className = i === 0 ? _tw.rowFirst : _tw.row;
        row.innerHTML = `<div class="${_tw.rowIcon}">🗂</div><div class="${_tw.rowText}"><div class="${_tw.rowTitle}">${esc(name)}</div></div><div class="${_tw.rowChev}">›</div>`;
        row.addEventListener("click", () => openSceneDetail(name));
        card.appendChild(row);
      });
      body.appendChild(card);
    });
  }

  function openSceneDetail(name) {
    _currentSceneName = name;
    document.getElementById("sceneDetailTitle").textContent = name;
    const body = document.getElementById("sceneDetailBody");
    body.innerHTML = "";
    const card = document.createElement("div"); card.className = _tw.card;
    const actions = [
      { label:"Start",      icon:"▶️", cls:"success", fn: () => { _sendNoReply(`scene/${name}/start`); toast("Started"); } },
      { label:"Stop",       icon:"⏹", cls:"warn",    fn: () => { _sendNoReply(`scene/${name}/stop`);  toast("Stopped"); } },
      { label:"Info",       icon:"ℹ️",                fn: () => _sendAndShow(`scene/${name}/info`, null, `${name} info`) },
      { label:"Enable All", icon:"✅",                fn: () => { _sendNoReply(`scene/${name}/enableAll`); toast("All enabled"); } },
      { label:"Delete",     icon:"🗑", cls:"danger",  fn: () => confirm(`Delete scene "${name}"?`, () => { _sendNoReply(`scene/${name}/delete`); toast("Deleted"); back(); }) },
    ];
    actions.forEach((a, i) => {
      const row = document.createElement("div"); row.className = i === 0 ? _tw.rowFirst : _tw.row;
      const titleCls = _tw.rowTitle + (a.cls && _titleColor[a.cls] ? ` ${_titleColor[a.cls]}` : "");
      row.innerHTML = `<div class="${_tw.rowIcon}">${a.icon}</div><div class="${_tw.rowText}"><div class="${titleCls}">${a.label}</div></div>`;
      row.addEventListener("click", a.fn);
      card.appendChild(row);
    });
    body.appendChild(card);
    go("screenSceneDetail");
  }

  /* ── Oris screen ─────────────────────────────────────────────────────────── */

  let _recPollId = null;

  function _stopRemoteRecPoll() {
    if (_recPollId) { clearInterval(_recPollId); _recPollId = null; }
  }

  function _setRemoteRecUI(active) {
    const startBtn   = document.getElementById("btnRemoteRecStart");
    const stopBtn    = document.getElementById("btnRemoteRecStop");
    const cancelBtn  = document.getElementById("btnRemoteRecCancel");
    const statusRow  = document.getElementById("remoteRecStatus");
    const nameInput  = document.getElementById("remoteRecName");
    if (active) {
      if (startBtn)  startBtn.classList.add("hidden");
      if (statusRow) statusRow.classList.remove("hidden");
      if (nameInput) nameInput.disabled = true;
    } else {
      if (startBtn)  startBtn.classList.remove("hidden");
      if (statusRow) statusRow.classList.add("hidden");
      if (nameInput) nameInput.disabled = false;
      const ctr = document.getElementById("remoteRecCounter");
      if (ctr) ctr.textContent = "0 samples";
      _stopRemoteRecPoll();
    }
  }

  function loadOris() {
    const body = document.getElementById("oriListBody");
    body.innerHTML = `<div class="${_tw.card}"><div class="${_tw.rowFirst}"><div class="${_tw.rowText}"><div class="${_tw.rowSub}">Loading…</div></div></div></div>`;
    showLoading("Fetching oris…");
    osc("ori/list").then(reply => {
      hideLoading();
      const names = parseNames(reply);
      body.innerHTML = "";

      /* ── Quick-tap: one button per ori ── */
      const quickCard = document.createElement("div"); quickCard.className = _tw.card;
      const quickLabel = document.createElement("div"); quickLabel.className = _tw.cardLabel;
      quickLabel.textContent = "Quick Save — tap to add one sample";
      quickCard.appendChild(quickLabel);
      if (names.length) {
        const tagList = document.createElement("div"); tagList.className = _tw.tagList;
        names.forEach(name => {
          const tag = document.createElement("button"); tag.className = _tw.tag;
          tag.textContent = name;
          tag.addEventListener("click", () => {
            _sendNoReply(`ori/save/${name}`);
            toast(`Saved sample to "${name}"`);
          });
          tagList.appendChild(tag);
        });
        quickCard.appendChild(tagList);
      } else {
        quickCard.innerHTML += `<div class="${_tw.rowFirst}"><div class="${_tw.rowText}"><div class="${_tw.rowSub}">No oris registered yet</div></div></div>`;
      }
      body.appendChild(quickCard);

      /* ── Recording section ── */
      const recCard = document.createElement("div"); recCard.className = _tw.card;
      recCard.innerHTML = `
        <div class="${_tw.cardLabel}">Record Ori — hold pose, then stop</div>
        <div class="${_tw.recNameRow}">
          <input id="remoteRecName" class="${_tw.textInput}" type="text" placeholder="ori name"
                 list="remoteOriList" autocomplete="off">
          <datalist id="remoteOriList">
            ${names.map(n => `<option value="${esc(n)}">`).join("")}
          </datalist>
        </div>
        <button id="btnRemoteRecStart" class="${_tw.btn} ${_tw.btnPrimary}">▶ Start Recording</button>
        <div id="remoteRecStatus" class="${_tw.recStatus} hidden">
          <span class="${_tw.recDot}"></span>
          <span id="remoteRecCounter">0 samples</span>
          <div class="${_tw.btnRow}" style="margin-top:6px">
            <button id="btnRemoteRecStop"   class="${_tw.btn} ${_tw.btnStop}">■ Stop</button>
            <button id="btnRemoteRecCancel" class="${_tw.btn} ${_tw.btnSecondary}">✕ Cancel</button>
          </div>
        </div>`;
      body.appendChild(recCard);

      /* Wire recording buttons */
      document.getElementById("btnRemoteRecStart").addEventListener("click", () => {
        const name = (document.getElementById("remoteRecName").value || "").trim();
        if (!name) { toast("Enter an ori name"); return; }
        osc(`ori/record/start/${name}`).then(() => {
          toast(`Recording: ${name}`);
          _setRemoteRecUI(true);
          _recPollId = setInterval(() => {
            osc("ori/record/status").then(r => {
              const text = replyText(r);
              if (/active:false/.test(text)) {
                _setRemoteRecUI(false);
                loadOris();
                return;
              }
              const cm = text.match(/count:(\d+)/);
              const ctr = document.getElementById("remoteRecCounter");
              if (cm && ctr) ctr.textContent = cm[1] + " samples";
            });
          }, 500);
        });
      });
      document.getElementById("btnRemoteRecStop").addEventListener("click", () => {
        osc("ori/record/stop").then(r => {
          _setRemoteRecUI(false);
          toast(replyText(r).split(",")[0]);
          loadOris();
        });
      });
      document.getElementById("btnRemoteRecCancel").addEventListener("click", () => {
        _sendNoReply("ori/record/cancel");
        _setRemoteRecUI(false);
        toast("Recording cancelled");
      });

      /* ── Full ori list — tap to view details ── */
      if (names.length) {
        const listCard = document.createElement("div"); listCard.className = _tw.card;
        names.forEach((name, i) => {
          const row = document.createElement("div"); row.className = i === 0 ? _tw.rowFirst : _tw.row;
          row.innerHTML = `<div class="${_tw.rowIcon}">🧭</div><div class="${_tw.rowText}"><div class="${_tw.rowTitle}">${esc(name)}</div></div><div class="${_tw.rowChev}">›</div>`;
          row.addEventListener("click", () => openOriDetail(name));
          listCard.appendChild(row);
        });
        body.appendChild(listCard);
      }
    });
  }

  function openOriDetail(name) {
    document.getElementById("oriDetailTitle").textContent = name;
    const body = document.getElementById("oriDetailBody");
    body.innerHTML = "";
    const card = document.createElement("div"); card.className = _tw.card;
    const actions = [
      { label: "Info",              icon: "ℹ️",              fn: () => _sendAndShow(`ori/info/${name}`, null, `${name} info`) },
      { label: "Quick Save Sample", icon: "📍",              fn: () => { _sendNoReply(`ori/save/${name}`); toast("Sample added"); } },
      { label: "Clear Samples",     icon: "🔄", cls: "warn", fn: () => confirm(`Clear samples for "${name}"?`, () => { _sendNoReply(`ori/reset/${name}`); toast("Samples cleared"); back(); }) },
      { label: "Delete",            icon: "🗑", cls: "danger", fn: () => confirm(`Delete ori "${name}"?`, () => { _sendNoReply(`ori/delete/${name}`); toast("Deleted"); back(); }) },
    ];
    actions.forEach((a, i) => {
      const row = document.createElement("div"); row.className = i === 0 ? _tw.rowFirst : _tw.row;
      const titleCls = _tw.rowTitle + (a.cls && _titleColor[a.cls] ? ` ${_titleColor[a.cls]}` : "");
      row.innerHTML = `<div class="${_tw.rowIcon}">${a.icon}</div><div class="${_tw.rowText}"><div class="${titleCls}">${a.label}</div></div>`;
      row.addEventListener("click", a.fn);
      card.appendChild(row);
    });
    body.appendChild(card);
    go("screenOriDetail");
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
    document.getElementById("menuScenes").addEventListener("click",  () => { go("screenScenes");  loadScenes();  });
    document.getElementById("menuOris").addEventListener("click",     () => { go("screenOris"); });
    document.getElementById("menuQuick").addEventListener("click",    () => go("screenQuick"));
    document.getElementById("menuMonitor").addEventListener("click",  () => go("screenMonitor"));
    document.getElementById("menuSettings").addEventListener("click", () => openSettings());

    /* ── Back buttons ── */
    ["backMessages","backScenes","backMsgDetail","backSceneDetail",
     "backOris","backOriDetail",
     "backQuick","backMonitor","backSettings","backResult"].forEach(id => {
      document.getElementById(id).addEventListener("click", back);
    });

    /* ── Messages ── */
    document.getElementById("btnRefreshMsgs").addEventListener("click", loadMessages);

    /* ── Oris ── */
    document.getElementById("btnRefreshOris").addEventListener("click", loadOris);
    document.getElementById("qOriStrict").addEventListener("click", () => { _sendNoReply("ori/strict"); toast("Strict mode toggled"); });
    document.getElementById("qOriClear").addEventListener("click", () =>
      confirm("Delete ALL saved oris?", () => { _sendNoReply("ori/clear"); toast("All oris cleared"); loadOris(); }));
    document.getElementById("btnSetThresh").addEventListener("click", () => {
      const v = document.getElementById("oriThreshInput").value.trim();
      if (!v) { toast("Enter a threshold value"); return; }
      _sendNoReply("ori/threshold", v); toast(`Threshold set to ${v}`);
    });
    document.getElementById("btnSetTol").addEventListener("click", () => {
      const v = document.getElementById("oriTolInput").value.trim();
      if (!v) { toast("Enter a tolerance value"); return; }
      _sendNoReply("ori/tolerance", v); toast(`Tolerance set to ${v}`);
    });
    document.getElementById("btnSaveOri").addEventListener("click", () => {
      const name = document.getElementById("oriNewName").value.trim();
      if (!name) { toast("Enter a name for the orientation"); return; }
      _sendNoReply(`ori/save/${name}`);
      toast(`Saved sample to "${name}"`);
      document.getElementById("oriNewName").value = "";
      loadOris();
    });

    /* ── Scenes ── */
    document.getElementById("btnRefreshScenes").addEventListener("click", loadScenes);

    /* ── Quick actions ── */
    document.getElementById("qBlackout").addEventListener("click", () =>
      confirm("Blackout? All scenes will stop.", () => { _sendNoReply("blackout"); toast("Blackout sent"); }));
    document.getElementById("qRestore").addEventListener("click",  () =>
      confirm("Restore all previous scenes?", () => { _sendNoReply("restore"); toast("Restore sent"); }));
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
