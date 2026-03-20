# Gooey – TheaterGWD Control Center

**A professional control interface for [TheaterGWD](https://github.com/halfsohappy/TheaterGWD) sensor devices.**

Built with Python. Runs in your browser. Designed for theater professionals.

> Based on [annieOSC](https://github.com/halfsohappy/annieOSC), redesigned and rebuilt as a dedicated TheaterGWD control center.

---

## Install

### Homebrew (macOS — recommended)

```bash
brew tap halfsohappy/theatergwd https://github.com/halfsohappy/TheaterGWD && brew install gooey
```

Then run:

```bash
gooey
```

That's it — your browser opens automatically.

> See the [Homebrew Guide](docs/homebrew.md) for full details on updating, uninstalling, and troubleshooting.

### pip install (any platform)

```bash
git clone https://github.com/halfsohappy/TheaterGWD.git
cd TheaterGWD/gooey
pip install .
gooey
```

### One-command installer

```bash
git clone https://github.com/halfsohappy/TheaterGWD.git
cd TheaterGWD/gooey
bash install.sh
```

### Manual

```bash
cd gooey
pip install -r requirements.txt
python run.py
```

The control center opens at **http://127.0.0.1:5000**.

> 📖 **[Full Installation Guide](docs/installation.md)** — all methods, all platforms, all details.

---

## What's Different from annieOSC

| Issue in annieOSC | Fixed in Control Center |
|---|---|
| Config strings split by spaces — commands like `value:accelX, ip:192.168.1.50` broken into multiple args | Config payloads sent as single string argument |
| Status/reply feed buried at bottom of TheaterGWD tab | Always-visible live feed panel on the right |
| Many commands missing (delete, clone, rename, move, solo, setAll, etc.) | Complete command coverage for all TheaterGWD operations |
| Generic OSC tool with TheaterGWD as an afterthought | Purpose-built for TheaterGWD with organized sections |
| Status config button sends empty payload (does nothing) | Proper status config form with IP, port, address fields |
| No structured input for clone/rename/move payloads | Dedicated forms with source/destination fields |
| Tab-based layout hides the feed when sending commands | Split-panel: controls left, live feed always visible |

---

## Layout

The interface uses a **split-panel layout**:

- **Left panel** — Organized command sections (Dashboard, Messages, Patches, Direct, Advanced, Reference)
- **Right panel** — Always-visible live activity feed showing all sent/received messages
- **Top bar** — Device connection settings and reply listener toggle

This means you can send commands and immediately see device replies without switching tabs.

---

## Sections

### Dashboard
Quick-access buttons for common operations:
- **Blackout / Restore** — Emergency stop and resume
- **Save / Load** — Persist to or restore from NVS
- **NVS Clear** — Factory reset OSC config
- **List** — Query messages, patches, or everything (with verbose option)
- **Status Config** — Tell the device where to send status messages

### Messages
Complete message management:
- **Create/Update** — Config builder with sensor value, target IP/port/address, bounds, patch assignment
- **Actions** — Info, Enable, Disable, Delete, Save for any named message
- **Clone/Rename** — Duplicate or rename messages

### Patches
Full patch control:
- **Start/Stop/Delete** — Lifecycle management
- **Info/Save** — Query details or persist individually
- **Period/AdrMode/Override** — Configure patch behavior
- **Add/Remove/Solo/Move** — Manage messages within patches
- **setAll** — Apply config to all messages at once
- **Clone/Rename** — Duplicate or rename patches

### Direct
One-step setup with config builder — creates message + patch, links them, starts streaming.

### Advanced
- **Raw OSC Send** — Send arbitrary OSC messages (single or repeated)
- **JSON Batch** — Send multiple messages from JSON
- **Bridge** — Forward messages between ports/hosts

### Reference
Searchable documentation for all:
- **Commands** — Every OSC address pattern with description and expected payload
- **Keywords** — Plain-language definitions for all TheaterGWD concepts
- **Config Keys** — All key:value pairs accepted in config strings
- **Address Modes** — How patches compose OSC addresses

---

## Command-Line Options

```
gooey [OPTIONS]

Options:
  --port PORT       Web server port (default: 5000)
  --host HOST       Web server host (default: 127.0.0.1)
  --no-browser      Don't auto-open browser on startup
  --debug           Enable debug mode
```

Examples:

```bash
gooey                           # Default — opens browser at localhost:5000
gooey --port 8080               # Use a different port
gooey --host 0.0.0.0            # Allow access from other devices on the network
gooey --no-browser              # Don't auto-open browser
```

---

## Documentation

| Guide | Description |
|-------|-------------|
| **[Installation](docs/installation.md)** | All install methods for every platform |
| **[Homebrew Guide](docs/homebrew.md)** | Homebrew-specific setup, updating, and troubleshooting |
| **[Quick Start](docs/quickstart.md)** | First-time walkthrough of the UI |
| **[Troubleshooting](docs/troubleshooting.md)** | Common issues and solutions |
| **[User Guide](../docs/user_guide.md)** | Full TheaterGWD user guide |
| **[Technical Guide](../docs/technical_guide.md)** | Architecture and protocol details |

---

## Tech Stack

| Component | Technology |
|-----------|-----------|
| Backend | Python 3 + Flask |
| Real-time | Flask-SocketIO (WebSocket) |
| OSC | python-osc |
| Frontend | Vanilla HTML / CSS / JS (no build step) |

---

## License

[MIT License](../LICENSE) — Same as TheaterGWD.
