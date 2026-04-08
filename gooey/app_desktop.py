# app_desktop.py  (new file, ~40 lines)
import threading
import time
import subprocess
import sys
import os
import webview          # pip install pywebview
import requests

PORT = 5001  # pick one not likely to be in use

def start_flask():
    """Run the existing Gooey server in a background thread."""
    # If running from PyInstaller bundle, sys.executable is the bundle binary
    # and the server code is in the same package.
    from app.main import create_app, socketio
    app = create_app()
    socketio.run(app, host="127.0.0.1", port=PORT,
                 use_reloader=False, allow_unsafe_werkzeug=True)

def wait_for_server(timeout=15):
    """Poll until Flask is answering requests."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            requests.get(f"http://127.0.0.1:{PORT}/", timeout=0.5)
            return True
        except Exception:
            time.sleep(0.2)
    return False

if __name__ == "__main__":
    t = threading.Thread(target=start_flask, daemon=True)
    t.start()
    if not wait_for_server():
        sys.exit("Gooey server failed to start.")
    webview.create_window(
        "annieData — Gooey",
        f"http://127.0.0.1:{PORT}/",
        width=1400, height=900,
        resizable=True
    )
    webview.start()  # blocks until window is closed; daemon thread dies with it
