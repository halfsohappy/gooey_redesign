/* ==============================================
   Gooey — Control Center Frontend  (ES Module entry point)
   Multi-device · registry-based · table-driven

   This file imports every feature module and wires
   up the cross-module callbacks that break circular
   dependencies.
   ============================================== */

/* ── Core (state, utilities, UI chrome) ── */
import "./modules/state.js";
import "./modules/theme.js";
import "./modules/navigation.js";
import "./modules/tooltip.js";

/* ── Networking & commands ── */
import { api } from "./modules/api.js";
import { toast } from "./modules/toast.js";
import {
  sendCmd, addr, sendFlush, _onFlushReply,
  CMD_ADDRESSES, openDevSettingsModal, getStatusConfigTargets,
} from "./modules/command.js";

/* ── Devices ── */
import {
  getActiveDev, devHost, devPort, devName,
  renderDeviceTabs, setDeviceCallbacks,
  restoreDevicesFromStorage,
} from "./modules/device-manager.js";
import {
  sendToDevice, openDevDropdown, dropdownDevice,
  setDeviceConfigCallbacks,
} from "./modules/device-config.js";

/* ── Registry & feed ── */
import {
  parseReplyIntoRegistry, parseConfigString,
  setParserCallbacks,
} from "./modules/registry-parser.js";
import {
  esc, appendToFeed, setFeedCallbacks,
} from "./modules/feed.js";

/* ── Messages ── */
import {
  renderMsgTable, populateMsgForm, msgAction,
  getMsgSceneFilter, registerString,
  refreshGateMsgSources,
} from "./modules/message-manager.js";

/* ── Scenes ── */
import { renderSceneTable, populateSceneForm } from "./modules/scene-manager.js";

/* ── Orientations ── */
import {
  renderOriTable, oriAction, showOriDetails,
  setOriManagerCallbacks,
} from "./modules/ori-manager.js";

/* ── Dropdowns ── */
import { refreshAllDropdowns } from "./modules/dropdown-coordinator.js";

/* ── Dashboard ── */
import "./modules/dashboard.js";

/* ── Forms ── */
import "./modules/message-form.js";
import "./modules/scene-form.js";

/* ── Calibrate ── */
import "./modules/calibrate.js";

/* ── Panel resize ── */
import "./modules/panel-resize.js";

/* ── Listen port ── */
import "./modules/listen.js";

/* ── Demo mode ── */
import "./modules/demo.js";

/* ── Verbose mode ── */
import "./modules/verbose.js";

/* ── Ori controls ── */
import "./modules/ori-controls.js";

/* ── Shows ── */
import { renderShowDeviceTable } from "./modules/show-manager.js";

/* ── Validation ── */
import { expandIp } from "./modules/field-validation.js";

/* ── Column picker ── */
import "./modules/column-picker.js";

/* ── Serial terminal ── */
import "./modules/serial.js";

/* ── Network settings ── */
import "./modules/network-settings.js";

/* ── Direct send (Raw OSC + Bridge) ── */
import "./modules/direct-send.js";

/* ── Guided tour ── */
import "./modules/guided-tour.js";

/* ── Script console ── */
import "./modules/script-console.js";

/* ── Reference panel ── */
import "./modules/reference.js";


/* ═══════════════════════════════════════════
   CROSS-MODULE CALLBACK WIRING

   Several modules use a late-binding callback
   pattern to avoid circular import dependencies.
   Wire them all up here once every module has
   been loaded.
   ═══════════════════════════════════════════ */

setDeviceCallbacks({
  renderMsgTable,
  renderSceneTable,
  renderOriTable,
  refreshAllDropdowns,
  refreshQueryDeviceSelect() {},   // stub — not used in current UI
  openDevDropdown,
});

setDeviceConfigCallbacks({
  renderMsgTable,
  renderSceneTable,
  renderOriTable,
});

setParserCallbacks({
  renderMsgTable,
  renderSceneTable,
  renderOriTable,
  refreshAllDropdowns,
  renderShowDeviceTable,
  showOriDetails,
});

setFeedCallbacks({
  getMsgSceneFilter,
  onStrRegistered(name) {
    /* feed.js calls this when a /str/register reply arrives */
    toast("String registered: " + name, "info");
  },
});

setOriManagerCallbacks({
  expandIp,
});

/* ── V1 tracker: wire edit actions via custom events (avoids circular deps) ── */
document.addEventListener('v1:editScene', function (e) {
  populateSceneForm(e.detail.name, e.detail.data);
});
document.addEventListener('v1:editMsg', function (e) {
  populateMsgForm(e.detail.name, e.detail.data);
});

/* ── Restore saved devices on startup ── */
restoreDevicesFromStorage();
