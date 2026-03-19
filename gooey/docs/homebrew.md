# Homebrew Guide

Complete reference for installing, running, updating, and managing Gooey via Homebrew on macOS.

---

## Table of Contents

- [Prerequisites](#prerequisites)
- [Installation](#installation)
- [Running Gooey](#running-gooey)
- [Updating](#updating)
- [Uninstalling](#uninstalling)
- [How It Works](#how-it-works)
- [Troubleshooting](#troubleshooting)

---

## Prerequisites

### macOS version

macOS 12 Monterey or later is recommended. Older versions may work but are untested.

### Homebrew

If you don't have Homebrew yet, install it:

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

After installation, follow any instructions Homebrew prints about adding it to your `PATH` (this is especially important on Apple Silicon Macs).

Verify it's working:

```bash
brew --version
```

### Python

Homebrew installs Python automatically as a dependency — you don't need to install it separately.

---

## Installation

### Option A: Tap and install (recommended)

```bash
brew tap halfsohappy/theatergwd https://github.com/halfsohappy/TheaterGWD
brew install gooey
```

The `tap` command registers the TheaterGWD repository as a Homebrew tap. You only need to do this once — future `brew install` / `brew upgrade` commands will find it automatically.

### Option B: One-liner

```bash
brew install halfsohappy/theatergwd/gooey
```

This taps the repository and installs in a single command.

### Verify

```bash
gooey --help
```

You should see:

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

## Running Gooey

### Basic launch

```bash
gooey
```

Your default browser opens to [http://127.0.0.1:5000](http://127.0.0.1:5000).

### Custom port

```bash
gooey --port 8080
```

### Allow network access

To let other devices on your local network access the control center (useful for tablets or second computers in a theater):

```bash
gooey --host 0.0.0.0
```

Then open `http://<your-mac-ip>:5000` on the other device.

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
brew upgrade gooey            # Upgrade Gooey to latest
```

To reinstall from scratch (e.g. after a major update):

```bash
brew reinstall gooey
```

---

## Uninstalling

### Remove Gooey

```bash
brew uninstall gooey
```

### Remove the tap (optional)

```bash
brew untap halfsohappy/theatergwd
```

This removes the tap registration. You can always re-tap later.

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

The virtual environment is fully managed by Homebrew — it won't conflict with your system Python or any other Python projects.

### Where things live

| Item | Location |
|------|----------|
| `gooey` command | `$(brew --prefix)/bin/gooey` |
| Application files | `$(brew --prefix)/Cellar/gooey/<version>/libexec/` |
| Python venv | `$(brew --prefix)/Cellar/gooey/<version>/libexec/venv/` |

---

## Troubleshooting

### "command not found: gooey"

Make sure Homebrew's `bin` directory is in your `PATH`.

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

Some terminal environments don't support auto-opening URLs. Open your browser manually and go to:

```
http://127.0.0.1:5000
```

Or use `--no-browser` and open it yourself:
```bash
gooey --no-browser
```

### Tap fails / permission denied

If `brew tap` fails, check that you can access the repository:

```bash
git ls-remote https://github.com/halfsohappy/TheaterGWD
```

If that fails, the repository may be private. Ask the owner for access, or install via the [manual method](installation.md#manual-install) using a personal access token.

### Virtual environment is corrupted

Reinstall to get a fresh virtual environment:

```bash
brew reinstall gooey
```

### Upgrade fails

```bash
brew untap halfsohappy/theatergwd
brew tap halfsohappy/theatergwd https://github.com/halfsohappy/TheaterGWD
brew install gooey
```

---

## Next Steps

- [Quick Start Guide](quickstart.md) — first-time UI walkthrough
- [Troubleshooting](troubleshooting.md) — more general troubleshooting
- [Installation Guide](installation.md) — non-Homebrew installation methods
