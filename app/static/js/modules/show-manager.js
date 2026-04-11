/* ── Show Management — device shows and library shows ── */

import { $ } from './state.js';
import { toast, showConfirm } from './toast.js';
import { sendCmd, addr, sendFlush } from './command.js';
import { getActiveDev } from './device-manager.js';
import { esc } from './feed.js';

/* ═══════════════════════════════════════════
   SHOW MANAGEMENT
   ═══════════════════════════════════════════ */

export function renderShowDeviceTable(names) {
  const tbody = $("#showDeviceTableBody");
  if (!tbody) return;
  if (!names || names.length === 0) {
    tbody.innerHTML = '<tr><td colspan="2"><div class="empty-state"><div class="empty-icon">◑</div><div class="empty-text">No shows saved on device.</div></div></td></tr>';
    return;
  }
  tbody.innerHTML = "";
  names.forEach(function (name) {
    name = name.trim();
    if (!name) return;
    const tr = document.createElement("tr");
    tr.innerHTML =
      '<td>' + esc(name) + '</td>' +
      '<td class="cell-actions">' +
        '<button class="tbl-btn tbl-btn-primary" data-act="load" title="Load this show (requires confirm)">Load</button>' +
        '<button class="tbl-btn tbl-btn-danger" data-act="delete" title="Delete show from device">×</button>' +
      '</td>';
    tr.querySelectorAll(".tbl-btn").forEach(function (btn) {
      btn.addEventListener("click", function () {
        if (btn.dataset.act === "load") {
          if (!getActiveDev()) { toast("Select a device first", "error"); return; }
          showConfirm(
            "Load Show '" + name + "'",
            "Loading replaces all current messages, scenes, and oris on the device. Continue?",
            function () {
              sendCmd(addr("/annieData/{device}/show/load/" + name), null).then(function () {
                sendCmd(addr("/annieData/{device}/show/load/confirm"), null).then(function (res) {
                  if (res && res.status === "ok") {
                    toast("Loaded show: " + name, "success");
                    sendCmd(addr("/annieData/{device}/list/all"), null);
                    sendCmd(addr("/annieData/{device}/ori/list"), null);
                  }
                });
              });
            },
            "Load", true
          );
        } else if (btn.dataset.act === "delete") {
          showConfirm("Delete Show '" + name + "'", "Delete '" + name + "' from device NVS?", function () {
            sendCmd(addr("/annieData/{device}/show/delete/" + name), null).then(function (res) {
              if (res && res.status === "ok") {
                toast("Deleted: " + name, "success");
                sendCmd(addr("/annieData/{device}/show/list"), null);
              }
            });
          }, "Delete", true);
        }
      });
    });
    tbody.appendChild(tr);
  });
}

export function renderShowLibraryTable(shows) {
  const tbody = $("#showLibraryTableBody");
  if (!tbody) return;
  if (!shows || shows.length === 0) {
    tbody.innerHTML = '<tr><td colspan="3"><div class="empty-state"><div class="empty-icon">◑</div><div class="empty-text">No shows in library.</div></div></td></tr>';
    return;
  }
  tbody.innerHTML = "";
  shows.forEach(function (s) {
    const tr = document.createElement("tr");
    const savedDate = s.saved ? s.saved.replace("T", " ").substr(0, 16) : "—";
    tr.innerHTML =
      '<td>' + esc(s.name) + '</td>' +
      '<td class="cell-mono" style="font-size:0.8em">' + esc(savedDate) + '</td>' +
      '<td class="cell-actions">' +
        '<button class="tbl-btn tbl-btn-primary" data-act="load" title="Push show to device">Load to Device</button>' +
        '<button class="tbl-btn tbl-btn-danger" data-act="delete" title="Delete from library">×</button>' +
      '</td>';
    tr.querySelectorAll(".tbl-btn").forEach(function (btn) {
      btn.addEventListener("click", function () {
        if (btn.dataset.act === "load") {
          if (!getActiveDev()) { toast("Select a device first", "error"); return; }
          showConfirm(
            "Load Library Show '" + s.name + "'",
            "Push all messages, scenes, and oris to device and save as show '" + s.name + "'?",
            function () {
              fetch("/api/shows/" + encodeURIComponent(s.name)).then(function (r) { return r.json(); })
                .then(function (showData) {
                  _pushLibraryShowToDevice(showData);
                })
                .catch(function (e) { toast("Failed to read library show: " + e, "error"); });
            },
            "Load", true
          );
        } else if (btn.dataset.act === "delete") {
          showConfirm("Delete '" + s.name + "' from Library", "This cannot be undone.", function () {
            fetch("/api/shows/" + encodeURIComponent(s.name), { method: "DELETE" })
              .then(function () { _refreshShowLibrary(); toast("Deleted: " + s.name, "info"); })
              .catch(function (e) { toast("Delete failed: " + e, "error"); });
          }, "Delete", true);
        }
      });
    });
    tbody.appendChild(tr);
  });
}

export function _refreshShowLibrary() {
  fetch("/api/shows").then(function (r) { return r.json(); })
    .then(function (data) { renderShowLibraryTable(data || []); })
    .catch(function (e) { console.warn("Library fetch failed:", e); });
}

function _pushLibraryShowToDevice(showData) {
  if (!showData) return;
  const name = showData.name || "imported";
  /* Push messages */
  (showData.messages || []).forEach(function (m) {
    let cfg = "value:" + m.value + ",ip:" + m.ip + ",port:" + m.port + ",adr:" + m.adr;
    if (m.low  !== undefined) cfg += ",low:" + m.low;
    if (m.high !== undefined) cfg += ",high:" + m.high;
    sendCmd(addr("/annieData/{device}/msg/" + m.name), '"' + cfg + '"');
  });
  /* Push scenes */
  (showData.scenes || []).forEach(function (p) {
    const cfg = "ip:" + p.ip + ",port:" + p.port + ",period:" + p.period;
    sendCmd(addr("/annieData/{device}/scene/" + p.name), '"' + cfg + '"');
    (p.msgs || []).forEach(function (mName) {
      sendCmd(addr("/annieData/{device}/scene/" + p.name + "/addMsg"), '"' + mName + '"');
    });
  });
  /* Push oris */
  (showData.oris || []).forEach(function (o) {
    const rgb = (o.color || [255, 255, 255]);
    const colorStr = '"' + rgb[0] + "," + rgb[1] + "," + rgb[2] + '"';
    sendCmd(addr("/annieData/{device}/ori/register/" + o.name), colorStr);
    if (o.quaternions && o.quaternions.length > 0) {
      o.quaternions.forEach(function () {
        sendCmd(addr("/annieData/{device}/ori/save/" + o.name), null);
      });
    }
  });
  /* Wait for device to finish processing all commands, then save as show */
  sendFlush().then(function () {
    sendCmd(addr("/annieData/{device}/show/save/" + name), null).then(function () {
      toast("Pushed library show '" + name + "' to device", "success");
      sendCmd(addr("/annieData/{device}/list/all"), null);
      sendCmd(addr("/annieData/{device}/ori/list"), null);
      sendCmd(addr("/annieData/{device}/show/list"), null);
    });
  });
}

/* Wire up show buttons */
(function () {
  const btnSaveDevice  = $("#btnShowSaveDevice");
  const btnSaveLibrary = $("#btnShowSaveLibrary");
  const btnListDevice  = $("#btnShowListDevice");
  const btnListLibrary = $("#btnShowListLibrary");

  if (btnSaveDevice) btnSaveDevice.addEventListener("click", function () {
    const name = ($("#showSaveName").value || "").trim();
    if (!name) { toast("Show name required", "error"); return; }
    if (!getActiveDev()) { toast("Select a device first", "error"); return; }
    sendCmd(addr("/annieData/{device}/show/save/" + name), null).then(function (res) {
      if (res && res.status === "ok") {
        toast("Saved show on device: " + name, "success");
        sendCmd(addr("/annieData/{device}/show/list"), null);
      }
    });
  });

  if (btnSaveLibrary) btnSaveLibrary.addEventListener("click", function () {
    const name = ($("#showSaveName").value || "").trim();
    if (!name) { toast("Show name required", "error"); return; }
    const dev = getActiveDev();
    if (!dev) { toast("Select a device first", "error"); return; }
    /* Build show JSON from current Gooey registry */
    const msgs = Object.entries(dev.messages || {}).map(function (kv) { return Object.assign({ name: kv[0] }, kv[1]); });
    const scenes = Object.entries(dev.scenes || {}).map(function (kv) { return Object.assign({ name: kv[0] }, kv[1]); });
    const oris = Object.entries(dev.oris || {}).map(function (kv) {
      return { name: kv[0], color: kv[1].color, samples: kv[1].samples, use_axis: kv[1].useAxis };
    });
    const payload = {
      name: name,
      saved: new Date().toISOString(),
      device: dev.name,
      messages: msgs,
      scenes: scenes,
      oris: oris
    };
    fetch("/api/shows/" + encodeURIComponent(name), {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    }).then(function (r) { return r.json(); })
      .then(function () {
        toast("Saved to library: " + name, "success");
        _refreshShowLibrary();
      })
      .catch(function (e) { toast("Library save failed: " + e, "error"); });
  });

  if (btnListDevice) btnListDevice.addEventListener("click", function () {
    if (!getActiveDev()) { toast("Select a device first", "error"); return; }
    sendCmd(addr("/annieData/{device}/show/list"), null);
  });

  if (btnListLibrary) btnListLibrary.addEventListener("click", function () {
    _refreshShowLibrary();
  });
}());
