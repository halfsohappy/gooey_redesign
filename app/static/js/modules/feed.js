/* ── Log / Feed — live OSC message display and realtime handler ── */

import { socket, $, MAX_LOG_ENTRIES, devices, activeDevice } from "./state.js";
import { parseReplyIntoRegistry, parseConfigString } from "./registry-parser.js";
import { _onFlushReply } from "./command.js";
import { showToast, toast } from "./toast.js";

/* ── Late-binding callbacks (set by other modules to avoid circular deps) ── */
let _getMsgSceneFilter = function () { return null; };
let _onStrRegistered = null;

export function setFeedCallbacks({ getMsgSceneFilter, onStrRegistered }) {
  if (getMsgSceneFilter) _getMsgSceneFilter = getMsgSceneFilter;
  if (onStrRegistered) _onStrRegistered = onStrRegistered;
}

/* ── HTML-escape helper ── */

export function esc(s) {
  const d = document.createElement("div");
  d.textContent = s;
  return d.innerHTML;
}

/* ── Log entry rendering ── */

export function renderLogEntry(entry) {
  const div = document.createElement("div");
  div.className = "log-entry";
  const tagClass = { send: "log-tag-send", recv: "log-tag-recv", bridge: "log-tag-bridge" };
  const argsStr = (entry.args || []).map(function (a) {
    if (a.type === "s") return '"' + a.value + '"';
    return a.value;
  }).join(" ");
  let destInfo = "";
  if (entry.dest) destInfo = " → " + entry.dest;
  if (entry.source && entry.dest) destInfo = " " + entry.source + " → " + entry.dest;
  else if (entry.source) destInfo = " ← " + entry.source;
  /* Determine device tag */
  let deviceTag = "";
  Object.keys(devices).forEach(function (id) {
    const d = devices[id];
    if (!entry.address) return;
    const seg = "/" + d.name + "/";
    const tail = "/" + d.name;
    if (entry.address.indexOf(seg) !== -1 || entry.address.slice(-tail.length) === tail) {
      deviceTag = '<span class="log-device-tag">' + d.name + '</span> ';
    }
  });
  div.innerHTML = [
    '<span class="log-time">' + entry.time + '</span>',
    '<span class="log-tag ' + (tagClass[entry.direction] || "") + '">' + entry.direction + '</span>',
    deviceTag,
    '<span class="log-address">' + esc(entry.address) + '</span>',
    argsStr ? '<span class="log-args">(' + esc(argsStr) + ')</span>' : '',
    destInfo ? '<span class="log-dest">' + esc(destInfo) + '</span>' : '',
  ].join("");
  return div;
}

/* ── Append to live feed ── */

export function appendToFeed(entry) {
  /* Device filter */
  const devFilter = ($("#feedDeviceFilter").value || "").trim();
  if (devFilter && entry.address) {
    const seg = "/" + devFilter + "/";
    const tail = "/" + devFilter;
    if (entry.address.indexOf(seg) === -1 && entry.address.slice(-tail.length) !== tail) return;
  }

  /* Text filter */
  const filterText = ($("#feedFilter").value || "").trim().toLowerCase();
  if (filterText) {
    const fullText = entry.address + " " + JSON.stringify(entry.args);
    if (fullText.toLowerCase().indexOf(filterText) === -1) return;
  }

  const feedEl = $("#feedLog");
  feedEl.appendChild(renderLogEntry(entry));
  while (feedEl.children.length > MAX_LOG_ENTRIES) {
    feedEl.removeChild(feedEl.firstChild);
  }
  if ($("#feedAutoScroll").checked) {
    feedEl.scrollTop = feedEl.scrollHeight;
  }
}

/* ── Message counter ── */
let msgCount = 0;
let rateCounter = 0;
let lastRateCheck = Date.now();

/* ── Clear feed button ── */
const btnFeedClear = $("#btnFeedClear");
if (btnFeedClear) {
  btnFeedClear.addEventListener("click", function () {
    const feedEl = $("#feedLog");
    if (feedEl) feedEl.innerHTML = "";
    msgCount = 0;
    rateCounter = 0;
    const countEl = $("#feedCount");
    const rateEl  = $("#feedRate");
    if (countEl) countEl.textContent = "0 messages";
    if (rateEl)  rateEl.textContent  = "0 msg/s";
  });
}

setInterval(function () {
  const now = Date.now();
  const elapsed = (now - lastRateCheck) / 1000;
  const rate = elapsed > 0 ? Math.round(rateCounter / elapsed) : 0;
  rateCounter = 0;
  lastRateCheck = now;
  const countEl = $("#feedCount");
  const rateEl = $("#feedRate");
  if (countEl) countEl.textContent = msgCount + " messages";
  if (rateEl) rateEl.textContent = rate + " msg/s";
}, 1000);

/* ── Realtime messages ── */
socket.on("osc_message", function (entry) {
  /* String registration reply: /reply/{device}/str/registered */
  if (entry.address && entry.address.indexOf("/str/registered") >= 0) {
    const strName = (entry.args && entry.args[0]) ? String(entry.args[0]) : "";
    if (_onStrRegistered && strName) {
      _onStrRegistered(strName);
    }
    return;
  }
  msgCount++;
  rateCounter++;
  appendToFeed(entry);
  /* Auto-parse replies into registry */
  let prevMsgCount = 0, prevSceneCount = 0;
  let matchedDevId = "";
  Object.keys(devices).forEach(function (id) {
    const d = devices[id];
    if (!entry.address) return;
    const seg = "/" + d.name + "/";
    const tail = "/" + d.name;
    if (entry.address.indexOf(seg) !== -1 || entry.address.slice(-tail.length) === tail) matchedDevId = id;
  });
  if (!matchedDevId) matchedDevId = activeDevice.id;
  if (matchedDevId && devices[matchedDevId] && /\/list\/(all|msgs|messages)/i.test(entry.address || "")) {
    const preDev = devices[matchedDevId];
    prevMsgCount = Object.keys(preDev.messages || {}).length;
    prevSceneCount = Object.keys(preDev.scenes || {}).length;
  }
  parseReplyIntoRegistry(entry);
  _onFlushReply(entry);
  /* Show query feedback toast after list/all replies add new data */
  if (matchedDevId && devices[matchedDevId] && /\/list\/(all|msgs|messages)/i.test(entry.address || "")) {
    const postDev = devices[matchedDevId];
    const newMsgCount = Object.keys(postDev.messages || {}).length;
    const newSceneCount = Object.keys(postDev.scenes || {}).length;
    if ((newMsgCount > 0 || newSceneCount > 0) && (newMsgCount !== prevMsgCount || newSceneCount !== prevSceneCount)) {
      showToast("Loaded " + newMsgCount + " message" + (newMsgCount !== 1 ? "s" : "") + " and " + newSceneCount + " scene" + (newSceneCount !== 1 ? "es" : "") + " from device.", "success");
    }
  }
});
