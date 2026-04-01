# Homebrew Guide

Reference guide for installing, running, updating, and managing the annieData Control Center via Homebrew on macOS.

---

## Table of Contents

- [Prerequisites](#prerequisites)
- [Installation](#installation)
- [Running annieData](#running-anniedata)
- [Updating](#updating)
- [Uninstalling](#uninstalling)
- [How It Works](#how-it-works)
- [Troubleshooting](#troubleshooting)

---

## Prerequisites

### macOS version

macOS 12 Monterey or later. Older versions may work but are untested.

### Homebrew

If Homebrew is not yet installed:

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

After installation, follow any instructions Homebrew prints about adding it to the `PATH` — this is especially important on Apple Silicon Macs.

Verify:

```bash
brew --version
```

### Python

Homebrew installs Python automatically as a dependency — no separate installation required.

---

## Installation

```bash
brew install halfsohappy/theatergwd/gooey
```

This automatically registers the TheaterGWD tap and installs annieData in one step. Future `brew upgrade gooey` commands will find it automatically.

### Verify

```bash
gooey --help
```

Expected output:

```
usage: run.py [-h] [--port PORT] [--host HOST] [--no-browser] [--debug]

TheaterGWD Control Center

options:
  -h, --help   show this help message and exit
  --port PORT  Web server port (default: 5000)
  --host HOST  Web server host (default: 127.0.0.1)
  --no-browser Don't auto-open browser on startup
  --debug      Enable debug mode
```

---

## Running annieData

### Basic launch

```bash
gooey
```

The default browser opens to [http://127.0.0.1:5000](http://127.0.0.1:5000).

### Custom port

```bash
gooey --port 8080
```

### Allow network access

Exposes the control center to other devices on the local network — useful for tablets or additional machines in the theater:

```bash
gooey --host 0.0.0.0
```

Then open `http://<host-mac-ip>:5000` on the other device.

### Headless mode (no browser)

```bash
gooey --no-browser
```

### Debug mode

```bash
gooey --debug
```

### Combine flags

```bash
gooey --host 0.0.0.0 --port 8080 --no-browser
```

### Stop

Press **Ctrl+C** in the terminal to stop the server.

---

## Updating

```bash
brew update                   # Refresh all tap metadata
brew upgrade gooey            # Upgrade annieData to latest
```

To reinstall from scratch (e.g. after a major update):

```bash
brew reinstall gooey
```

---

## Uninstalling

### Remove annieData

```bash
brew uninstall gooey
```

### Remove the tap (optional)

```bash
brew untap halfsohappy/theatergwd
```

This removes the tap registration. Re-installation is possible at any time.

### Full cleanup

```bash
brew uninstall gooey
brew untap halfsohappy/theatergwd
brew cleanup
```

---

## How It Works

When you `brew install gooey`, Homebrew:

1. Clones the TheaterGWD repository
2. Copies the `gooey/` application files into Homebrew's `libexec` directory
3. Creates an isolated Python virtual environment
4. Installs Flask, Flask-SocketIO, and python-osc into the virtual environment
5. Creates a `gooey` launcher script in your `PATH`

> The CLI command and package name remain `gooey` / `gooey-theatergwd`.

The virtual environment is fully managed by Homebrew — it does not conflict with the system Python or other Python projects.

### Where things live

| Item | Location |
|------|----------|
| `gooey` command | `$(brew --prefix)/bin/gooey` |
| Application files | `$(brew --prefix)/Cellar/gooey/<version>/libexec/` |
| Python venv | `$(brew --prefix)/Cellar/gooey/<version>/libexec/venv/` |

---

## Troubleshooting

### "command not found: gooey"

Ensure Homebrew's `bin` directory is in the `PATH`.

**Intel Macs:**
```bash
echo 'export PATH="/usr/local/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

**Apple Silicon Macs (M1/M2/M3/M4):**
```bash
echo 'export PATH="/opt/homebrew/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

### "Error: python@3 is not installed"

```bash
brew install python@3
brew reinstall gooey
```

### Port 5000 already in use

On macOS Ventura and later, AirPlay Receiver uses port 5000 by default.

**Option A:** Use a different port:
```bash
gooey --port 5001
```

**Option B:** Disable AirPlay Receiver:
1. Open **System Settings** → **General** → **AirDrop & Handoff**
2. Turn off **AirPlay Receiver**

### Browser doesn't open

Some terminal environments do not support auto-opening URLs. Navigate manually to:

```
http://127.0.0.1:5000
```

Or suppress the attempt with `--no-browser`:
```bash
gooey --no-browser
```

### Tap fails / permission denied

If `brew tap` fails, verify access to the repository:

```bash
git ls-remote https://github.com/halfsohappy/TheaterGWD
```

If that fails, the repository may be private. Request access from the owner, or install via the [manual method](installation.md#manual-install) using a personal access token.

### Virtual environment is corrupted

Reinstall to get a fresh venv:

```bash
brew reinstall gooey
```

### Upgrade fails

```bash
brew untap halfsohappy/theatergwd
brew install halfsohappy/theatergwd/gooey
```

---

## Next Steps

- [Quick Start Guide](quickstart.md) — first-time UI walkthrough
- [Troubleshooting](troubleshooting.md) — more general troubleshooting
- [Installation Guide](installation.md) — non-Homebrew installation methods
