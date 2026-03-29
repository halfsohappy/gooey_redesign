"""Headless Flask/SocketIO server — sidecar entry point for Tauri."""
import sys

if getattr(sys, "frozen", False) and sys._MEIPASS not in sys.path:
    sys.path.insert(0, sys._MEIPASS)

from app.main import create_app

HOST = "127.0.0.1"
PORT = 5254


def main():
    app, socketio = create_app()
    print(f"GOOEY_SERVER_READY http://{HOST}:{PORT}", flush=True)
    socketio.run(app, host=HOST, port=PORT, use_reloader=False, allow_unsafe_werkzeug=True)


if __name__ == "__main__":
    main()
