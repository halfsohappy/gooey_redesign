"""OSC message handler: send, receive, bridge, and monitor functionality."""

import threading
import time
from datetime import datetime, timezone

from pythonosc import udp_client, osc_server, dispatcher


class OSCEngine:
    """Core OSC engine managing send/receive/bridge operations."""

    FLOAT_PRECISION = 6

    def __init__(self, socketio):
        self.socketio = socketio
        self._receivers = {}
        self._bridges = {}
        self._senders = {}
        self._lock = threading.Lock()
        self._message_log = []
        self._max_log = 500

    def _timestamp(self):
        return datetime.now(timezone.utc).strftime("%H:%M:%S.%f")[:-3]

    def _log_message(self, direction, address, args, source="", dest=""):
        entry = {
            "time": self._timestamp(),
            "direction": direction,
            "address": address,
            "args": [self._serialize_arg(a) for a in args],
            "source": source,
            "dest": dest,
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

    def send_message(self, host, port, address, args=None):
        """Send a single OSC message."""
        try:
            client = udp_client.SimpleUDPClient(host, int(port))
            parsed = self._parse_args(args) if args else []
            client.send_message(address, parsed)
            self._log_message("send", address, parsed,
                              dest=f"{host}:{port}")
            return {"status": "ok"}
        except Exception as e:
            return {"status": "error", "message": str(e)}

    def start_repeated_send(self, send_id, host, port, address, args=None,
                            interval_ms=1000):
        """Start repeatedly sending an OSC message."""
        if send_id in self._senders:
            self.stop_repeated_send(send_id)

        stop_event = threading.Event()
        self._senders[send_id] = {"stop": stop_event, "host": host,
                                  "port": port, "address": address}

        def _loop():
            client = udp_client.SimpleUDPClient(host, int(port))
            parsed = self._parse_args(args) if args else []
            while not stop_event.is_set():
                try:
                    client.send_message(address, parsed)
                    self._log_message("send", address, parsed,
                                      dest=f"{host}:{port}")
                except Exception:
                    break
                stop_event.wait(interval_ms / 1000.0)

        t = threading.Thread(target=_loop, daemon=True)
        t.start()
        return {"status": "ok", "id": send_id}

    def stop_repeated_send(self, send_id):
        """Stop a repeated send."""
        if send_id in self._senders:
            self._senders[send_id]["stop"].set()
            del self._senders[send_id]
            return {"status": "ok"}
        return {"status": "error", "message": "Send not found"}

    def start_receiver(self, recv_id, port, filter_str=""):
        """Start listening for OSC messages on a port."""
        if recv_id in self._receivers:
            self.stop_receiver(recv_id)

        disp = dispatcher.Dispatcher()

        filter_text = filter_str.strip()
        exclude = filter_text.startswith("-") if filter_text else False
        if exclude:
            filter_text = filter_text[1:]

        def _handler(address, *args):
            if filter_text:
                match = filter_text in address
                if exclude and match:
                    return
                if not exclude and not match:
                    return
            self._log_message("recv", address, list(args),
                              source=f"0.0.0.0:{port}")

        disp.set_default_handler(_handler)

        try:
            server = osc_server.ThreadingOSCUDPServer(
                ("0.0.0.0", int(port)), disp)
        except OSError as e:
            return {"status": "error", "message": str(e)}

        self._receivers[recv_id] = {
            "server": server,
            "port": port,
            "filter": filter_str,
        }

        def _serve():
            server.serve_forever()

        t = threading.Thread(target=_serve, daemon=True)
        t.start()

        self.socketio.emit("receiver_started", {
            "id": recv_id, "port": port, "filter": filter_str})
        return {"status": "ok", "id": recv_id}

    def stop_receiver(self, recv_id):
        """Stop a receiver."""
        if recv_id in self._receivers:
            self._receivers[recv_id]["server"].shutdown()
            del self._receivers[recv_id]
            self.socketio.emit("receiver_stopped", {"id": recv_id})
            return {"status": "ok"}
        return {"status": "error", "message": "Receiver not found"}

    def start_bridge(self, bridge_id, in_port, out_host, out_port,
                     filter_str=""):
        """Start bridging OSC messages."""
        if bridge_id in self._bridges:
            self.stop_bridge(bridge_id)

        client = udp_client.SimpleUDPClient(out_host, int(out_port))
        disp = dispatcher.Dispatcher()

        filter_text = filter_str.strip()
        exclude = filter_text.startswith("-") if filter_text else False
        if exclude:
            filter_text = filter_text[1:]

        def _handler(address, *args):
            if filter_text:
                match = filter_text in address
                if exclude and match:
                    return
                if not exclude and not match:
                    return
            try:
                client.send_message(address, list(args))
                self._log_message("bridge", address, list(args),
                                  source=f"0.0.0.0:{in_port}",
                                  dest=f"{out_host}:{out_port}")
            except Exception:
                pass

        disp.set_default_handler(_handler)

        try:
            server = osc_server.ThreadingOSCUDPServer(
                ("0.0.0.0", int(in_port)), disp)
        except OSError as e:
            return {"status": "error", "message": str(e)}

        self._bridges[bridge_id] = {
            "server": server,
            "in_port": in_port,
            "out_host": out_host,
            "out_port": out_port,
            "filter": filter_str,
        }

        def _serve():
            server.serve_forever()

        t = threading.Thread(target=_serve, daemon=True)
        t.start()

        self.socketio.emit("bridge_started", {"id": bridge_id})
        return {"status": "ok", "id": bridge_id}

    def stop_bridge(self, bridge_id):
        """Stop a bridge."""
        if bridge_id in self._bridges:
            self._bridges[bridge_id]["server"].shutdown()
            del self._bridges[bridge_id]
            self.socketio.emit("bridge_stopped", {"id": bridge_id})
            return {"status": "ok"}
        return {"status": "error", "message": "Bridge not found"}

    def send_json_messages(self, host, port, messages, interval_ms=0):
        """Send multiple OSC messages from JSON data."""
        results = []
        client = udp_client.SimpleUDPClient(host, int(port))
        for msg in messages:
            address = msg.get("address", "")
            args = msg.get("args", [])
            if not address or not address.startswith("/"):
                results.append({"status": "error",
                                "message": f"Invalid address: {address}"})
                continue
            try:
                parsed = self._parse_args(args) if args else []
                client.send_message(address, parsed)
                self._log_message("send", address, parsed,
                                  dest=f"{host}:{port}")
                results.append({"status": "ok", "address": address})
            except Exception as e:
                results.append({"status": "error", "message": str(e)})
            if interval_ms > 0:
                time.sleep(interval_ms / 1000.0)
        return {"status": "ok", "results": results}

    def get_log(self):
        """Get message log."""
        with self._lock:
            return list(self._message_log)

    def clear_log(self):
        """Clear message log."""
        with self._lock:
            self._message_log.clear()
        return {"status": "ok"}

    def get_status(self):
        """Get current engine status."""
        return {
            "receivers": {
                k: {"port": v["port"], "filter": v["filter"]}
                for k, v in self._receivers.items()
            },
            "bridges": {
                k: {
                    "in_port": v["in_port"],
                    "out_host": v["out_host"],
                    "out_port": v["out_port"],
                    "filter": v["filter"],
                }
                for k, v in self._bridges.items()
            },
            "senders": {
                k: {"host": v["host"], "port": v["port"],
                     "address": v["address"]}
                for k, v in self._senders.items()
            },
            "log_count": len(self._message_log),
        }

    def stop_all(self):
        """Stop all receivers, bridges, and senders."""
        for rid in list(self._receivers.keys()):
            self.stop_receiver(rid)
        for bid in list(self._bridges.keys()):
            self.stop_bridge(bid)
        for sid in list(self._senders.keys()):
            self.stop_repeated_send(sid)
        return {"status": "ok"}

    @staticmethod
    def _parse_args(args):
        """Parse argument list from various input formats.

        - list: each element is coerced individually (preserves structure)
        - string: split by spaces and coerce each token
        - None: empty list
        """
        if args is None:
            return []
        if isinstance(args, list):
            return [OSCEngine._coerce_arg(a) for a in args]
        if isinstance(args, str):
            parts = args.split()
            return [OSCEngine._coerce_arg(p) for p in parts]
        return [args]

    @staticmethod
    def _coerce_arg(val):
        """Coerce a value to int, float, or string."""
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
