/* ── Orientation Controls — ori button handlers, IMU calibration card ── */

import { $ } from './state.js';
import { toast, showConfirm } from './toast.js';
import { sendCmd, addr } from './command.js';
import { getActiveDev } from './device-manager.js';
import { renderOriTable } from './ori-manager.js';
import { refreshAllDropdowns } from './dropdown-coordinator.js';

/* ═══════════════════════════════════════════
   ORI CONTROLS
   ═══════════════════════════════════════════ */

const oriButtons = {
  btnOriSave: function () {
    const name = ($("#oriName").value || "").trim();
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
    const name = ($("#oriName").value || "").trim();
    if (!name) { toast("Ori name required", "error"); return; }
    sendCmd(addr("/annieData/{device}/ori/delete"), name).then(function (res) {
      if (res && res.status === "ok") {
        const dev = getActiveDev();
        if (dev) { delete dev.oris[name]; renderOriTable(); refreshAllDropdowns(); }
      }
    });
  },
  btnOriClear2: function () {
    const name = ($("#oriName").value || "").trim();
    if (!name) { toast("Ori name required", "error"); return; }
    sendCmd(addr("/annieData/{device}/ori/reset/" + name), null).then(function (res) {
      if (res && res.status === "ok") {
        toast("Samples cleared: " + name, "success");
        const dev = getActiveDev();
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
          const dev = getActiveDev();
          if (dev) { dev.oris = {}; renderOriTable(); refreshAllDropdowns(); }
          toast("All oris cleared", "success");
        }
      });
    }, "Clear All", true);
  },
  btnOriThreshold: function () {
    const val = ($("#oriThreshold").value || "").trim();
    if (!val) { toast("Threshold value required", "error"); return; }
    sendCmd(addr("/annieData/{device}/ori/threshold"), '"' + val + '"');
  },
  btnOriHysteresis: function () {
    const val = ($("#oriHysteresis").value || "").trim();
    if (!val) { toast("Hysteresis value required", "error"); return; }
    sendCmd(addr("/annieData/{device}/ori/hysteresis"), '"' + val + '"').then(function (res) {
      if (res && res.status === "ok") toast("Hysteresis set to " + val + "°", "success");
    });
  },
  btnOriTolerance: function () {
    const val = ($("#oriTolerance").value || "").trim();
    if (!val) { toast("Tolerance value required", "error"); return; }
    sendCmd(addr("/annieData/{device}/ori/tolerance"), '"' + val + '"');
  },
  btnOriGeneral: function () {
    const name = ($("#oriGeneralName") ? $("#oriGeneralName").value : "").trim();
    if (!name) { toast("Ori name required", "error"); return; }
    sendCmd(addr("/annieData/{device}/ori/general/" + name), null).then(function (res) {
      if (res && res.status === "ok") toast("General ori: " + name, "success");
    });
  },
  btnOriRemoveGeneral: function () {
    sendCmd(addr("/annieData/{device}/ori/general/none"), null).then(function (res) {
      if (res && res.status === "ok") {
        toast("General ori cleared", "success");
        const el = $("#oriGeneralName");
        if (el) el.value = "";
      }
    });
  },
  btnOriStrict: function () {
    sendCmd(addr("/annieData/{device}/ori/strict"), "on");
  },
  btnOriNearest: function () {
    const el = $("#oriStrict");
    if (el) el.checked = false;
    sendCmd(addr("/annieData/{device}/ori/strict"), "off");
  },
  btnOriWatch: function () {
    const btn = $("#btnOriWatch");
    const watching = btn && btn.classList.contains("watching");
    const next = watching ? "off" : "on";
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
  const el = $("#" + id);
  if (el) {
    el.addEventListener("click", function () {
      oriButtons[id]();
    });
  }
});

/* ═══════════════════════════════════════════
   IMU CALIBRATION CARD (Advanced section)
   ═══════════════════════════════════════════ */

const btnImuScaleSet = $("#btnImuScaleSet");
if (btnImuScaleSet) {
  btnImuScaleSet.addEventListener("click", function () {
    const accel = ($("#imuAccelScale").value || "").trim();
    const gyro  = ($("#imuGyroScale").value || "").trim();
    if (accel) {
      sendCmd(addr("/annieData/{device}/scale/accel"), accel).then(function (res) {
        if (res && res.status === "ok") toast("Accel scale set to ±" + accel + " m/s²", "success");
      });
    }
    if (gyro) {
      sendCmd(addr("/annieData/{device}/scale/gyro"), gyro).then(function (res) {
        if (res && res.status === "ok") toast("Gyro scale set to ±" + gyro + " rad/s", "success");
      });
    }
  });
}

const btnImuScaleQuery = $("#btnImuScaleQuery");
if (btnImuScaleQuery) {
  btnImuScaleQuery.addEventListener("click", function () {
    sendCmd(addr("/annieData/{device}/scale/query"), null).then(function (res) {
      if (res && res.reply) toast("Scales: " + res.reply, "info");
    });
  });
}

const btnImuEulerOrderSet = $("#btnImuEulerOrderSet");
if (btnImuEulerOrderSet) {
  btnImuEulerOrderSet.addEventListener("click", function () {
    const sel = $("#imuEulerOrder");
    const val = sel ? sel.value : "auto";
    sendCmd(addr("/annieData/{device}/tare/order"), val).then(function (res) {
      if (res && res.status === "ok") toast("Euler order: " + val, "success");
    });
  });
}

const btnImuTareStatus = $("#btnImuTareStatus");
if (btnImuTareStatus) {
  btnImuTareStatus.addEventListener("click", function () {
    sendCmd(addr("/annieData/{device}/tare/status"), null).then(function (res) {
      if (res && res.reply) toast(res.reply, "info");
    });
  });
}

/* ═══════════════════════════════════════════
   BRIDGE LIST
   ═══════════════════════════════════════════ */

export function refreshBridgeList() {
  fetch("/api/status").then(function (r) { return r.json(); }).then(function (data) {
    const container = $("#activeBridges");
    container.innerHTML = "";
    const bridges = data.bridges || {};
    Object.keys(bridges).forEach(function (id) {
      const b = bridges[id];
      const div = document.createElement("div");
      div.className = "active-item";
      div.innerHTML = '<span><span class="active-item-dot"></span>' + b.in_port + ' → ' + b.out_host + ':' + b.out_port + '</span>';
      container.appendChild(div);
    });
  });
}
