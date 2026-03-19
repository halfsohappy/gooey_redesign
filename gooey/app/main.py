"""Flask application with SocketIO for TheaterGWD Control Center."""

import re

from flask import Flask, render_template, request, jsonify
from flask_socketio import SocketIO

from .osc_handler import OSCEngine

app = Flask(__name__)
app.config["SECRET_KEY"] = "theatergwd-control-center"
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")
engine = OSCEngine(socketio)

# --- Validation helpers ---

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
    h = _resolve_host(host)
    return bool(_IP_RE.match(h))


def _valid_port(port):
    try:
        p = int(port)
        return 1 <= p <= 65535
    except (ValueError, TypeError):
        return False


def _valid_address(addr):
    return isinstance(addr, str) and addr.startswith("/")


def _error(msg, code=400):
    return jsonify({"status": "error", "message": msg}), code


# --- Routes ---

@app.route("/")
def index():
    return render_template("index.html")


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
    send_id = data.get("id", "default")
    result = engine.stop_repeated_send(send_id)
    return jsonify(result)


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
    result = engine.stop_receiver(recv_id)
    return jsonify(result)


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
    result = engine.stop_bridge(bridge_id)
    return jsonify(result)


@app.route("/api/log", methods=["GET"])
def api_log():
    return jsonify({"status": "ok", "log": engine.get_log()})


@app.route("/api/log/clear", methods=["POST"])
def api_log_clear():
    return jsonify(engine.clear_log())


@app.route("/api/status", methods=["GET"])
def api_status():
    return jsonify({"status": "ok", **engine.get_status()})


@app.route("/api/stop-all", methods=["POST"])
def api_stop_all():
    return jsonify(engine.stop_all())


# --- SocketIO events ---

@socketio.on("connect")
def handle_connect():
    socketio.emit("status", engine.get_status())


@socketio.on("ping_server")
def handle_ping():
    socketio.emit("pong_server", {"status": "ok"})


# --- TheaterGWD presets ---

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
    },
}


@app.route("/api/presets/theater-gwd", methods=["GET"])
def api_theater_gwd_presets():
    return jsonify({"status": "ok", "presets": THEATER_GWD_PRESETS})


def create_app():
    """Create and return the Flask app."""
    return app, socketio
