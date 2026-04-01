"""User script execution engine for the Script tab.

Runs user-written Python in a dedicated thread with a curated namespace
that exposes OSC send/receive helpers, sensor data access, and high-level
proxies for controlling TheaterGWD Messages and Scenes.
"""

import collections as _collections
import math
import random as _random
import re as _re
import signal
import threading
import time
import traceback
from datetime import datetime, timezone


# Builtins allowed inside user scripts (no file/network/import access)
_SAFE_BUILTINS = {
    "abs": abs, "all": all, "any": any, "bin": bin, "bool": bool,
    "bytes": bytes, "chr": chr, "dict": dict, "divmod": divmod,
    "enumerate": enumerate, "filter": filter, "float": float,
    "format": format, "frozenset": frozenset, "hex": hex, "int": int,
    "isinstance": isinstance, "len": len, "list": list, "map": map,
    "max": max, "min": min, "oct": oct, "ord": ord, "pow": pow,
    "range": range, "repr": repr, "reversed": reversed, "round": round,
    "set": set, "slice": slice, "sorted": sorted, "str": str,
    "sum": sum, "tuple": tuple, "type": type, "zip": zip,
    "True": True, "False": False, "None": None,
}


# ── OSC pattern matching ──

def _osc_pattern_to_regex(pattern):
    """Convert an OSC 1.0 address pattern to a compiled Python regex.

    Supports: * (any chars), ? (one char), [abc]/[!abc] (char class),
    {alt1,alt2} (alternation).
    """
    r = "^"
    i = 0
    while i < len(pattern):
        c = pattern[i]
        if c == "*":
            r += ".*"
            i += 1
        elif c == "?":
            r += "."
            i += 1
        elif c == "[":
            j = pattern.find("]", i)
            if j < 0:
                r += _re.escape("[")
                i += 1
                continue
            inner = pattern[i + 1:j]
            if inner.startswith("!"):
                inner = "^" + inner[1:]
            r += "[" + inner + "]"
            i = j + 1
        elif c == "{":
            j = pattern.find("}", i)
            if j < 0:
                r += _re.escape("{")
                i += 1
                continue
            alts = [_re.escape(a) for a in pattern[i + 1:j].split(",")]
            r += "(?:" + "|".join(alts) + ")"
            i = j + 1
        else:
            r += _re.escape(c)
            i += 1
    r += "$"
    return _re.compile(r, _re.IGNORECASE)


# ── Device proxy classes ──

class MsgProxy:
    """High-level handle for a single firmware Message.

    Returned by ``device.msg("name")``. Name may be an OSC wildcard pattern
    (e.g. ``"accel*"``) — the firmware dispatches to all matching messages.
    """

    def __init__(self, name, host, port, dev_name, engine):
        self._name   = name
        self._host   = host
        self._port   = port
        self._dev    = dev_name
        self._engine = engine

    def _cmd(self, command, payload=None):
        address = f"/annieData/{self._dev}/msg/{self._name}/{command}"
        self._engine.send_message(
            self._host, self._port, address,
            [payload] if payload is not None else None,
        )

    def enable(self):
        """Enable this message."""
        self._cmd("enable")

    def disable(self):
        """Disable (mute) this message."""
        self._cmd("disable")

    def delete(self):
        """Delete this message from the device."""
        self._cmd("delete")

    def info(self):
        """Ask the device to reply with this message's current config."""
        self._cmd("info")

    def assign(self, **kwargs):
        """Create or update the message with keyword config values.

        Keys map directly to firmware config: value, ip, port, adr,
        low, high, enabled, scene, ori_only, ori_not, ternori.

        Example::

            device.msg("accelX").assign(
                value="accelX", ip="192.168.1.50",
                port=9000, adr="/light/x", low=0, high=255
            )
        """
        cfg = ", ".join(f"{k}:{v}" for k, v in kwargs.items())
        address = f"/annieData/{self._dev}/msg/{self._name}/assign"
        self._engine.send_message(self._host, self._port, address, [cfg])

    def __repr__(self):
        return f"<MsgProxy {self._name!r} @ {self._dev}>"


class SceneProxy:
    """High-level handle for a single firmware Scene.

    Returned by ``device.scene("name")``. Name may be an OSC wildcard pattern.
    """

    def __init__(self, name, host, port, dev_name, engine):
        self._name   = name
        self._host   = host
        self._port   = port
        self._dev    = dev_name
        self._engine = engine

    def _cmd(self, command, payload=None):
        address = f"/annieData/{self._dev}/scene/{self._name}/{command}"
        self._engine.send_message(
            self._host, self._port, address,
            [payload] if payload is not None else None,
        )

    def start(self):
        """Start streaming (launch the scene's send task)."""
        self._cmd("start")

    def stop(self):
        """Stop streaming."""
        self._cmd("stop")

    def enable(self):
        """Enable the scene."""
        self._cmd("enable")

    def disable(self):
        """Disable the scene."""
        self._cmd("disable")

    def delete(self):
        """Delete this scene from the device."""
        self._cmd("delete")

    def info(self):
        """Ask the device to reply with this scene's current config."""
        self._cmd("info")

    def period(self, ms):
        """Set the send interval in milliseconds (20–60000)."""
        self._cmd("period", str(int(ms)))

    def add_msg(self, *names):
        """Add one or more messages to this scene by name."""
        self._cmd("addMsg", ",".join(str(n) for n in names))

    def remove_msg(self, name):
        """Remove a message from this scene."""
        self._cmd("removeMsg", str(name))

    def solo(self, name):
        """Solo a single message within this scene."""
        self._cmd("solo", str(name))

    def unsolo(self):
        """Restore all messages from solo state."""
        self._cmd("unsolo")

    def enable_all(self):
        """Enable every message in this scene."""
        self._cmd("enableAll")

    def set_all(self, **kwargs):
        """Apply config values to all messages in this scene."""
        cfg = ", ".join(f"{k}:{v}" for k, v in kwargs.items())
        self._cmd("setAll", cfg)

    def adr_mode(self, mode):
        """Set address composition mode.

        mode: ``'fallback'`` | ``'override'`` | ``'prepend'`` | ``'append'``
        """
        self._cmd("adrMode", str(mode))

    def override(self, *fields):
        """Set scene-level override fields.

        Pass field names from: ``'ip'``, ``'port'``, ``'adr'``, ``'low'``, ``'high'``.
        Pass no arguments to clear all overrides.
        """
        self._cmd("override", "+".join(fields) if fields else "none")

    def assign(self, **kwargs):
        """Create or update the scene with keyword config values.

        Keys: ip, port, adr, low, high, period, adrmode, override, msgs.
        """
        cfg = ", ".join(f"{k}:{v}" for k, v in kwargs.items())
        address = f"/annieData/{self._dev}/scene/{self._name}/assign"
        self._engine.send_message(self._host, self._port, address, [cfg])

    def __repr__(self):
        return f"<SceneProxy {self._name!r} @ {self._dev}>"


class DeviceProxy:
    """High-level handle for the active TheaterGWD device.

    Available as ``device`` in scripts. Provides factory methods for
    message/scene proxies and convenience wrappers for device-level commands.

    Registry access (``messages()`` / ``scenes()``) returns a snapshot of
    gooey's parsed config cache — the same data shown in the UI tables.
    """

    def __init__(self, device_context, engine, registry, registry_lock,
                 sensor_data=None, sensor_lock=None):
        self._ctx          = device_context   # {"id", "host", "port", "name"}
        self._engine       = engine
        self._registry     = registry
        self._lock         = registry_lock
        self._sensor_data  = sensor_data if sensor_data is not None else {}
        self._sensor_lock  = sensor_lock  if sensor_lock  is not None else threading.Lock()

    # ── Identity ──

    @property
    def name(self):
        """Device name used in OSC addresses (e.g. ``'bart'``)."""
        return self._ctx.get("name", "")

    @property
    def host(self):
        """Device IP address."""
        return self._ctx.get("host", "")

    @property
    def port(self):
        """Device OSC command port."""
        return self._ctx.get("port", 8000)

    # ── Proxy factories ──

    def msg(self, name):
        """Return a MsgProxy for *name* (may be an OSC wildcard pattern)."""
        return MsgProxy(name, self.host, self.port, self.name, self._engine)

    def scene(self, name):
        """Return a SceneProxy for *name* (may be an OSC wildcard pattern)."""
        return SceneProxy(name, self.host, self.port, self.name, self._engine)

    # ── Device-level commands ──

    def _cmd(self, suffix, payload=None):
        address = f"/annieData/{self.name}/{suffix}"
        self._engine.send_message(
            self.host, self.port, address,
            [payload] if payload is not None else None,
        )

    def blackout(self):
        """Stop all scenes immediately."""
        self._cmd("blackout")

    def restore(self):
        """Restart all scenes after a blackout."""
        self._cmd("restore")

    def save(self):
        """Persist the current config to device flash (NVS)."""
        self._cmd("save")

    def load(self):
        """Reload config from device flash."""
        self._cmd("load")

    def dedup(self):
        """Toggle duplicate-suppression on the device."""
        self._cmd("dedup")

    def flush(self):
        """Flush a pending transactional batch."""
        self._cmd("flush")

    def list_msgs(self, verbose=False):
        """Ask the device to send back its message list."""
        self._cmd("list/msgs", "verbose=1" if verbose else None)

    def list_scenes(self, verbose=False):
        """Ask the device to send back its scene list."""
        self._cmd("list/scenes", "verbose=1" if verbose else None)

    def list_all(self, verbose=False):
        """Ask the device to send back all objects."""
        self._cmd("list/all", "verbose=1" if verbose else None)

    # ── Registry snapshots ──

    def messages(self):
        """Return a snapshot dict of gooey's tracked message configs.

        Keys are message names; values are dicts with fields like
        ``value``, ``ip``, ``port``, ``adr``, ``low``, ``high``, ``enabled``.
        Only messages that gooey has seen (via list or info replies) appear here.
        """
        did = self._ctx.get("id", "")
        with self._lock:
            dev = self._registry.get(did, {})
            return dict(dev.get("messages", {}))

    def scenes(self):
        """Return a snapshot dict of gooey's tracked scene configs.

        Keys are scene names; values are dicts with fields like
        ``period``, ``msgs``, ``enabled``, ``adrMode``.
        Only scenes that gooey has seen appear here.
        """
        did = self._ctx.get("id", "")
        with self._lock:
            dev = self._registry.get(did, {})
            return dict(dev.get("scenes", {}))

    # ── Sensor access (device-scoped) ──

    def sensor(self, name):
        """Get the latest sensor value for *this* device by short name.

        Uses device-scoped storage so two devices with the same sensor name
        (e.g. both sending ``accelX``) don't clobber each other.
        Returns 0.0 if no data has arrived yet.
        """
        scoped = f"{self.name}/{name}"
        with self._sensor_lock:
            data = self._sensor_data.get(scoped)
            if data and data["args"]:
                return data["args"][0]
            # Fallback: unscoped key (works correctly in single-device setups)
            data = self._sensor_data.get(name)
            if data and data["args"]:
                return data["args"][0]
        return 0.0

    def sensors(self):
        """Get all current sensor values for *this* device as a dict.

        Returns ``{"accelX": 0.5, "gyroY": 0.2, ...}`` — only sensors that
        have received data from this specific device.
        """
        prefix = f"{self.name}/"
        result = {}
        with self._sensor_lock:
            for key, data in self._sensor_data.items():
                if key.startswith(prefix) and "/" not in key[len(prefix):] and data["args"]:
                    result[key[len(prefix):]] = data["args"][0]
        return result

    def __repr__(self):
        return f"<DeviceProxy {self.name!r} {self.host}:{self.port}>"


# ── Script runner ──

class ScriptRunner:
    """Execute a user script in a managed thread with OSC helpers."""

    ITERATION_TIMEOUT = 5.0   # max seconds per loop iteration
    MAX_CONSOLE_LINES = 500
    MIN_INTERVAL_MS = 10

    def __init__(self, engine, socketio):
        self.engine = engine
        self.socketio = socketio
        self._thread = None
        self._stop = threading.Event()
        self._running = False
        self._console = []
        self._lock = threading.Lock()
        # Sensor data store: updated by incoming OSC messages
        self._sensor_data = {}
        self._sensor_lock = threading.Lock()
        # Receiver for incoming sensor data
        self._recv_id = None
        # on_osc callback state
        self._osc_callbacks = []        # [(compiled_regex, callback_fn)]
        self._osc_queue = []            # [(address, args)] pending dispatch
        self._osc_lock = threading.Lock()
        # Device context (set per-run, cleared on stop)
        self._device_context = {}
        self._registry = {}
        self._registry_lock = threading.Lock()

    @property
    def running(self):
        return self._running

    def update_sensor(self, address, args):
        """Called by the OSC receiver to update sensor values."""
        with self._sensor_lock:
            ts = time.time()
            entry = {"args": list(args), "time": ts}
            self._sensor_data[address] = entry

            parts = address.strip("/").split("/")
            if args:
                # Short unscoped key: "accelX" — last write wins across devices.
                # Kept for backward-compat with single-device scripts.
                short = parts[-1]
                self._sensor_data[short] = entry

                # Device-scoped key: "bart/accelX"
                # /annieData/{device}/... → parts[0]="annieData", parts[1]=device
                if len(parts) >= 2 and parts[0].lower() == "anniedata":
                    self._sensor_data[f"{parts[1]}/{short}"] = entry

        # Enqueue for on_osc dispatch (runs on script thread, not here)
        with self._osc_lock:
            if self._osc_callbacks:
                self._osc_queue.append((address, list(args)))

    def _drain_osc_queue(self):
        """Dispatch queued OSC messages to registered on_osc callbacks.

        Called from the script thread between loop iterations to keep all
        user callback execution off the OSC receiver thread.
        """
        with self._osc_lock:
            pending, self._osc_queue = self._osc_queue, []
        for address, args in pending:
            # Snapshot callbacks to allow on_osc() calls from within a callback
            with self._osc_lock:
                callbacks = list(self._osc_callbacks)
            for regex, cb in callbacks:
                if regex.match(address):
                    try:
                        cb(address, args)
                    except Exception:
                        self._emit_console(traceback.format_exc(), "error")

    def _emit_console(self, text, level="info"):
        ts = datetime.now(timezone.utc).strftime("%H:%M:%S")
        entry = {"time": ts, "text": str(text), "level": level}
        with self._lock:
            self._console.append(entry)
            if len(self._console) > self.MAX_CONSOLE_LINES:
                self._console = self._console[-self.MAX_CONSOLE_LINES:]
        self.socketio.emit("script_output", entry)

    def _build_namespace(self, mode, interval_ms):
        """Build the restricted namespace exposed to user scripts."""
        runner = self
        state = {}
        start_time = time.time()
        last_time = [start_time]

        def osc_send(host, port, address, *args):
            """Send an OSC message to a destination."""
            runner.engine.send_message(str(host), int(port), str(address),
                                       list(args) if args else None)

        def sensor(name):
            """Get the latest sensor value by short name (e.g. 'accelX').

            With multiple devices, use ``device.sensor(name)`` instead —
            this unscoped version returns whichever device wrote last.
            """
            with runner._sensor_lock:
                data = runner._sensor_data.get(name)
                if data and data["args"]:
                    return data["args"][0]
            return 0.0

        def sensors():
            """Get all current sensor values as a dict (unscoped, all devices).

            With multiple devices, use ``device.sensors()`` instead —
            this returns a merged view where same-named sensors from different
            devices overwrite each other.
            """
            result = {}
            with runner._sensor_lock:
                for key, data in runner._sensor_data.items():
                    if "/" not in key and data["args"]:
                        result[key] = data["args"][0]
            return result

        def osc_data(address):
            """Get raw args list for a full OSC address."""
            with runner._sensor_lock:
                data = runner._sensor_data.get(address)
                if data:
                    return list(data["args"])
            return []

        def script_print(*args, **kwargs):
            sep = kwargs.get("sep", " ")
            runner._emit_console(sep.join(str(a) for a in args))

        def script_log(*args):
            script_print(*args)

        def clamp(val, lo=0.0, hi=1.0):
            return max(lo, min(hi, val))

        def remap(val, in_lo, in_hi, out_lo, out_hi):
            """Linearly remap a value from one range to another."""
            if in_hi == in_lo:
                return out_lo
            t = (val - in_lo) / (in_hi - in_lo)
            return out_lo + t * (out_hi - out_lo)

        def elapsed():
            return time.time() - start_time

        def dt():
            now = time.time()
            d = now - last_time[0]
            last_time[0] = now
            return d

        def on_osc(pattern, callback):
            """Register a callback for incoming OSC messages matching pattern.

            pattern supports OSC 1.0 wildcards: * ? [abc] {alt1,alt2}
            callback receives: callback(address, args_list)

            Callbacks are dispatched between loop iterations (not in the
            OSC receiver thread). Register once; re-registering in a loop
            body will accumulate duplicate handlers.

            Example::

                def got_tilt(address, args):
                    print("tilt:", args[0] if args else "?")

                on_osc("/annieData/*/stream/roll", got_tilt)
            """
            regex = _osc_pattern_to_regex(str(pattern))
            with runner._osc_lock:
                runner._osc_callbacks.append((regex, callback))

        # Device proxy — bound to the context active when this script started
        _dev_proxy = DeviceProxy(
            runner._device_context,
            runner.engine,
            runner._registry,
            runner._registry_lock,
            sensor_data=runner._sensor_data,
            sensor_lock=runner._sensor_lock,
        )

        # Top-level convenience aliases for device registry snapshots
        def messages():
            """Snapshot of gooey's tracked message configs for the active device."""
            return _dev_proxy.messages()

        def scenes():
            """Snapshot of gooey's tracked scene configs for the active device."""
            return _dev_proxy.scenes()

        ns = {
            "__builtins__": dict(_SAFE_BUILTINS),
            # OSC
            "osc_send": osc_send,
            "sensor": sensor,
            "sensors": sensors,
            "osc_data": osc_data,
            # Output
            "print": script_print,
            "log": script_log,
            # State
            "state": state,
            # Time
            "elapsed": elapsed,
            "dt": dt,
            "time": time,
            # Math
            "math": math,
            "sqrt": math.sqrt,
            "sin": math.sin,
            "cos": math.cos,
            "tan": math.tan,
            "atan2": math.atan2,
            "pi": math.pi,
            "abs": abs,
            "clamp": clamp,
            "remap": remap,
            # Device proxy
            "device": _dev_proxy,
            "messages": messages,
            "scenes": scenes,
            # Event callbacks
            "on_osc": on_osc,
            # Expanded builtins
            "random": _random,
            "deque": _collections.deque,
        }
        return ns

    def run(self, code, mode="loop", interval_ms=50, listen_port=None,
            device_context=None, registry=None, registry_lock=None):
        """Start executing a user script.

        Args:
            code: Python source code string.
            mode: 'loop' (repeat at interval) or 'once' (run once).
            interval_ms: Loop interval in ms (loop mode only).
            listen_port: OSC port to listen on for incoming sensor data.
            device_context: Dict with 'id', 'host', 'port', 'name' for the
                active device. Populates the ``device`` proxy in the namespace.
            registry: Reference to gooey's _device_registry dict.
            registry_lock: Threading lock protecting the registry.
        """
        if self._running:
            self.stop()

        interval_ms = max(self.MIN_INTERVAL_MS, int(interval_ms))
        self._stop.clear()
        self._running = True

        # Store device context for this run
        self._device_context = device_context or {}
        self._registry = registry if registry is not None else {}
        self._registry_lock = registry_lock if registry_lock is not None else threading.Lock()
        with self._osc_lock:
            self._osc_callbacks = []
            self._osc_queue = []

        # Start a receiver to capture incoming sensor data
        if listen_port:
            self._start_sensor_receiver(int(listen_port))

        # Compile the code upfront to catch syntax errors early
        try:
            compiled = compile(code, "<script>", "exec")
        except SyntaxError as e:
            self._emit_console(f"SyntaxError: {e}", "error")
            self._running = False
            self.socketio.emit("script_stopped", {"reason": "syntax_error"})
            return

        def _run_thread():
            ns = self._build_namespace(mode, interval_ms)
            self._emit_console(
                f"Script started ({mode} mode"
                + (f", {interval_ms}ms" if mode == "loop" else "")
                + ")"
            )

            if mode == "once":
                try:
                    exec(compiled, ns)
                except Exception:
                    self._emit_console(traceback.format_exc(), "error")
                self._drain_osc_queue()
                self._emit_console("Script finished.")
            else:
                iteration = 0
                while not self._stop.is_set():
                    iteration += 1
                    try:
                        exec(compiled, ns)
                    except Exception:
                        self._emit_console(traceback.format_exc(), "error")
                        # Don't kill the loop on errors — keep going
                    self._drain_osc_queue()
                    self._stop.wait(interval_ms / 1000.0)

                self._emit_console("Script stopped.")

            self._running = False
            self.socketio.emit("script_stopped", {"reason": "finished"})

        self._thread = threading.Thread(target=_run_thread, daemon=True)
        self._thread.start()

    def stop(self):
        """Stop the running script."""
        if self._running:
            self._stop.set()
            self._running = False
            with self._osc_lock:
                self._osc_callbacks.clear()
                self._osc_queue.clear()
            self._device_context = {}
            self._registry = {}
            if self._recv_id:
                try:
                    self.engine.stop_receiver(self._recv_id)
                except Exception:
                    pass
                self._recv_id = None
        self.socketio.emit("script_stopped", {"reason": "user"})

    def get_console(self):
        with self._lock:
            return list(self._console)

    def clear_console(self):
        with self._lock:
            self._console.clear()

    def _start_sensor_receiver(self, port):
        """Start an OSC receiver that populates sensor data."""
        recv_id = f"script-sensor-{port}"

        # If already listening on this port, reuse
        if self._recv_id == recv_id:
            return

        if self._recv_id:
            try:
                self.engine.stop_receiver(self._recv_id)
            except Exception:
                pass

        from pythonosc import osc_server, dispatcher as osc_dispatcher

        disp = osc_dispatcher.Dispatcher()

        def _handler(address, *args):
            self.update_sensor(address, args)

        disp.set_default_handler(_handler)

        try:
            server = osc_server.ThreadingOSCUDPServer(
                ("0.0.0.0", port), disp)
        except OSError:
            # Port already in use — try tapping into existing receiver data
            self._emit_console(
                f"Port {port} already in use — sensor data will come from "
                "the main listener if active.", "warn"
            )
            self._recv_id = None
            return

        self.engine._receivers[recv_id] = {
            "server": server, "port": port, "filter": "",
        }
        self._recv_id = recv_id

        t = threading.Thread(target=server.serve_forever, daemon=True)
        t.start()
        self._emit_console(f"Listening for sensor data on port {port}")
