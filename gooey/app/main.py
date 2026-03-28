"""Flask application with SocketIO for TheaterGWD Control Center."""

import datetime
import json
import os
import re
import threading

import markdown as md_lib
import serial
import serial.tools.list_ports
from flask import Flask, render_template, request, jsonify
from flask_socketio import SocketIO, join_room, leave_room

from .osc_handler import OSCEngine

app = Flask(__name__)
app.config["SECRET_KEY"] = "theatergwd-control-center"
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")
engine = OSCEngine(socketio)

# ── Validation helpers ──

_IP_RE = re.compile(
    r"^(?:(?:25[0-5]|2[0-4]\d|[01]?\d\d?)\.){3}"
    r"(?:25[0-5]|2[0-4]\d|[01]?\d\d?)$"
)


def _resolve_host(host):
    h = host.strip()
    if h.lower() == "localhost":
        return "127.0.0.1"
    return h


def _valid_host(host):
    return bool(_IP_RE.match(_resolve_host(host)))


def _valid_port(port):
    try:
        return 1 <= int(port) <= 65535
    except (ValueError, TypeError):
        return False


def _valid_address(addr):
    return isinstance(addr, str) and addr.startswith("/")


def _error(msg, code=400):
    return jsonify({"status": "error", "message": msg}), code


# ── Client-side device registry ──
# Stores per-device known messages/patches so the UI can show dropdowns/tables.
# This is updated by parsing reply messages and manual user edits.

_device_registry_lock = threading.Lock()
_device_registry = {}  # {device_id: {"host","port","name","messages":{},"patches":{}}}


def _get_device(device_id):
    """Return device dict, creating if needed."""
    with _device_registry_lock:
        if device_id not in _device_registry:
            _device_registry[device_id] = {
                "messages": {},
                "patches": {},
            }
        return _device_registry[device_id]


# ── Serial connection registry ──
# {sid: {"ser": Serial, "stop": Event}}
_serial_connections = {}

# ── Remote session registry ──
# {sid: {"host": str, "port": int, "device": str, "listen_port": int}}
_remote_sessions = {}

# ── Routes ──

@app.route("/")
def index():
    return render_template("index.html")


@app.route("/remote")
def remote():
    return render_template("remote.html")


# -- Send / Receive / Bridge (unchanged OSC transport) --

@app.route("/api/send", methods=["POST"])
def api_send():
    data = request.get_json(silent=True) or {}
    host = data.get("host", "").strip()
    port = data.get("port", "")
    address = data.get("address", "").strip()
    args = data.get("args")

    if not host:
        return _error("Host is required")
    if not _valid_host(host):
        return _error("Invalid host IP address")
    if not _valid_port(port):
        return _error("Port must be 1-65535")
    if not _valid_address(address):
        return _error("OSC address must start with /")

    result = engine.send_message(_resolve_host(host), int(port), address, args)
    return jsonify(result)


@app.route("/api/send/repeat", methods=["POST"])
def api_send_repeat():
    data = request.get_json(silent=True) or {}
    host = data.get("host", "").strip()
    port = data.get("port", "")
    address = data.get("address", "").strip()
    args = data.get("args")
    interval = data.get("interval", 1000)
    send_id = data.get("id", "default")

    if not host:
        return _error("Host is required")
    if not _valid_host(host):
        return _error("Invalid host IP address")
    if not _valid_port(port):
        return _error("Port must be 1-65535")
    if not _valid_address(address):
        return _error("OSC address must start with /")

    try:
        interval = max(10, int(interval))
    except (ValueError, TypeError):
        return _error("Invalid interval")

    result = engine.start_repeated_send(
        send_id, _resolve_host(host), int(port), address, args, interval)
    return jsonify(result)


@app.route("/api/send/stop", methods=["POST"])
def api_send_stop():
    data = request.get_json(silent=True) or {}
    return jsonify(engine.stop_repeated_send(data.get("id", "default")))


@app.route("/api/send/json", methods=["POST"])
def api_send_json():
    data = request.get_json(silent=True) or {}
    host = data.get("host", "").strip()
    port = data.get("port", "")
    messages = data.get("messages", [])
    interval = data.get("interval", 0)

    if not host:
        return _error("Host is required")
    if not _valid_host(host):
        return _error("Invalid host IP address")
    if not _valid_port(port):
        return _error("Port must be 1-65535")
    if not isinstance(messages, list) or not messages:
        return _error("Messages must be a non-empty array")

    result = engine.send_json_messages(
        _resolve_host(host), int(port), messages, int(interval))
    return jsonify(result)


@app.route("/api/recv/start", methods=["POST"])
def api_recv_start():
    data = request.get_json(silent=True) or {}
    port = data.get("port", 9000)
    filter_str = data.get("filter", "")
    recv_id = data.get("id", f"recv-{port}")

    if not _valid_port(port):
        return _error("Port must be 1-65535")

    result = engine.start_receiver(recv_id, int(port), filter_str)
    return jsonify(result)


@app.route("/api/recv/stop", methods=["POST"])
def api_recv_stop():
    data = request.get_json(silent=True) or {}
    recv_id = data.get("id", "")
    if not recv_id:
        return _error("Receiver id is required")
    return jsonify(engine.stop_receiver(recv_id))


@app.route("/api/bridge/start", methods=["POST"])
def api_bridge_start():
    data = request.get_json(silent=True) or {}
    in_port = data.get("in_port", "")
    out_host = data.get("out_host", "").strip()
    out_port = data.get("out_port", "")
    filter_str = data.get("filter", "")
    bridge_id = data.get("id", f"bridge-{in_port}-{out_port}")

    if not _valid_port(in_port):
        return _error("Input port must be 1-65535")
    if not out_host:
        return _error("Output host is required")
    if not _valid_host(out_host):
        return _error("Invalid output host IP address")
    if not _valid_port(out_port):
        return _error("Output port must be 1-65535")

    result = engine.start_bridge(
        bridge_id, int(in_port), _resolve_host(out_host),
        int(out_port), filter_str)
    return jsonify(result)


@app.route("/api/bridge/stop", methods=["POST"])
def api_bridge_stop():
    data = request.get_json(silent=True) or {}
    bridge_id = data.get("id", "")
    if not bridge_id:
        return _error("Bridge id is required")
    return jsonify(engine.stop_bridge(bridge_id))


@app.route("/api/log", methods=["GET"])
def api_log():
    return jsonify({"status": "ok", "log": engine.get_log()})


@app.route("/api/log/clear", methods=["POST"])
def api_log_clear():
    return jsonify(engine.clear_log())


@app.route("/api/status", methods=["GET"])
def api_status():
    return jsonify({"status": "ok", **engine.get_status()})


@app.route("/api/my-ip", methods=["GET"])
def api_my_ip():
    """Return the server machine's local IP address."""
    import socket as _socket
    try:
        s = _socket.socket(_socket.AF_INET, _socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
    except Exception:
        ip = "127.0.0.1"
    return jsonify({"status": "ok", "ip": ip})


@app.route("/api/stop-all", methods=["POST"])
def api_stop_all():
    return jsonify(engine.stop_all())


# ── Device registry API ──

@app.route("/api/devices", methods=["GET"])
def api_devices_list():
    """Return all known devices and their registries."""
    with _device_registry_lock:
        return jsonify({"status": "ok", "devices": _device_registry})


@app.route("/api/devices/<device_id>", methods=["GET"])
def api_device_get(device_id):
    """Return a single device's registry."""
    dev = _get_device(device_id)
    return jsonify({"status": "ok", "device": dev})


@app.route("/api/devices/<device_id>", methods=["DELETE"])
def api_device_delete(device_id):
    """Remove a device from tracking."""
    with _device_registry_lock:
        _device_registry.pop(device_id, None)
    return jsonify({"status": "ok"})


@app.route("/api/devices/<device_id>/messages", methods=["GET"])
def api_device_messages(device_id):
    """Return tracked messages for a device."""
    dev = _get_device(device_id)
    return jsonify({"status": "ok", "messages": dev["messages"]})


@app.route("/api/devices/<device_id>/messages", methods=["POST"])
def api_device_messages_update(device_id):
    """Update (merge) tracked messages for a device."""
    data = request.get_json(silent=True) or {}
    msgs = data.get("messages", {})
    dev = _get_device(device_id)
    with _device_registry_lock:
        dev["messages"].update(msgs)
    return jsonify({"status": "ok", "messages": dev["messages"]})


@app.route("/api/devices/<device_id>/messages/<msg_name>", methods=["PUT"])
def api_device_message_put(device_id, msg_name):
    """Create or replace a single tracked message."""
    data = request.get_json(silent=True) or {}
    dev = _get_device(device_id)
    with _device_registry_lock:
        dev["messages"][msg_name] = data
    return jsonify({"status": "ok"})


@app.route("/api/devices/<device_id>/messages/<msg_name>", methods=["DELETE"])
def api_device_message_delete(device_id, msg_name):
    """Remove a tracked message."""
    dev = _get_device(device_id)
    with _device_registry_lock:
        dev["messages"].pop(msg_name, None)
    return jsonify({"status": "ok"})


@app.route("/api/devices/<device_id>/patches", methods=["GET"])
def api_device_patches(device_id):
    """Return tracked patches for a device."""
    dev = _get_device(device_id)
    return jsonify({"status": "ok", "patches": dev["patches"]})


@app.route("/api/devices/<device_id>/patches", methods=["POST"])
def api_device_patches_update(device_id):
    """Update (merge) tracked patches for a device."""
    data = request.get_json(silent=True) or {}
    patches = data.get("patches", {})
    dev = _get_device(device_id)
    with _device_registry_lock:
        dev["patches"].update(patches)
    return jsonify({"status": "ok", "patches": dev["patches"]})


@app.route("/api/devices/<device_id>/patches/<patch_name>", methods=["PUT"])
def api_device_patch_put(device_id, patch_name):
    """Create or replace a single tracked patch."""
    data = request.get_json(silent=True) or {}
    dev = _get_device(device_id)
    with _device_registry_lock:
        dev["patches"][patch_name] = data
    return jsonify({"status": "ok"})


@app.route("/api/devices/<device_id>/patches/<patch_name>", methods=["DELETE"])
def api_device_patch_delete(device_id, patch_name):
    """Remove a tracked patch."""
    dev = _get_device(device_id)
    with _device_registry_lock:
        dev["patches"].pop(patch_name, None)
    return jsonify({"status": "ok"})


# ── SocketIO events ──

@socketio.on("connect")
def handle_connect():
    socketio.emit("status", engine.get_status())


@socketio.on("disconnect")
def handle_disconnect():
    sid = request.sid
    if sid in _remote_sessions:
        _remote_sessions.pop(sid, None)
        try:
            leave_room("remote_clients")
        except Exception:
            pass
        if not _remote_sessions:
            engine.stop_receiver("remote-recv")
    _close_serial(sid)


def _close_serial(sid):
    conn = _serial_connections.pop(sid, None)
    if conn:
        conn["stop"].set()
        try:
            conn["ser"].close()
        except Exception:
            pass


@socketio.on("serial_list_ports")
def handle_serial_list_ports():
    ports = [
        {"device": p.device, "description": p.description or p.device}
        for p in serial.tools.list_ports.comports()
    ]
    socketio.emit("serial_ports", {"ports": ports}, to=request.sid)


@socketio.on("serial_connect")
def handle_serial_connect(data):
    sid = request.sid
    port = data.get("port", "")
    baud = int(data.get("baud", 115200))
    _close_serial(sid)
    try:
        ser = serial.Serial(port, baud, timeout=0.1)
    except Exception as e:
        socketio.emit("serial_error", {"message": str(e)}, to=sid)
        return
    stop_event = threading.Event()

    def _read():
        while not stop_event.is_set():
            try:
                line = ser.readline()
                if line:
                    socketio.emit("serial_data",
                                  {"data": line.decode("utf-8", errors="replace")},
                                  to=sid)
            except Exception:
                break

    t = threading.Thread(target=_read, daemon=True)
    t.start()
    _serial_connections[sid] = {"ser": ser, "stop": stop_event}
    socketio.emit("serial_connected", {"port": port, "baud": baud}, to=sid)


@socketio.on("serial_disconnect_port")
def handle_serial_disconnect_port():
    sid = request.sid
    _close_serial(sid)
    socketio.emit("serial_disconnected", {}, to=sid)


@socketio.on("serial_send")
def handle_serial_send(data):
    sid = request.sid
    conn = _serial_connections.get(sid)
    if not conn:
        return
    text = data.get("data", "")
    try:
        conn["ser"].write((text + "\n").encode("utf-8", errors="replace"))
    except Exception as e:
        socketio.emit("serial_error", {"message": str(e)}, to=sid)


@socketio.on("ping_server")
def handle_ping():
    socketio.emit("pong_server", {"status": "ok"})


@socketio.on("remote_configure")
def handle_remote_configure(data):
    sid = request.sid
    host = _resolve_host(data.get("host", "").strip())
    port = data.get("port", 8000)
    device = data.get("device", "annieData").strip()
    listen_port = data.get("listen_port", 9000)

    if not _valid_host(host):
        socketio.emit("remote_error", {"message": f"Invalid host: {host}"}, to=sid)
        return
    if not _valid_port(port) or not _valid_port(listen_port):
        socketio.emit("remote_error", {"message": "Invalid port"}, to=sid)
        return

    _remote_sessions[sid] = {
        "host": host,
        "port": int(port),
        "device": device,
        "listen_port": int(listen_port),
    }
    join_room("remote_clients")
    engine.start_remote_receiver("remote-recv", int(listen_port))
    socketio.emit("remote_configured", {
        "status": "ok",
        "host": host,
        "port": int(port),
        "device": device,
        "listen_port": int(listen_port),
    }, to=sid)


@socketio.on("remote_send")
def handle_remote_send(data):
    sid = request.sid
    cfg = _remote_sessions.get(sid)
    if not cfg:
        socketio.emit("remote_error", {"message": "Not configured"}, to=sid)
        return
    address = data.get("address", "")
    args = data.get("args")
    engine.send_message(cfg["host"], cfg["port"], address, args)


@socketio.on("remote_disconnect_session")
def handle_remote_disconnect_session():
    sid = request.sid
    _remote_sessions.pop(sid, None)
    try:
        leave_room("remote_clients")
    except Exception:
        pass
    if not _remote_sessions:
        engine.stop_receiver("remote-recv")


# ── TheaterGWD presets ──

THEATER_GWD_PRESETS = {
    "sensor_values": [
        "accelX", "accelY", "accelZ", "accelLength",
        "gyroX", "gyroY", "gyroZ", "gyroLength",
        "baro",
        "eulerX", "eulerY", "eulerZ",
    ],
    "commands": {
        "blackout": {
            "address": "/annieData/{device}/blackout",
            "description": "Stop all patches — all sensor output halts.",
            "payload": None,
            "category": "device",
        },
        "restore": {
            "address": "/annieData/{device}/restore",
            "description": "Restart all patches that were running before blackout.",
            "payload": None,
            "category": "device",
        },
        "save": {
            "address": "/annieData/{device}/save",
            "description": "Persist all messages and patches to NVS.",
            "payload": None,
            "category": "device",
        },
        "load": {
            "address": "/annieData/{device}/load",
            "description": "Reload all messages and patches from NVS.",
            "payload": None,
            "category": "device",
        },
        "nvs_clear": {
            "address": "/annieData/{device}/nvs/clear",
            "description": "Erase all saved OSC data from NVS — factory reset.",
            "payload": None,
            "category": "device",
        },
        "list_messages": {
            "address": "/annieData/{device}/list/msgs",
            "description": "List all configured messages.",
            "payload": "optional: 'verbose'",
            "category": "list",
        },
        "list_patches": {
            "address": "/annieData/{device}/list/patches",
            "description": "List all configured patches.",
            "payload": "optional: 'verbose'",
            "category": "list",
        },
        "list_all": {
            "address": "/annieData/{device}/list/all",
            "description": "List all messages and patches.",
            "payload": "optional: 'verbose'",
            "category": "list",
        },
        "status_config": {
            "address": "/annieData/{device}/status/config",
            "description": "Configure where device sends status messages.",
            "payload": "config string: ip:x.x.x.x, port:N, adr:/status",
            "category": "status",
        },
        "status_level": {
            "address": "/annieData/{device}/status/level",
            "description": "Set minimum status level.",
            "payload": "error | warn | info | debug",
            "category": "status",
        },
        "create_message": {
            "address": "/annieData/{device}/msg/{name}",
            "description": "Create or update a named message.",
            "payload": "config string",
            "category": "message",
        },
        "delete_msg": {
            "address": "/annieData/{device}/msg/{name}/delete",
            "description": "Remove a message from the registry.",
            "payload": None,
            "category": "message",
        },
        "enable_msg": {
            "address": "/annieData/{device}/msg/{name}/enable",
            "description": "Enable a message so it sends.",
            "payload": None,
            "category": "message",
        },
        "disable_msg": {
            "address": "/annieData/{device}/msg/{name}/disable",
            "description": "Mute a message — stays registered but does not send.",
            "payload": None,
            "category": "message",
        },
        "info_msg": {
            "address": "/annieData/{device}/msg/{name}/info",
            "description": "Request the parameters of a specific message.",
            "payload": None,
            "category": "message",
        },
        "save_msg": {
            "address": "/annieData/{device}/save/msg",
            "description": "Save one specific message to NVS.",
            "payload": "message name",
            "category": "message",
        },
        "clone_msg": {
            "address": "/annieData/{device}/clone/msg",
            "description": "Duplicate a message to a new name.",
            "payload": "sourceName, destName",
            "category": "message",
        },
        "rename_msg": {
            "address": "/annieData/{device}/rename/msg",
            "description": "Rename a message.",
            "payload": "oldName, newName",
            "category": "message",
        },
        "create_patch": {
            "address": "/annieData/{device}/patch/{name}",
            "description": "Create or update a named patch.",
            "payload": "config string",
            "category": "patch",
        },
        "start_patch": {
            "address": "/annieData/{device}/patch/{name}/start",
            "description": "Start streaming all messages in a patch.",
            "payload": None,
            "category": "patch",
        },
        "stop_patch": {
            "address": "/annieData/{device}/patch/{name}/stop",
            "description": "Stop streaming all messages in a patch.",
            "payload": None,
            "category": "patch",
        },
        "delete_patch": {
            "address": "/annieData/{device}/patch/{name}/delete",
            "description": "Remove a patch and its task.",
            "payload": None,
            "category": "patch",
        },
        "info_patch": {
            "address": "/annieData/{device}/patch/{name}/info",
            "description": "Request the parameters of a specific patch.",
            "payload": None,
            "category": "patch",
        },
        "add_msg": {
            "address": "/annieData/{device}/patch/{name}/addMsg",
            "description": "Add message(s) to a patch.",
            "payload": "msgName or comma-separated names",
            "category": "patch",
        },
        "remove_msg": {
            "address": "/annieData/{device}/patch/{name}/removeMsg",
            "description": "Remove a message from a patch.",
            "payload": "msgName",
            "category": "patch",
        },
        "patch_period": {
            "address": "/annieData/{device}/patch/{name}/period",
            "description": "Set how often a patch sends messages (ms).",
            "payload": "period in ms (e.g. 50)",
            "category": "patch",
        },
        "patch_override": {
            "address": "/annieData/{device}/patch/{name}/override",
            "description": "Set which fields a patch forces on its messages.",
            "payload": "ip+port+adr+low+high (+-separated)",
            "category": "patch",
        },
        "patch_adr_mode": {
            "address": "/annieData/{device}/patch/{name}/adrMode",
            "description": "Set how the patch composes OSC addresses.",
            "payload": "fallback | override | prepend | append",
            "category": "patch",
        },
        "patch_set_all": {
            "address": "/annieData/{device}/patch/{name}/setAll",
            "description": "Apply a config string to every message in a patch.",
            "payload": "config string",
            "category": "patch",
        },
        "patch_solo": {
            "address": "/annieData/{device}/patch/{name}/solo",
            "description": "Enable one message, mute all others in patch.",
            "payload": "msgName",
            "category": "patch",
        },
        "patch_unsolo": {
            "address": "/annieData/{device}/patch/{name}/unsolo",
            "description": "Unmute all messages after a solo.",
            "payload": None,
            "category": "patch",
        },
        "patch_enable_all": {
            "address": "/annieData/{device}/patch/{name}/enableAll",
            "description": "Enable all messages in a patch.",
            "payload": None,
            "category": "patch",
        },
        "save_patch": {
            "address": "/annieData/{device}/save/patch",
            "description": "Save one specific patch to NVS.",
            "payload": "patch name",
            "category": "patch",
        },
        "clone_patch": {
            "address": "/annieData/{device}/clone/patch",
            "description": "Duplicate a patch to a new name.",
            "payload": "sourceName, destName",
            "category": "patch",
        },
        "rename_patch": {
            "address": "/annieData/{device}/rename/patch",
            "description": "Rename a patch.",
            "payload": "oldName, newName",
            "category": "patch",
        },
        "move_msg": {
            "address": "/annieData/{device}/move",
            "description": "Move a message from one patch to another.",
            "payload": "msgName, patchName",
            "category": "patch",
        },
        "direct": {
            "address": "/annieData/{device}/direct/{name}",
            "description": "One-step: create msg + patch, link, and start.",
            "payload": "config string",
            "category": "direct",
        },
    },
    "config_keys": {
        "value": "Which sensor to read (e.g. accelX, gyroY, baro).",
        "ip": "Destination IP address.",
        "port": "Destination UDP port number.",
        "adr": "OSC address path (e.g. /fader/1). Aliases: addr, address.",
        "low": "Output range minimum. Alias: min.",
        "high": "Output range maximum. Alias: max.",
        "patch": "Assign this message to a named patch.",
        "period": "Send interval in ms (patch-level or direct only).",
        "adrmode": "Address mode: fallback, override, prepend, append.",
        "override": "Override flags: ip+port+adr+low+high (+-separated).",
        "msgs": "Message list for a patch: msg1+msg2+msg3 (+-separated).",
        "enabled": "Enable flag: true or false.",
    },
    "address_modes": {
        "fallback": "Use message address; patch address only if message has none.",
        "override": "Patch address replaces message address.",
        "prepend": "patch.adr + msg.adr (e.g. /mixer + /fader1 → /mixer/fader1).",
        "append": "msg.adr + patch.adr (e.g. /fader1 + /mixer → /fader1/mixer).",
    },
    "status_levels": ["error", "warn", "info", "debug"],
    "keywords": {
        "blackout": "Stop all patches immediately — all sensor output halts.",
        "restore": "Restart all patches that were running before blackout.",
        "save": "Persist all messages and patches to NVS so they survive reboot.",
        "load": "Reload all messages and patches from NVS.",
        "nvs/clear": "Erase all saved OSC data from NVS — factory reset for OSC config.",
        "msg": "A named message — maps a sensor value to a target IP, port, and OSC address.",
        "patch": "A named group of messages that can be started/stopped together.",
        "direct": "One-step command: creates msg + patch, links them, and starts sending.",
        "start": "Begin streaming all messages belonging to a patch.",
        "stop": "Stop streaming all messages belonging to a patch.",
        "delete": "Remove a message or patch from the device registry.",
        "enable": "Enable a previously disabled message so it sends again.",
        "disable": "Mute a message — it stays registered but does not send.",
        "info": "Request the parameters of a specific message or patch.",
        "addMsg": "Add one or more existing messages to a patch (comma-separated).",
        "removeMsg": "Remove a message from a patch.",
        "period": "Set how often a patch sends its messages, in milliseconds.",
        "override": "Set which fields (ip, port, adr, low, high) a patch forces on its messages.",
        "adrMode": "Set how the patch composes OSC addresses for its messages.",
        "setAll": "Apply a config string to every message in a patch at once.",
        "solo": "Enable one message in a patch, mute all others.",
        "unsolo": "Unmute all messages in a patch after a solo.",
        "enableAll": "Enable all messages in a patch.",
        "clone": "Copy a message or patch to a new name (payload: srcName, destName).",
        "rename": "Rename a message or patch (payload: oldName, newName).",
        "move": "Move a message from one patch to another (payload: msgName, patchName).",
        "list": "Request the device to list configured messages, patches, or both.",
        "status/config": "Set where the device sends status/reply messages (config string).",
        "status/level": "Set minimum status level: error, warn, info, or debug.",
        "save/msg": "Save one specific message to NVS (payload: message name).",
        "save/patch": "Save one specific patch to NVS (payload: patch name).",
        "accelX": "Accelerometer X-axis — tilt left/right.",
        "accelY": "Accelerometer Y-axis — tilt forward/back.",
        "accelZ": "Accelerometer Z-axis — vertical acceleration.",
        "accelLength": "Total acceleration magnitude (combined X, Y, Z).",
        "gyroX": "Gyroscope X-axis — rotational velocity around X.",
        "gyroY": "Gyroscope Y-axis — rotational velocity around Y.",
        "gyroZ": "Gyroscope Z-axis — rotational velocity around Z.",
        "gyroLength": "Total rotational velocity magnitude.",
        "baro": "Barometric pressure sensor — altitude / air pressure.",
        "eulerX": "Euler angle X (roll) — orientation around X-axis.",
        "eulerY": "Euler angle Y (pitch) — orientation around Y-axis.",
        "eulerZ": "Euler angle Z (yaw) — orientation around Z-axis.",
        "fallback": "Address mode: use msg address, patch address as fallback.",
        "prepend": "Address mode: patch.adr + msg.adr.",
        "append": "Address mode: msg.adr + patch.adr.",
        "config string": "CSV key:value pairs, e.g. 'value:accelX, ip:192.168.1.50, port:9000'.",
        "gaccelX": "Gravity-corrected acceleration X — linear acceleration without gravity.",
        "gaccelY": "Gravity-corrected acceleration Y — linear acceleration without gravity.",
        "gaccelZ": "Gravity-corrected acceleration Z — linear acceleration without gravity.",
        "gaccelLength": "Gravity-corrected acceleration magnitude.",
        "quatI": "Quaternion I component — orientation in quaternion form.",
        "quatJ": "Quaternion J component — orientation in quaternion form.",
        "quatK": "Quaternion K component — orientation in quaternion form.",
        "quatR": "Quaternion R (scalar/real) component — orientation in quaternion form.",
    },
}


@app.route("/api/presets/theater-gwd", methods=["GET"])
def api_theater_gwd_presets():
    return jsonify({"status": "ok", "presets": THEATER_GWD_PRESETS})


# ── Show Library (disk JSON) ──

_SHOWS_DIR = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "data", "shows")
)


def _ensure_shows_dir():
    os.makedirs(_SHOWS_DIR, exist_ok=True)


def _show_path(name):
    """Return the file path for a show, rejecting path-traversal attempts."""
    safe = re.sub(r"[^\w\-. ]", "_", name)
    return os.path.join(_SHOWS_DIR, safe + ".json")


@app.route("/api/shows", methods=["GET"])
def api_shows_list():
    _ensure_shows_dir()
    shows = []
    for fname in sorted(os.listdir(_SHOWS_DIR)):
        if not fname.endswith(".json"):
            continue
        fpath = os.path.join(_SHOWS_DIR, fname)
        try:
            with open(fpath, encoding="utf-8") as fh:
                data = json.load(fh)
            shows.append({
                "name": data.get("name", fname[:-5]),
                "saved": data.get("saved", ""),
                "device": data.get("device", ""),
            })
        except Exception:
            pass
    return jsonify(shows)


@app.route("/api/shows/<name>", methods=["GET"])
def api_shows_get(name):
    _ensure_shows_dir()
    fpath = _show_path(name)
    if not os.path.isfile(fpath):
        return _error("Show not found", 404)
    try:
        with open(fpath, encoding="utf-8") as fh:
            data = json.load(fh)
        return jsonify(data)
    except Exception as e:
        return _error(str(e), 500)


@app.route("/api/shows/<name>", methods=["POST"])
def api_shows_save(name):
    _ensure_shows_dir()
    body = request.get_json(silent=True) or {}
    body["name"] = name
    if not body.get("saved"):
        body["saved"] = datetime.datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%S")
    fpath = _show_path(name)
    try:
        with open(fpath, "w", encoding="utf-8") as fh:
            json.dump(body, fh, indent=2)
        return jsonify({"status": "ok", "name": name})
    except Exception as e:
        return _error(str(e), 500)


@app.route("/api/shows/<name>", methods=["DELETE"])
def api_shows_delete(name):
    fpath = _show_path(name)
    if not os.path.isfile(fpath):
        return _error("Show not found", 404)
    try:
        os.remove(fpath)
        return jsonify({"status": "ok"})
    except Exception as e:
        return _error(str(e), 500)


# ── Docs ──

def _find_docs_root():
    """Return the docs directory, checking both source-checkout and brew-install layouts."""
    base = os.path.dirname(__file__)
    candidates = [
        os.path.join(base, "..", "..", "docs"),  # source checkout: repo/docs/
        os.path.join(base, "..", "docs"),         # brew install:   libexec/docs/
    ]
    for path in candidates:
        path = os.path.normpath(path)
        if os.path.isdir(path):
            return path
    # Fall back to source-checkout path even if absent (produces a clear 404)
    return os.path.normpath(candidates[0])

_DOCS_ROOT = _find_docs_root()

_DOCS_GUIDES = {
    "user-guide": ("user_guide.md", "User Guide"),
    "technical-guide": ("technical_guide.md", "Technical Guide"),
}

_MD_EXTENSIONS = ["toc", "fenced_code", "tables", "attr_list"]
_MD_EXTENSION_CONFIGS = {
    "toc": {"permalink": True, "toc_depth": "2-3"},
}


@app.route("/docs/")
@app.route("/docs/<guide>")
def docs(guide=None):
    if guide is None:
        guide = "user-guide"
    if guide not in _DOCS_GUIDES:
        return _error("Unknown guide", 404)
    filename, title = _DOCS_GUIDES[guide]
    path = os.path.join(_DOCS_ROOT, filename)
    try:
        with open(path, encoding="utf-8") as fh:
            raw = fh.read()
    except OSError:
        return _error("Guide not found", 404)
    md = md_lib.Markdown(extensions=_MD_EXTENSIONS, extension_configs=_MD_EXTENSION_CONFIGS)
    content_html = md.convert(raw)
    toc_html = getattr(md, "toc", "")
    other_guide = "technical-guide" if guide == "user-guide" else "user-guide"
    other_title = _DOCS_GUIDES[other_guide][1]
    return render_template(
        "docs.html",
        title=title,
        content=content_html,
        toc=toc_html,
        guide=guide,
        other_guide=other_guide,
        other_title=other_title,
    )


def create_app():
    """Create and return the Flask app."""
    return app, socketio
