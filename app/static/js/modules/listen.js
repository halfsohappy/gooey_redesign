/* ── Reply listener (recv start/stop) ── */

import { $ } from "./state.js";
import { api } from "./api.js";
import { toast } from "./toast.js";
import { activeViews, updatePanelLayout } from "./panel-resize.js";

let isListening = false;
let _listenPort = 9000;

export function startListen(port) {
  port = parseInt(port, 10) || 9000;
  const doStart = function () {
    api("recv/start", { port: port, id: "reply-listener" }).then(function (res) {
      if (res.status === "ok") {
        isListening = true;
        _listenPort = port;
        $("#listenPortDisplay").textContent = port;
        $("#replyPort").value = port;
        $("#listenDot").classList.add("on");
      } else {
        toast("Listen failed: " + (res.message || ""), "error");
      }
    });
  };
  if (isListening) {
    /* Stop previous listener first, then restart on new port */
    api("recv/stop", { id: "reply-listener" }).then(function () {
      isListening = false;
      doStart();
    });
  } else {
    doStart();
  }
}

/* Apply port button: restart listener on the entered port */
$("#btnApplyPort").addEventListener("click", function () {
  const port = parseInt($("#replyPort").value, 10) || 9000;
  if (port === _listenPort && isListening) {
    toast("Already listening on port " + port, "info");
    return;
  }
  startListen(port);
  toast("Listening on port " + port, "success");
});

/* Auto-start listening on page load, show feed */
startListen(9000);
activeViews.feed = true;
updatePanelLayout();
