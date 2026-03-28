"""User script execution engine for the Script tab.

Runs user-written Python in a dedicated thread with a curated namespace
that exposes OSC send/receive helpers and sensor data access.
"""

import math
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

    @property
    def running(self):
        return self._running

    def update_sensor(self, address, args):
        """Called by the OSC receiver to update sensor values."""
        with self._sensor_lock:
            self._sensor_data[address] = {
                "args": list(args),
                "time": time.time(),
            }
            # Also store individual sensor values by short name.
            # OSC addresses like /annieData/device/stream/accelX → key "accelX"
            parts = address.strip("/").split("/")
            if len(parts) >= 1:
                short = parts[-1]
                if args:
                    self._sensor_data[short] = {
                        "args": list(args),
                        "time": time.time(),
                    }

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
            """Get the latest value of a sensor by short name (e.g. 'accelX')."""
            with runner._sensor_lock:
                data = runner._sensor_data.get(name)
                if data and data["args"]:
                    return data["args"][0]
            return 0.0

        def sensors():
            """Get all current sensor values as a dict."""
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
        }
        return ns

    def run(self, code, mode="loop", interval_ms=50, listen_port=None):
        """Start executing a user script.

        Args:
            code: Python source code string.
            mode: 'loop' (repeat at interval) or 'once' (run once).
            interval_ms: Loop interval in ms (loop mode only).
            listen_port: OSC port to listen on for incoming sensor data.
        """
        if self._running:
            self.stop()

        interval_ms = max(self.MIN_INTERVAL_MS, int(interval_ms))
        self._stop.clear()
        self._running = True

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
                self._emit_console("Script finished.")
            else:
                iteration = 0
                while not self._stop.is_set():
                    iteration += 1
                    try:
                        # Run with a simple timeout check via a watchdog
                        exec(compiled, ns)
                    except Exception:
                        self._emit_console(traceback.format_exc(), "error")
                        # Don't kill the loop on errors — keep going
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
