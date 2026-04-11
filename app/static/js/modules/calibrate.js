/* ── Calibrate & Zero Modal — tare actions, euler order, status query ── */

import { $  } from './state.js';
import { devices } from './state.js';
import { toast } from './toast.js';
import { sendCmd } from './command.js';
import { dropdownDevice, closeDevDropdown } from './device-config.js';

(function () {
  const modal     = $("#tareModal");
  const fb        = $("#tareFeedbackImu");
  const advStatus = $("#tareAdvStatus");

  function openTareModal() {
    const d = devices[dropdownDevice.id];
    if (!d) return;
    $("#tareModalDevice").textContent = d.name;
    setFeedback("", "");
    advStatus.textContent = "";
    advStatus.classList.remove("visible");
    modal.classList.remove("hidden");
  }

  function closeTareModal() { modal.classList.add("hidden"); }

  function setFeedback(msg, type) {
    fb.textContent = msg;
    fb.className = "tare-feedback" + (type ? " " + type : "");
  }

  const TARE_ACTIONS = {
    "avg":              { path: "/tare/avg",             args: "20", working: "Zeroing…", okMsg: "✓ Orientation zeroed" },
    "swingtwist":       { path: "/tare/swingtwist",      args: null, okMsg: "✓ All arm axes zeroed" },
    "twist":            { path: "/tare/twist",           args: null, okMsg: "✓ Wrist rotation zeroed" },
    "azimuth":          { path: "/tare/azimuth",         args: null, okMsg: "✓ Pointing direction zeroed" },
    "tilt":             { path: "/tare/tilt",            args: null, okMsg: "✓ Vertical tilt zeroed" },
    "swingtwist/reset": { path: "/tare/swingtwist/reset",args: null, okMsg: "✓ Arm zeros cleared" }
  };

  modal.addEventListener("click", function (e) {
    const btn = e.target.closest("[data-tare-action]");
    if (!btn) return;
    const cfg = TARE_ACTIONS[btn.dataset.tareAction];
    if (!cfg || !dropdownDevice.id) return;

    if (cfg.working) setFeedback(cfg.working, "working");
    btn.disabled = true;

    sendCmd("/annieData/" + dropdownDevice.id + cfg.path, cfg.args || null).then(function (res) {
      btn.disabled = false;
      if (res && res.status === "ok") {
        setFeedback(cfg.okMsg, "ok");
        setTimeout(function () { setFeedback("", ""); }, 3000);
      } else {
        setFeedback("Command failed — is the device connected?", "err");
      }
    }).catch(function () {
      btn.disabled = false;
      setFeedback("No response — check device connection", "err");
    });
  });

  /* Euler order apply */
  $("#tareBtnEulerApply").addEventListener("click", function () {
    const val = $("#tareEulerOrder").value;
    sendCmd("/annieData/" + dropdownDevice.id + "/tare/order", val).then(function (res) {
      if (res && res.status === "ok") toast("Euler order set to " + val, "success");
    });
  });

  /* Status query */
  $("#tareBtnStatus").addEventListener("click", function () {
    advStatus.textContent = "Querying…";
    advStatus.classList.add("visible");
    sendCmd("/annieData/" + dropdownDevice.id + "/tare/status", null).then(function (res) {
      if (res && res.reply) {
        advStatus.textContent = res.reply;
      } else if (res && res.status === "ok") {
        advStatus.textContent = "(No status text returned — device may be on older firmware)";
      } else {
        advStatus.textContent = "No response — check device connection";
      }
    }).catch(function () {
      advStatus.textContent = "No response — check device connection";
    });
  });

  /* Open / close handlers */
  $("#devDdOpenTare").addEventListener("click", function () {
    openTareModal();
    closeDevDropdown();
  });
  $("#tareModalDone").addEventListener("click", closeTareModal);
  modal.addEventListener("click", function (e) {
    if (e.target === modal) closeTareModal();
  });
}());
