"""Simulated OSC engine for UI design/development.

Replaces the real pythonosc-backed engine with a simulation layer that:
- Never opens UDP sockets (no OS permission prompts, no blocked calls)
- Returns realistic responses after a short delay so loading states are visible
- Emits fake incoming OSC messages on active receivers/bridges so the message
  log populates and real-time UI components can be exercised
"""

import random
import threading
import time
from datetime import datetime, timezone


# Fake OSC addresses and value shapes used by the message simulator
_FAKE_ADDRESSES = [
    ("/sensor/accel/x",   lambda: round(random.uniform(-1.0, 1.0), 4)),
    ("/sensor/accel/y",   lambda: round(random.uniform(-1.0, 1.0), 4)),
    ("/sensor/accel/z",   lambda: round(random.uniform(0.85, 1.15), 4)),
    ("/sensor/gyro/x",    lambda: round(random.uniform(-0.05, 0.05), 4)),
    ("/sensor/gyro/y",    lambda: round(random.uniform(-0.05, 0.05), 4)),
    ("/sensor/gyro/z",    lambda: round(random.uniform(-0.05, 0.05), 4)),
    ("/sensor/temp",      lambda: round(random.uniform(22.0, 26.0), 2)),
    ("/device/heartbeat", lambda: 1),
    ("/device/battery",   lambda: round(random.uniform(0.6, 1.0), 3)),
    ("/cue/trigger",      lambda: random.randint(1, 32)),
    ("/dmx/channel",      lambda: random.randint(0, 255)),
]

_SIM_INTERVAL = 2.0   # seconds between simulated incoming messages
_SEND_DELAY   = 0.15  # seconds to sleep before returning a send result


class OSCEngine:
    """Simulated OSC engine — same public API as the real one."""

    FLOAT_PRECISION = 6

    def __init__(self, socketio):
        self.socketio = socketio
        self._receivers = {}   # recv_id -> {port, filter, stop_event}
        self._bridges   = {}   # bridge_id -> {in_port, out_host, out_port, filter, stop_event}
        self._senders   = {}   # send_id -> {host, port, address, stop_event}
        self._lock = threading.Lock()
        self._message_log = []
        self._max_log = 500

    # ── Internal helpers ──────────────────────────────────────────────────────

    def _timestamp(self):
        return datetime.now(timezone.utc).strftime("%H:%M:%S.%f")[:-3]

    def _log_message(self, direction, address, args, source="", dest=""):
        entry = {
            "time":      self._timestamp(),
            "direction": direction,
            "address":   address,
            "args":      [self._serialize_arg(a) for a in args],
            "source":    source,
            "dest":      dest,
        }
        with self._lock:
            self._message_log.append(entry)
            if len(self._message_log) > self._max_log:
                self._message_log = self._message_log[-self._max_log:]
        self.socketio.emit("osc_message", entry)
        return entry

    @staticmethod
    def _serialize_arg(arg):
        if isinstance(arg, float):
            return {"type": "f", "value": round(arg, OSCEngine.FLOAT_PRECISION)}
        elif isinstance(arg, int):
            return {"type": "i", "value": arg}
        elif isinstance(arg, str):
            return {"type": "s", "value": arg}
        elif isinstance(arg, bytes):
            return {"type": "b", "value": arg.hex()}
        else:
            return {"type": "s", "value": str(arg)}

    def _fake_message_loop(self, stop_event, port, filter_str, direction,
                           source="", dest=""):
        """Background thread that emits simulated OSC messages."""
        filter_text = filter_str.strip().lstrip("-") if filter_str else ""
        exclude = filter_str.strip().startswith("-") if filter_str else False

        while not stop_event.wait(_SIM_INTERVAL):
            address, fn = random.choice(_FAKE_ADDRESSES)
            if filter_text:
                match = filter_text in address
                if exclude and match:
                    continue
                if not exclude and not match:
                    continue
            self._log_message(direction, address, [fn()],
                              source=source, dest=dest)

    # ── Send ──────────────────────────────────────────────────────────────────

    def send_message(self, host, port, address, args=None):
        """Simulate sending a single OSC message (~150 ms delay)."""
        parsed = self._parse_args(args) if args else []
        time.sleep(_SEND_DELAY)
        self._log_message("send", address, parsed, dest=f"{host}:{port}")
        return {"status": "ok"}

    def start_repeated_send(self, send_id, host, port, address, args=None,
                            interval_ms=1000):
        """Simulate repeated OSC sending."""
        if send_id in self._senders:
            self.stop_repeated_send(send_id)

        stop_event = threading.Event()
        parsed = self._parse_args(args) if args else []
        self._senders[send_id] = {
            "stop": stop_event, "host": host, "port": port, "address": address
        }

        def _loop():
            while not stop_event.is_set():
                time.sleep(_SEND_DELAY)
                self._log_message("send", address, parsed, dest=f"{host}:{port}")
                stop_event.wait(interval_ms / 1000.0)

        threading.Thread(target=_loop, daemon=True).start()
        return {"status": "ok", "id": send_id}

    def stop_repeated_send(self, send_id):
        if send_id in self._senders:
            self._senders[send_id]["stop"].set()
            del self._senders[send_id]
            return {"status": "ok"}
        return {"status": "error", "message": "Send not found"}

    # ── Receive ───────────────────────────────────────────────────────────────

    def start_receiver(self, recv_id, port, filter_str=""):
        if recv_id in self._receivers:
            self.stop_receiver(recv_id)

        stop_event = threading.Event()
        self._receivers[recv_id] = {
            "port": port, "filter": filter_str, "stop": stop_event
        }

        threading.Thread(
            target=self._fake_message_loop,
            args=(stop_event, port, filter_str, "recv",
                  f"sim:{port}", ""),
            daemon=True,
        ).start()

        self.socketio.emit("receiver_started",
                           {"id": recv_id, "port": port, "filter": filter_str})
        return {"status": "ok", "id": recv_id}

    def start_remote_receiver(self, recv_id, port):
        """Simulate the remote-control receiver."""
        if recv_id in self._receivers:
            if self._receivers[recv_id]["port"] == int(port):
                return {"status": "ok", "id": recv_id}
            self.stop_receiver(recv_id)

        stop_event = threading.Event()
        self._receivers[recv_id] = {"port": int(port), "filter": "", "stop": stop_event}

        def _remote_loop():
            while not stop_event.wait(_SIM_INTERVAL):
                address, fn = random.choice(_FAKE_ADDRESSES)
                serialized = [self._serialize_arg(fn())]
                self.socketio.emit("remote_reply",
                                   {"address": address, "args": serialized},
                                   to="remote_clients")

        threading.Thread(target=_remote_loop, daemon=True).start()
        return {"status": "ok", "id": recv_id}

    def stop_receiver(self, recv_id):
        if recv_id in self._receivers:
            self._receivers[recv_id]["stop"].set()
            del self._receivers[recv_id]
            self.socketio.emit("receiver_stopped", {"id": recv_id})
            return {"status": "ok"}
        return {"status": "error", "message": "Receiver not found"}

    # ── Bridge ────────────────────────────────────────────────────────────────

    def start_bridge(self, bridge_id, in_port, out_host, out_port,
                     filter_str=""):
        if bridge_id in self._bridges:
            self.stop_bridge(bridge_id)

        stop_event = threading.Event()
        self._bridges[bridge_id] = {
            "in_port": in_port, "out_host": out_host, "out_port": out_port,
            "filter": filter_str, "stop": stop_event,
        }

        threading.Thread(
            target=self._fake_message_loop,
            args=(stop_event, in_port, filter_str, "bridge",
                  f"sim:{in_port}", f"{out_host}:{out_port}"),
            daemon=True,
        ).start()

        self.socketio.emit("bridge_started", {"id": bridge_id})
        return {"status": "ok", "id": bridge_id}

    def stop_bridge(self, bridge_id):
        if bridge_id in self._bridges:
            self._bridges[bridge_id]["stop"].set()
            del self._bridges[bridge_id]
            self.socketio.emit("bridge_stopped", {"id": bridge_id})
            return {"status": "ok"}
        return {"status": "error", "message": "Bridge not found"}

    # ── Batch send ────────────────────────────────────────────────────────────

    def send_json_messages(self, host, port, messages, interval_ms=0):
        results = []
        for msg in messages:
            address = msg.get("address", "")
            args = msg.get("args", [])
            if not address or not address.startswith("/"):
                results.append({"status": "error",
                                 "message": f"Invalid address: {address}"})
                continue
            time.sleep(_SEND_DELAY)
            parsed = self._parse_args(args) if args else []
            self._log_message("send", address, parsed, dest=f"{host}:{port}")
            results.append({"status": "ok", "address": address})
            if interval_ms > 0:
                time.sleep(interval_ms / 1000.0)
        return {"status": "ok", "results": results}

    # ── Log / status / control ────────────────────────────────────────────────

    def get_log(self):
        with self._lock:
            return list(self._message_log)

    def clear_log(self):
        with self._lock:
            self._message_log.clear()
        return {"status": "ok"}

    def get_status(self):
        return {
            "receivers": {
                k: {"port": v["port"], "filter": v["filter"]}
                for k, v in self._receivers.items()
            },
            "bridges": {
                k: {
                    "in_port":  v["in_port"],
                    "out_host": v["out_host"],
                    "out_port": v["out_port"],
                    "filter":   v["filter"],
                }
                for k, v in self._bridges.items()
            },
            "senders": {
                k: {"host": v["host"], "port": v["port"], "address": v["address"]}
                for k, v in self._senders.items()
            },
            "log_count": len(self._message_log),
        }

    def stop_all(self):
        for rid in list(self._receivers.keys()):
            self.stop_receiver(rid)
        for bid in list(self._bridges.keys()):
            self.stop_bridge(bid)
        for sid in list(self._senders.keys()):
            self.stop_repeated_send(sid)
        return {"status": "ok"}

    # ── Argument parsing (unchanged from real engine) ─────────────────────────

    @staticmethod
    def _parse_args(args):
        if args is None:
            return []
        if isinstance(args, list):
            return [OSCEngine._coerce_arg(a) for a in args]
        if isinstance(args, str):
            return [OSCEngine._coerce_arg(p) for p in args.split()]
        return [args]

    @staticmethod
    def _coerce_arg(val):
        if isinstance(val, (int, float)):
            return val
        s = str(val).strip()
        if s.startswith('"') and s.endswith('"'):
            return s[1:-1]
        try:
            return int(s)
        except ValueError:
            pass
        try:
            return float(s)
        except ValueError:
            pass
        return s
