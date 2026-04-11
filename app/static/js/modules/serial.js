/* ── Serial terminal — port listing, connect/disconnect, send/receive ── */

import { socket, $ } from "./state.js";
import { toast } from "./toast.js";

(function () {
  const terminal   = $("#serialTerminal");
  const portSelect = $("#serialPortSelect");
  const baudSelect = $("#serialBaudSelect");
  const btnConnect = $("#btnSerialConnect");
  const btnClear   = $("#btnSerialClear");
  const btnRefresh = $("#btnSerialRefreshPorts");
  let _connected   = false;

  if (!terminal) return;

  function refreshPorts() {
    socket.emit("serial_list_ports");
  }

  socket.on("serial_ports", function (data) {
    portSelect.innerHTML = "";
    if (!data.ports || data.ports.length === 0) {
      portSelect.innerHTML = '<option value="">No ports found</option>';
      return;
    }
    data.ports.forEach(function (p) {
      const opt = document.createElement("option");
      opt.value = p.device;
      opt.textContent = p.device + (p.description && p.description !== p.device ? " — " + p.description : "");
      portSelect.appendChild(opt);
    });
  });

  function appendLine(text, cls) {
    const placeholder = terminal.querySelector(".serial-placeholder");
    if (placeholder) placeholder.remove();
    const line = document.createElement("div");
    line.className = "serial-line" + (cls ? " " + cls : "");
    line.textContent = text;
    terminal.appendChild(line);
    // keep last 500 lines
    while (terminal.children.length > 500) terminal.removeChild(terminal.firstChild);
    terminal.scrollTop = terminal.scrollHeight;
  }

  socket.on("serial_data", function (data) {
    (data.data || "").split(/\r?\n/).forEach(function (line) {
      if (line) appendLine(line);
    });
  });

  socket.on("serial_connected", function (data) {
    _connected = true;
    btnConnect.textContent = "Disconnect";
    btnConnect.classList.add("connected");
    appendLine("── Connected: " + data.port + " @ " + data.baud + " baud ──");
  });

  socket.on("serial_disconnected", function () {
    _connected = false;
    btnConnect.textContent = "Connect";
    btnConnect.classList.remove("connected");
    appendLine("── Disconnected ──");
  });

  socket.on("serial_error", function (data) {
    appendLine("Error: " + (data.message || "unknown"), "stderr");
  });

  btnConnect.addEventListener("click", function () {
    if (_connected) {
      socket.emit("serial_disconnect_port");
    } else {
      const port = portSelect.value;
      if (!port) { toast("Select a serial port first", "warn"); return; }
      socket.emit("serial_connect", { port: port, baud: parseInt(baudSelect.value, 10) });
    }
  });

  btnRefresh.addEventListener("click", refreshPorts);

  btnClear.addEventListener("click", function () {
    terminal.innerHTML = "";
  });

  /* ── Send command ── */
  const sendInput = $("#serialSendInput");
  const btnSend   = $("#btnSerialSend");

  function doSend() {
    if (!_connected) { toast("Not connected to serial port", "warn"); return; }
    const val = sendInput.value;
    if (!val) return;
    socket.emit("serial_send", { data: val });
    appendLine("> " + val);
    sendInput.value = "";
    sendInput.focus();
  }

  if (btnSend) btnSend.addEventListener("click", doSend);
  if (sendInput) {
    sendInput.addEventListener("keydown", function (e) {
      if (e.key === "Enter") { e.preventDefault(); doSend(); }
    });
  }
}());
