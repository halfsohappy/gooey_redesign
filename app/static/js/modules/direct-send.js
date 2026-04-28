/* ── Direct Send — Raw OSC send-once/repeat and Bridge controls ── */

import { $ } from "./state.js";
import { api } from "./api.js";
import { toast } from "./toast.js";

(function () {
  /* ─────────────────────────────
     RAW OSC
     ───────────────────────────── */

  var _repeatId = null;

  function getRawArgs() {
    var raw = ($("#rawArgs").value || "").trim() || null;
    if (raw && $("#rawSingleString") && $("#rawSingleString").checked) {
      return [raw];   /* single-string mode — wrap in array so backend doesn't split */
    }
    return raw;
  }

  var btnRawSend   = $("#btnRawSend");
  var btnRawRepeat = $("#btnRawRepeat");
  var btnRawStop   = $("#btnRawStop");

  if (btnRawSend) {
    btnRawSend.addEventListener("click", function () {
      api("send", {
        host:    ($("#rawHost").value    || "").trim(),
        port:    parseInt($("#rawPort").value, 10),
        address: ($("#rawAddress").value || "").trim(),
        args:    getRawArgs(),
      }).then(function (res) {
        if (res.status === "ok") toast("Sent", "success");
      });
    });
  }

  if (btnRawRepeat) {
    btnRawRepeat.addEventListener("click", function () {
      api("send/repeat", {
        host:     ($("#rawHost").value    || "").trim(),
        port:     parseInt($("#rawPort").value, 10),
        address:  ($("#rawAddress").value || "").trim(),
        args:     getRawArgs(),
        interval: parseInt($("#rawInterval").value, 10) || 1000,
        id:       "raw-repeat",
      }).then(function (res) {
        if (res.status === "ok") {
          _repeatId = "raw-repeat";
          btnRawRepeat.disabled = true;
          if (btnRawStop) btnRawStop.disabled = false;
          toast("Repeat started", "success");
        }
      });
    });
  }

  if (btnRawStop) {
    btnRawStop.addEventListener("click", function () {
      api("send/stop", { id: _repeatId || "raw-repeat" }).then(function () {
        _repeatId = null;
        if (btnRawRepeat) btnRawRepeat.disabled = false;
        btnRawStop.disabled = true;
        toast("Repeat stopped", "info");
      });
    });
  }

  /* ─────────────────────────────
     BRIDGE
     ───────────────────────────── */

  function refreshBridgeList() {
    fetch("/api/status")
      .then(function (r) { return r.json(); })
      .then(function (data) {
        var container = $("#activeBridges");
        if (!container) return;
        container.innerHTML = "";
        var bridges = data.bridges || {};
        Object.keys(bridges).forEach(function (id) {
          var b = bridges[id];
          var div = document.createElement("div");
          div.className = "active-item";
          div.innerHTML =
            '<span><span class="active-item-dot"></span>' +
            b.in_port + " \u2192 " + b.out_host + ":" + b.out_port +
            "</span>";
          container.appendChild(div);
        });
      })
      .catch(function () { /* ignore */ });
  }

  var btnBridgeStart = $("#btnBridgeStart");
  var btnBridgeStop  = $("#btnBridgeStop");

  if (btnBridgeStart) {
    btnBridgeStart.addEventListener("click", function () {
      api("bridge/start", {
        in_port:  parseInt($("#bridgeInPort").value, 10),
        out_host: ($("#bridgeOutHost").value || "").trim(),
        out_port: parseInt($("#bridgeOutPort").value, 10),
        filter:   ($("#bridgeFilter").value  || "").trim(),
      }).then(function (res) {
        if (res.status === "ok") {
          toast("Bridge started", "success");
          if (btnBridgeStop) btnBridgeStop.disabled = false;
          refreshBridgeList();
        } else {
          toast("Bridge error: " + (res.message || "unknown"), "error");
        }
      });
    });
  }

  if (btnBridgeStop) {
    btnBridgeStop.addEventListener("click", function () {
      api("stop-all", {}).then(function () {
        toast("All bridges stopped", "info");
        btnBridgeStop.disabled = true;
        var bridgesEl = $("#activeBridges");
        if (bridgesEl) bridgesEl.innerHTML = "";
      });
    });
  }
}());
