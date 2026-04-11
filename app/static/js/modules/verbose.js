/* ── Verbose mode — confirm before send (per-device) ── */

import { getActiveDev } from "./device-manager.js";
import { showConfirm } from "./toast.js";

export function isVerboseMode() {
  const dev = getActiveDev();
  return dev && dev.verbose;
}

/**
 * Wrap sendCmd and api functions with verbose-mode confirmation dialogs.
 * Returns { sendCmd, api } — the wrapped versions.
 */
export function wrapWithVerbose(sendCmdFn, apiFn) {
  const wrappedSendCmd = function (address, payload) {
    if (isVerboseMode()) {
      return new Promise(function (resolve) {
        const msgText = "Address:\n" + address + "\n\nPayload:\n" + (payload || "(none)");
        showConfirm("Verbose \u2014 Send OSC?", msgText, function () {
          resolve(sendCmdFn(address, payload));
        }, "Send", false);
        document.getElementById("confirmCancel").addEventListener("click", function () {
          resolve({ status: "cancelled" });
        }, { once: true });
      });
    }
    return sendCmdFn(address, payload);
  };

  const wrappedApi = function (endpoint, data, method) {
    if (endpoint === "send" && data && isVerboseMode()) {
      return new Promise(function (resolve) {
        const msgText = "Address:\n" + (data.address || "") + "\n\nPayload:\n" + JSON.stringify(data.args || "") + "\n\nHost: " + (data.host || "") + ":" + (data.port || "");
        showConfirm("Verbose \u2014 Send OSC?", msgText, function () {
          resolve(apiFn(endpoint, data, method));
        }, "Send", false);
        document.getElementById("confirmCancel").addEventListener("click", function () {
          resolve({ status: "cancelled" });
        }, { once: true });
      });
    }
    return apiFn(endpoint, data, method);
  };

  return { sendCmd: wrappedSendCmd, api: wrappedApi };
}
