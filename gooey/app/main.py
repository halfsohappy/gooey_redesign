"""Flask application with SocketIO for TheaterGWD Control Center."""

import datetime
import json
import logging
import os
import re
import threading

import markdown as md_lib
import serial
import serial.tools.list_ports
from flask import Flask, render_template, request, jsonify, send_from_directory
from flask_socketio import SocketIO, join_room, leave_room

from .osc_handler import OSCEngine
from .script_runner import ScriptRunner

app = Flask(__name__)
app.config["SECRET_KEY"] = "theatergwd-control-center"
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")
DEMO_MODE = os.environ.get("DEMO_MODE", "").lower() == "true"

# Suppress benign Werkzeug AssertionError noise (browser dropped connections)
class _DropWerkzeugAssertions(logging.Filter):
    def filter(self, record):
        return not (record.exc_info and record.exc_info[0] is AssertionError)

logging.getLogger("werkzeug").addFilter(_DropWerkzeugAssertions())
engine = OSCEngine(socketio)
script_runner = ScriptRunner(engine, socketio)

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


def _demo(action):
    return jsonify({"status": "error", "message": f"Demo mode \u2014 {action}"}), 200


# ── Client-side device registry ──
# Stores per-device known messages/scenes so the UI can show dropdowns/tables.
# This is updated by parsing reply messages and manual user edits.

_device_registry_lock = threading.Lock()
_device_registry = {}  # {device_id: {"host","port","name","messages":{},"scenes":{}}}


def _get_device(device_id):
    """Return device dict, creating if needed."""
    with _device_registry_lock:
        if device_id not in _device_registry:
            _device_registry[device_id] = {
                "messages": {},
                "scenes": {},
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
    return render_template("index.html", demo_mode=DEMO_MODE)


@app.route("/remote")
def remote():
    return render_template("remote.html")


@app.route("/sw.js")
def service_worker():
    response = send_from_directory(app.static_folder, "sw.js")
    response.headers["Service-Worker-Allowed"] = "/"
    response.headers["Cache-Control"] = "no-cache"
    return response


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

    if DEMO_MODE:
        return _demo("OSC messages can't be sent in the online demo.")
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

    if DEMO_MODE:
        return _demo("Repeated send isn't available in the online demo.")
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

    if DEMO_MODE:
        return _demo("OSC messages can't be sent in the online demo.")
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

    if DEMO_MODE:
        return _demo("OSC listeners can't be started in the online demo.")
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

    if DEMO_MODE:
        return _demo("The OSC bridge isn't available in the online demo.")
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


@app.route("/api/remote-qr")
def api_remote_qr():
    """Return a styled SVG QR code pointing to /remote on this machine's LAN IP."""
    import socket as _socket
    import qrcode
    try:
        s = _socket.socket(_socket.AF_INET, _socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
    except Exception:
        ip = "127.0.0.1"
    port = request.host.split(":")[-1] if ":" in request.host else "80"
    url = f"http://{ip}:{port}/remote"

    qr = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_H,
                       box_size=10, border=4)
    qr.add_data(url)
    qr.make(fit=True)
    matrix = qr.get_matrix()
    N   = len(matrix)
    SZ  = 10
    PAD = 24
    COLOR = "#4A5568"
    BG    = "#DAC7FF"

    def _is_finder(r, c):
        return (r < 7 and c < 7) or (r < 7 and c >= N - 7) or (r >= N - 7 and c < 7)

    parts = []
    for r, row in enumerate(matrix):
        for c, cell in enumerate(row):
            if not cell:
                continue
            x, y = c * SZ, r * SZ
            if _is_finder(r, c):
                parts.append(
                    f'<rect x="{x+.5}" y="{y+.5}" width="{SZ-1}" height="{SZ-1}" '
                    f'rx="2.5" ry="2.5" fill="{COLOR}"/>'
                )
            else:
                cx, cy, rv = x + SZ / 2, y + SZ / 2, SZ / 2 * 0.78
                parts.append(f'<circle cx="{cx}" cy="{cy}" r="{rv}" fill="{COLOR}"/>')

    full = N * SZ + PAD * 2
    svg = (
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {full} {full}"'
        f' width="{full}" height="{full}">'
        f'<rect width="{full}" height="{full}" rx="16" fill="{BG}"/>'
        f'<g transform="translate({PAD},{PAD})">'
        + "".join(parts) +
        '</g></svg>'
    )
    return svg, 200, {"Content-Type": "image/svg+xml", "X-Remote-URL": url}


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


@app.route("/api/devices/<device_id>/scenes", methods=["GET"])
def api_device_scenes(device_id):
    """Return tracked scenes for a device."""
    dev = _get_device(device_id)
    return jsonify({"status": "ok", "scenes": dev["scenes"]})


@app.route("/api/devices/<device_id>/scenes", methods=["POST"])
def api_device_scenes_update(device_id):
    """Update (merge) tracked scenes for a device."""
    data = request.get_json(silent=True) or {}
    scenes = data.get("scenes", {})
    dev = _get_device(device_id)
    with _device_registry_lock:
        dev["scenes"].update(scenes)
    return jsonify({"status": "ok", "scenes": dev["scenes"]})


@app.route("/api/devices/<device_id>/scenes/<scene_name>", methods=["PUT"])
def api_device_scene_put(device_id, scene_name):
    """Create or replace a single tracked scene."""
    data = request.get_json(silent=True) or {}
    dev = _get_device(device_id)
    with _device_registry_lock:
        dev["scenes"][scene_name] = data
    return jsonify({"status": "ok"})


@app.route("/api/devices/<device_id>/scenes/<scene_name>", methods=["DELETE"])
def api_device_scene_delete(device_id, scene_name):
    """Remove a tracked scene."""
    dev = _get_device(device_id)
    with _device_registry_lock:
        dev["scenes"].pop(scene_name, None)
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
    if DEMO_MODE:
        socketio.emit("serial_error", {"message": "Demo mode \u2014 Serial connections aren't available in the online demo."}, to=sid)
        return
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
    if DEMO_MODE:
        socketio.emit("serial_error", {"message": "Demo mode \u2014 Can't send serial data in the online demo."}, to=sid)
        return
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
            "description": "Stop all scenes — all sensor output halts.",
            "payload": None,
            "category": "device",
        },
        "restore": {
            "address": "/annieData/{device}/restore",
            "description": "Restart all scenes that were running before blackout.",
            "payload": None,
            "category": "device",
        },
        "save": {
            "address": "/annieData/{device}/save",
            "description": "Persist all messages and scenes to NVS.",
            "payload": None,
            "category": "device",
        },
        "load": {
            "address": "/annieData/{device}/load",
            "description": "Reload all messages and scenes from NVS.",
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
        "list_scenes": {
            "address": "/annieData/{device}/list/scenes",
            "description": "List all configured scenes.",
            "payload": "optional: 'verbose'",
            "category": "list",
        },
        "list_all": {
            "address": "/annieData/{device}/list/all",
            "description": "List all messages and scenes.",
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
            "description": "Remove a message from the registry. Supports OSC patterns in {name}.",
            "payload": None,
            "category": "message",
        },
        "enable_msg": {
            "address": "/annieData/{device}/msg/{name}/enable",
            "description": "Enable a message so it sends. Supports OSC patterns in {name} (e.g. accel*).",
            "payload": None,
            "category": "message",
        },
        "disable_msg": {
            "address": "/annieData/{device}/msg/{name}/disable",
            "description": "Mute a message — stays registered but does not send. Supports OSC patterns in {name}.",
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
        "create_scene": {
            "address": "/annieData/{device}/scene/{name}",
            "description": "Create or update a named scene.",
            "payload": "config string",
            "category": "scene",
        },
        "start_scene": {
            "address": "/annieData/{device}/scene/{name}/start",
            "description": "Start streaming all messages in a scene. Supports OSC patterns in {name}.",
            "payload": None,
            "category": "scene",
        },
        "stop_scene": {
            "address": "/annieData/{device}/scene/{name}/stop",
            "description": "Stop streaming all messages in a scene. Supports OSC patterns in {name}.",
            "payload": None,
            "category": "scene",
        },
        "delete_scene": {
            "address": "/annieData/{device}/scene/{name}/delete",
            "description": "Remove a scene and its task. Supports OSC patterns in {name}.",
            "payload": None,
            "category": "scene",
        },
        "info_scene": {
            "address": "/annieData/{device}/scene/{name}/info",
            "description": "Request the parameters of a specific scene.",
            "payload": None,
            "category": "scene",
        },
        "add_msg": {
            "address": "/annieData/{device}/scene/{name}/addMsg",
            "description": "Add message(s) to a scene.",
            "payload": "msgName or comma-separated names",
            "category": "scene",
        },
        "remove_msg": {
            "address": "/annieData/{device}/scene/{name}/removeMsg",
            "description": "Remove a message from a scene.",
            "payload": "msgName",
            "category": "scene",
        },
        "scene_period": {
            "address": "/annieData/{device}/scene/{name}/period",
            "description": "Set how often a scene sends messages (ms).",
            "payload": "period in ms (e.g. 50)",
            "category": "scene",
        },
        "scene_override": {
            "address": "/annieData/{device}/scene/{name}/override",
            "description": "Set which fields a scene forces on its messages.",
            "payload": "ip+port+adr+low+high (+-separated)",
            "category": "scene",
        },
        "scene_adr_mode": {
            "address": "/annieData/{device}/scene/{name}/adrMode",
            "description": "Set how the scene composes OSC addresses.",
            "payload": "fallback | override | prepend | append",
            "category": "scene",
        },
        "scene_set_all": {
            "address": "/annieData/{device}/scene/{name}/setAll",
            "description": "Apply a config string to every message in a scene.",
            "payload": "config string",
            "category": "scene",
        },
        "scene_solo": {
            "address": "/annieData/{device}/scene/{name}/solo",
            "description": "Enable one message, mute all others in scene.",
            "payload": "msgName",
            "category": "scene",
        },
        "scene_unsolo": {
            "address": "/annieData/{device}/scene/{name}/unsolo",
            "description": "Unmute all messages after a solo.",
            "payload": None,
            "category": "scene",
        },
        "scene_enable_all": {
            "address": "/annieData/{device}/scene/{name}/enableAll",
            "description": "Enable all messages in a scene.",
            "payload": None,
            "category": "scene",
        },
        "save_scene": {
            "address": "/annieData/{device}/save/scene",
            "description": "Save one specific scene to NVS.",
            "payload": "scene name",
            "category": "scene",
        },
        "clone_scene": {
            "address": "/annieData/{device}/clone/scene",
            "description": "Duplicate a scene to a new name.",
            "payload": "sourceName, destName",
            "category": "scene",
        },
        "rename_scene": {
            "address": "/annieData/{device}/rename/scene",
            "description": "Rename a scene.",
            "payload": "oldName, newName",
            "category": "scene",
        },
        "move_msg": {
            "address": "/annieData/{device}/move",
            "description": "Move a message from one scene to another.",
            "payload": "msgName, sceneName",
            "category": "scene",
        },
        "direct": {
            "address": "/annieData/{device}/direct/{name}",
            "description": "One-step: create msg + scene, link, and start.",
            "payload": "config string",
            "category": "direct",
        },
        "flush": {
            "address": "/annieData/{device}/flush",
            "description": "Reply OK once all preceding commands are processed. Used for transactional push.",
            "payload": None,
            "category": "device",
        },
        "show_save": {
            "address": "/annieData/{device}/show/save/{name}",
            "description": "Snapshot current device state as a named show (up to 16 on device).",
            "payload": None,
            "category": "show",
        },
        "show_load": {
            "address": "/annieData/{device}/show/load/{name}",
            "description": "Load a named show (two-step: pending then confirm).",
            "payload": None,
            "category": "show",
        },
        "show_list": {
            "address": "/annieData/{device}/show/list",
            "description": "List saved show names on device.",
            "payload": None,
            "category": "show",
        },
        "show_delete": {
            "address": "/annieData/{device}/show/delete/{name}",
            "description": "Delete a named show from device NVS.",
            "payload": None,
            "category": "show",
        },
        "show_rename": {
            "address": "/annieData/{device}/show/rename",
            "description": "Rename a saved show.",
            "payload": "oldName, newName",
            "category": "show",
        },
    },
    "config_keys": {
        "value": "Which sensor to read (e.g. accelX, gyroY, baro).",
        "ip": "Destination IP address.",
        "port": "Destination UDP port number.",
        "adr": "OSC address path (e.g. /fader/1). Aliases: addr, address.",
        "low": "Output range minimum. Alias: min.",
        "high": "Output range maximum. Alias: max.",
        "scene": "Assign this message to scene(s). Use + for multiple: scene:mix1+mix2.",
        "period": "Send interval in ms (scene-level or direct only).",
        "adrmode": "Address mode: fallback, override, prepend, append.",
        "override": "Override flags: ip+port+adr+low+high (+-separated).",
        "msgs": "Message list for a scene: msg1+msg2+msg3 (+-separated).",
        "enabled": "Enable flag: true or false.",
    },
    "address_modes": {
        "fallback": "Use message address; scene address only if message has none.",
        "override": "Scene address replaces message address.",
        "prepend": "scene.adr + msg.adr (e.g. /mixer + /fader1 → /mixer/fader1).",
        "append": "msg.adr + scene.adr (e.g. /fader1 + /mixer → /fader1/mixer).",
    },
    "status_levels": ["error", "warn", "info", "debug"],
    "keywords": {
        "blackout": "Stop all scenes immediately — all sensor output halts.",
        "restore": "Restart all scenes that were running before blackout.",
        "save": "Persist all messages and scenes to NVS so they survive reboot.",
        "load": "Reload all messages and scenes from NVS.",
        "nvs/clear": "Erase all saved OSC data from NVS — factory reset for OSC config.",
        "msg": "A named message — maps a sensor value to a target IP, port, and OSC address.",
        "scene": "A named group of messages that can be started/stopped together.",
        "direct": "One-step command: creates msg + scene, links them, and starts sending.",
        "start": "Begin streaming all messages belonging to a scene.",
        "stop": "Stop streaming all messages belonging to a scene.",
        "delete": "Remove a message or scene from the device registry.",
        "enable": "Enable a previously disabled message so it sends again.",
        "disable": "Mute a message — it stays registered but does not send.",
        "info": "Request the parameters of a specific message or scene.",
        "addMsg": "Add one or more existing messages to a scene (comma-separated).",
        "removeMsg": "Remove a message from a scene.",
        "period": "Set how often a scene sends its messages, in milliseconds.",
        "override": "Set which fields (ip, port, adr, low, high) a scene forces on its messages.",
        "adrMode": "Set how the scene composes OSC addresses for its messages.",
        "setAll": "Apply a config string to every message in a scene at once.",
        "solo": "Enable one message in a scene, mute all others.",
        "unsolo": "Unmute all messages in a scene after a solo.",
        "enableAll": "Enable all messages in a scene.",
        "clone": "Copy a message or scene to a new name (payload: srcName, destName).",
        "rename": "Rename a message or scene (payload: oldName, newName).",
        "move": "Move a message from one scene to another (payload: msgName, sceneName).",
        "list": "Request the device to list configured messages, scenes, or both.",
        "status/config": "Set where the device sends status/reply messages (config string).",
        "status/level": "Set minimum status level: error, warn, info, or debug.",
        "save/msg": "Save one specific message to NVS (payload: message name).",
        "save/scene": "Save one specific scene to NVS (payload: scene name).",
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
        "fallback": "Address mode: use msg address, scene address as fallback.",
        "prepend": "Address mode: scene.adr + msg.adr.",
        "append": "Address mode: msg.adr + scene.adr.",
        "flush": "Synchronization barrier — device replies OK after all preceding commands finish.",
        "show/save": "Snapshot current state as a named show (up to 16 on device NVS).",
        "show/load": "Load a named show (two-step: send load, then confirm).",
        "show/list": "List all saved show names on device.",
        "show/delete": "Delete a named show from device NVS.",
        "show/rename": "Rename a saved show (payload: oldName, newName).",
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
    "pattern_matching": {
        "description": "OSC 1.0 pattern matching lets you target multiple messages or scenes with a single command.",
        "supported_in": "The {name} segment of /msg/{name}/{command} and /scene/{name}/{command} addresses.",
        "patterns": {
            "*": "Match zero or more characters. Example: accel* matches accelX, accelY, accelZ.",
            "?": "Match exactly one character. Example: accel? matches accelX but not accelXY.",
            "[abc]": "Match any single character in the set. Example: accel[XY] matches accelX, accelY.",
            "[a-z]": "Match any character in the range. Example: [a-c]hat matches ahat, bhat, chat.",
            "[!abc]": "Match any character NOT in the set. Example: accel[!Z] matches accelX, accelY.",
            "{foo,bar}": "Match any of the alternatives. Example: {accelX,gyroY} matches accelX or gyroY.",
        },
        "restrictions": "Patterns cannot be used with assign/create — you cannot create an entity named '*'.",
        "examples": [
            "/msg/*/enable — enable all messages",
            "/msg/accel*/info — get info for all accel messages",
            "/scene/*/start — start all scenes",
            "/scene/{mix,fx}/stop — stop scenes named 'mix' or 'fx'",
            "/msg/[!g]*/delete — delete all messages not starting with 'g'",
        ],
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


# ── Script runner ──

_SCRIPTS_DIR = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "data", "scripts")
)


def _ensure_scripts_dir():
    os.makedirs(_SCRIPTS_DIR, exist_ok=True)


def _script_path(name):
    safe = re.sub(r"[^\w\-. ]", "_", name)
    return os.path.join(_SCRIPTS_DIR, safe + ".py")


@app.route("/api/scripts", methods=["GET"])
def api_scripts_list():
    _ensure_scripts_dir()
    scripts = []
    for fname in sorted(os.listdir(_SCRIPTS_DIR)):
        if not fname.endswith(".py"):
            continue
        scripts.append({"name": fname[:-3]})
    return jsonify({"status": "ok", "scripts": scripts})


@app.route("/api/scripts/<name>", methods=["GET"])
def api_scripts_get(name):
    _ensure_scripts_dir()
    fpath = _script_path(name)
    if not os.path.isfile(fpath):
        return _error("Script not found", 404)
    with open(fpath, encoding="utf-8") as fh:
        code = fh.read()
    return jsonify({"status": "ok", "name": name, "code": code})


@app.route("/api/scripts/<name>", methods=["POST"])
def api_scripts_save(name):
    _ensure_scripts_dir()
    body = request.get_json(silent=True) or {}
    code = body.get("code", "")
    fpath = _script_path(name)
    with open(fpath, "w", encoding="utf-8") as fh:
        fh.write(code)
    return jsonify({"status": "ok", "name": name})


@app.route("/api/scripts/<name>", methods=["DELETE"])
def api_scripts_delete(name):
    fpath = _script_path(name)
    if not os.path.isfile(fpath):
        return _error("Script not found", 404)
    os.remove(fpath)
    return jsonify({"status": "ok"})


@socketio.on("script_run")
def handle_script_run(data):
    if DEMO_MODE:
        socketio.emit("script_output", {
            "time": "", "text": "Demo mode -- scripts can't run in the online demo.",
            "level": "error"
        }, to=request.sid)
        return
    code = data.get("code", "")
    mode = data.get("mode", "loop")
    interval = data.get("interval", 50)
    listen_port = data.get("listen_port")
    device_context = {
        "id":   data.get("device_id", ""),
        "host": data.get("device_host", ""),
        "port": int(data.get("device_port") or 8000),
        "name": data.get("device_name", ""),
    }
    script_runner.run(code, mode=mode, interval_ms=interval,
                      listen_port=listen_port,
                      device_context=device_context,
                      registry=_device_registry,
                      registry_lock=_device_registry_lock)


@socketio.on("script_stop")
def handle_script_stop():
    script_runner.stop()


@socketio.on("script_status")
def handle_script_status():
    socketio.emit("script_status_reply", {
        "running": script_runner.running,
    }, to=request.sid)


# ── Docs ──

def _find_docs_root():
    """Return the docs directory, checking PyInstaller bundle, source checkout, and brew install."""
    import sys
    candidates = []
    # PyInstaller --onefile: data files extracted to sys._MEIPASS at runtime
    if getattr(sys, 'frozen', False) and hasattr(sys, '_MEIPASS'):
        candidates.append(os.path.join(sys._MEIPASS, 'docs'))
    base = os.path.dirname(__file__)
    candidates += [
        os.path.join(base, "..", "..", "docs"),  # source checkout: repo/docs/
        os.path.join(base, "..", "docs"),         # brew install:   libexec/docs/
    ]
    for path in candidates:
        path = os.path.normpath(path)
        if os.path.isdir(path):
            return path
    # Fall back to first candidate even if absent (produces a clear 404)
    return os.path.normpath(candidates[0])

_DOCS_ROOT = _find_docs_root()

_DOCS_GUIDES = {
    "gooey-guide": ("gooey_guide.md", "Gooey Guide"),
    "osc-guide": ("osc_guide.md", "OSC Guide"),
    "engineering": ("engineering.md", "Engineering Guide"),
}

_MD_EXTENSIONS = ["toc", "fenced_code", "tables", "attr_list"]
_MD_EXTENSION_CONFIGS = {
    "toc": {"permalink": True, "toc_depth": "2-3"},
}


@app.route("/docs/")
@app.route("/docs/<guide>")
def docs(guide=None):
    if guide is None:
        guide = "gooey-guide"
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
    other_guides = {k: v[1] for k, v in _DOCS_GUIDES.items() if k != guide}
    return render_template(
        "docs.html",
        title=title,
        content=content_html,
        toc=toc_html,
        guide=guide,
        other_guides=other_guides,
    )


def create_app():
    """Create and return the Flask app."""
    return app, socketio
