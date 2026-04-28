/* ── Script console — settings toggles, Python editor, templates, CRUD ── */

import { socket, activeDevice, $, $$ } from "./state.js";
import { toast, showConfirm } from "./toast.js";
import { api } from "./api.js";
import { getActiveDev, devHost, devPort, devName } from "./device-manager.js";
import { SENSOR_CATEGORIES } from "./message-manager.js";
import { setAssumeIpPrefix } from "./field-validation.js";

(function () {
  /* ── Direct auto-start toggle ── */
  (function () {
    const DIRECT_START_KEY = "gooey_direct_autostart";
    const chk = $("#chkDirectAutoStart");
    let saved = null;
    try { saved = localStorage.getItem(DIRECT_START_KEY); } catch (e) { /* ignore */ }
    /* Default is checked (true); only override if explicitly saved as "0" */
    if (saved === "0" && chk) chk.checked = false;
    if (chk) chk.addEventListener("change", function () {
      try { localStorage.setItem(DIRECT_START_KEY, chk.checked ? "1" : "0"); } catch (e) { /* ignore */ }
    });
  }());

  /* ── Bulk Actions toggle ── */
  (function () {
    const BULK_KEY = "gooey_bulk_actions";
    const chkBulk = $("#chkShowBulkActions");
    function setBulkVisible(on) {
      document.body.classList.toggle("bulk-actions-visible", on);
      try { localStorage.setItem(BULK_KEY, on ? "1" : "0"); } catch (e) { /* ignore */ }
    }
    let saved = null;
    try { saved = localStorage.getItem(BULK_KEY); } catch (e) { /* ignore */ }
    if (saved === "1") {
      if (chkBulk) chkBulk.checked = true;
      setBulkVisible(true);
    }
    if (chkBulk) chkBulk.addEventListener("change", function () { setBulkVisible(chkBulk.checked); });
  }());

  /* ── Quaternion sensors toggle ── */
  (function () {
    const QUAT_KEY = "gooey_show_quats";
    const chkQuat = $("#chkShowQuats");
    const quatCat = SENSOR_CATEGORIES.filter(function (c) { return c.id === "quaternion"; })[0];
    function setQuatsVisible(on) {
      $$("select[id$='Category']").forEach(function (sel) {
        const existing = sel.querySelector('option[value="quaternion"]');
        if (on && !existing && quatCat) {
          const opt = document.createElement("option");
          opt.value = "quaternion"; opt.textContent = quatCat.label;
          sel.appendChild(opt);
        } else if (!on && existing) {
          existing.remove();
          if (sel.value === "quaternion") { sel.value = ""; sel.dispatchEvent(new Event("change")); }
        }
      });
      try { localStorage.setItem(QUAT_KEY, on ? "1" : "0"); } catch (e) { /* ignore */ }
    }
    let saved = null;
    try { saved = localStorage.getItem(QUAT_KEY); } catch (e) { /* ignore */ }
    if (saved === "1") {
      if (chkQuat) chkQuat.checked = true;
      setQuatsVisible(true);
    }
    if (chkQuat) chkQuat.addEventListener("change", function () { setQuatsVisible(chkQuat.checked); });
  }());

  /* ── Gate controls toggle ── */
  (function () {
    const GATE_KEY = "gooey_show_gate";
    const chkGate = $("#chkShowGate");
    function setGateVisible(on) {
      $$(".gate-section").forEach(function (el) {
        if (el.id && el.id.indexOf("Section") >= 0) el.style.display = on ? "" : "none";
      });
      try { localStorage.setItem(GATE_KEY, on ? "1" : "0"); } catch (e) { /* ignore */ }
    }
    let saved = null;
    try { saved = localStorage.getItem(GATE_KEY); } catch (e) { /* ignore */ }
    if (saved === "1") {
      if (chkGate) chkGate.checked = true;
      setGateVisible(true);
    }
    if (chkGate) chkGate.addEventListener("change", function () { setGateVisible(chkGate.checked); });
  }());

  /* ── Message values as gate sources toggle ── */
  (function () {
    const MSG_GATE_KEY = "gooey_msg_gate_sources";
    const chkMsg = $("#chkMsgGateSources");
    function refreshGateMsgSources(on) {
      const dev = getActiveDev();
      const names = (on && dev) ? Object.keys(dev.messages) : [];
      /* Gate pickers are global objects created by the gate-picker IIFE.
         Access them via the window namespace that the original code used. */
      if (window.msgGatePicker) window.msgGatePicker.refreshMsgSources(names);
      if (window.sceneGatePicker) window.sceneGatePicker.refreshMsgSources(names);
      try { localStorage.setItem(MSG_GATE_KEY, on ? "1" : "0"); } catch (e) { /* ignore */ }
    }
    let saved = null;
    try { saved = localStorage.getItem(MSG_GATE_KEY); } catch (e) { /* ignore */ }
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
    const ASSUME_IP_KEY        = "gooey_assume_ip";
    const ASSUME_IP_CUSTOM_KEY = "gooey_assume_ip_custom";
    const sel         = $("#assumeIpSelect");
    const customGroup = $("#assumeIpCustomGroup");
    const customInput = $("#assumeIpCustom");

    function getPrefix() {
      if (!sel) return "";
      if (sel.value === "custom") return customInput ? customInput.value.trim() : "";
      return sel.value || "";
    }

    function updateIpLabels(prefix) {
      setAssumeIpPrefix(prefix);
      let badge = "";
      if (prefix) {
        const prefixDots = (prefix.match(/\./g) || []).length;
        const needed = 4 - prefixDots;
        const placeholders = ["X", "Y", "Z"].slice(0, needed);
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
      const prefix = getPrefix();
      updateIpLabels(prefix);
      try {
        localStorage.setItem(ASSUME_IP_KEY, sel ? sel.value : "");
        if (customInput) localStorage.setItem(ASSUME_IP_CUSTOM_KEY, customInput.value.trim());
      } catch (e) { /* ignore */ }
    }

    /* Restore from localStorage */
    (function () {
      let savedVal = null;
      let savedCustom = "";
      try {
        savedVal    = localStorage.getItem(ASSUME_IP_KEY);
        savedCustom = localStorage.getItem(ASSUME_IP_CUSTOM_KEY) || "";
      } catch (e) { /* ignore */ }
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

  /* ── Script editor state ── */
  const SCRIPT_KEY       = "gooey_enable_script";
  const SCRIPT_DRAFT_KEY = "gooey_script_draft";
  const SCRIPT_NAME_KEY  = "gooey_script_name";
  const navBtn           = $("#navScript");
  const chkEnable        = $("#chkEnableScript");
  const editor           = $("#scriptEditor");
  const lineNums         = $("#scriptLineNumbers");
  const consoleEl        = $("#scriptConsole");
  const btnRun           = $("#btnScriptRun");
  const btnStop          = $("#btnScriptStop");
  const btnClear         = $("#btnScriptConsoleClear");
  const btnSave          = $("#btnScriptSave");
  const btnSaveAs        = $("#btnScriptSaveAs");
  const btnLoad          = $("#btnScriptLoad");
  const btnDelete        = $("#btnScriptDelete");
  const btnTemplate      = $("#btnScriptTemplate");
  const selLoad          = $("#scriptLoadSelect");
  const selTemplate      = $("#scriptTemplateSelect");
  const selMode          = $("#scriptMode");
  const inputInterval    = $("#scriptInterval");
  const inputListenPort  = $("#scriptListenPort");

  const btnMirrorFeed    = $("#chkScriptMirrorFeed");

  let scriptRunning    = false;
  let mirrorToFeed     = false;
  let currentScriptName = "";

  /* ── Enable/disable toggle ── */
  function setScriptEnabled(on) {
    if (navBtn) navBtn.style.display = on ? "" : "none";
    try { localStorage.setItem(SCRIPT_KEY, on ? "1" : "0"); } catch (e) { /* ignore */ }
  }

  // Restore from localStorage
  (function () {
    let saved = null;
    try { saved = localStorage.getItem(SCRIPT_KEY); } catch (e) { /* ignore */ }
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
          const msgBtn = $(".nav-btn[data-section='messages']");
          if (msgBtn) msgBtn.click();
        }
      }
    });
  }

  /* ── Line numbers ── */
  function updateLineNumbers() {
    if (!editor || !lineNums) return;
    const lines = editor.value.split("\n").length;
    const nums = [];
    for (let i = 1; i <= lines; i++) nums.push(i);
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
        const start = editor.selectionStart;
        const end = editor.selectionEnd;
        editor.value = editor.value.substring(0, start) + "    " + editor.value.substring(end);
        editor.selectionStart = editor.selectionEnd = start + 4;
        updateLineNumbers();
      }
    });

    // Restore draft
    try {
      const draft = localStorage.getItem(SCRIPT_DRAFT_KEY);
      if (draft) editor.value = draft;
      const savedName = localStorage.getItem(SCRIPT_NAME_KEY);
      if (savedName) currentScriptName = savedName;
    } catch (e) { /* ignore */ }
    updateLineNumbers();

    // Auto-save draft
    editor.addEventListener("input", function () {
      try { localStorage.setItem(SCRIPT_DRAFT_KEY, editor.value); } catch (e) { /* ignore */ }
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
    const line = document.createElement("div");
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
    const prefix = data.time ? "[" + data.time + "] " : "";
    appendConsole(prefix + data.text, data.level);
    // Mirror to Live Feed if enabled
    if (mirrorToFeed) {
      const feedLog = $("#feedLog");
      if (feedLog) {
        const entry = document.createElement("div");
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
      const code = editor.value.trim();
      if (!code) { toast("No script to run", "error"); return; }
      let interval = inputInterval ? parseInt(inputInterval.value, 10) || 50 : 50;
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
        device_id:   activeDevice.id || null,
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
        const opt = document.createElement("option");
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
      let name = currentScriptName || prompt("Script name:");
      if (!name) return;
      name = name.trim();
      if (!name) return;
      api("scripts/" + encodeURIComponent(name), { code: editor.value }).then(function (res) {
        if (res.status === "ok") {
          currentScriptName = name;
          try { localStorage.setItem(SCRIPT_NAME_KEY, name); } catch (e) { /* ignore */ }
          toast("Saved: " + name, "info");
          refreshScriptList();
        }
      });
    });
  }

  if (btnSaveAs) {
    btnSaveAs.addEventListener("click", function () {
      if (!editor) return;
      let name = prompt("Save script as:", currentScriptName || "");
      if (!name) return;
      name = name.trim();
      if (!name) return;
      api("scripts/" + encodeURIComponent(name), { code: editor.value }).then(function (res) {
        if (res.status === "ok") {
          currentScriptName = name;
          try { localStorage.setItem(SCRIPT_NAME_KEY, name); } catch (e) { /* ignore */ }
          toast("Saved: " + name, "info");
          refreshScriptList();
        }
      });
    });
  }

  if (btnLoad) {
    btnLoad.addEventListener("click", function () {
      if (!selLoad) return;
      const name = selLoad.value;
      if (!name) { toast("Select a script first", "error"); return; }
      api("scripts/" + encodeURIComponent(name), null, "GET").then(function (res) {
        if (res.status === "ok" && editor) {
          editor.value = res.code;
          currentScriptName = name;
          try {
            localStorage.setItem(SCRIPT_DRAFT_KEY, res.code);
            localStorage.setItem(SCRIPT_NAME_KEY, name);
          } catch (e) { /* ignore */ }
          updateLineNumbers();
          toast("Loaded: " + name, "info");
        }
      });
    });
  }

  if (btnDelete) {
    btnDelete.addEventListener("click", function () {
      if (!selLoad) return;
      const name = selLoad.value;
      if (!name) { toast("Select a script first", "error"); return; }
      showConfirm("Delete Python Script", "Delete \"" + name + "\"? This cannot be undone.", function () {
        api("scripts/" + encodeURIComponent(name), null, "DELETE").then(function (res) {
          if (res.status === "ok") {
            if (currentScriptName === name) {
              currentScriptName = "";
              try { localStorage.removeItem(SCRIPT_NAME_KEY); } catch (e) { /* ignore */ }
            }
            toast("Deleted: " + name, "info");
            refreshScriptList();
          }
        });
      });
    });
  }

  /* ── Templates ── */
  const TEMPLATES = {
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
      const key = selTemplate.value;
      if (!key || !TEMPLATES[key]) { toast("Select a template first", "error"); return; }
      if (editor.value.trim()) {
        showConfirm("Load Template", "Replace current editor contents with template?", function () {
          editor.value = TEMPLATES[key];
          try { localStorage.setItem(SCRIPT_DRAFT_KEY, editor.value); } catch (e) { /* ignore */ }
          updateLineNumbers();
        }, "Replace", true);
      } else {
        editor.value = TEMPLATES[key];
        try { localStorage.setItem(SCRIPT_DRAFT_KEY, editor.value); } catch (e) { /* ignore */ }
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
  if (navBtn) {
    navBtn.addEventListener("click", function () {
      refreshScriptList();
    });
  }

  // Initial load if enabled
  try {
    if (localStorage.getItem(SCRIPT_KEY) === "1") refreshScriptList();
  } catch (e) { /* ignore */ }
}());
