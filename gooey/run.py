#!/usr/bin/env python3
"""Entry point for TheaterGWD Control Center."""

import argparse
import os
import threading
try:
    import webbrowser
except ImportError:
    webbrowser = None

from app.main import create_app


def main():
    parser = argparse.ArgumentParser(
        description="TheaterGWD Control Center",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
examples:
  python run.py                  # Start on default port 5000
  python run.py --port 8080      # Start on port 8080
  python run.py --no-browser     # Don't auto-open browser
""",
    )
    parser.add_argument(
        "--port", type=int, default=int(os.environ.get("PORT", 5000)),
        help="Web server port (default: $PORT env var, or 5000)",
    )
    parser.add_argument(
        "--host", type=str, default="127.0.0.1",
        help="Web server host (default: 127.0.0.1)",
    )
    parser.add_argument(
        "--no-browser", action="store_true",
        help="Don't auto-open browser on startup",
    )
    parser.add_argument(
        "--debug", action="store_true",
        help="Enable debug mode",
    )

    args = parser.parse_args()
    app, socketio = create_app()

    url = f"http://{args.host}:{args.port}"
    print(f"\n  TheaterGWD Control Center")
    print(f"  ────────────────────────")
    print(f"  Running at: {url}")
    if args.host == "0.0.0.0":
        print(f"  Accessible from other devices on your network")
    print(f"  Press Ctrl+C to quit\n")

    if not args.no_browser and webbrowser:
        threading.Timer(1.0, lambda: webbrowser.open(url)).start()

    socketio.run(
        app,
        host=args.host,
        port=args.port,
        debug=args.debug,
        use_reloader=False,
        allow_unsafe_werkzeug=True,
    )


if __name__ == "__main__":
    main()
