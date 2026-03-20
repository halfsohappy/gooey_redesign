# Installation Guide

Everything you need to get Gooey – the TheaterGWD Control Center – running on your machine.

Pick the method that suits you best, then jump to [First Launch](#first-launch) at the bottom.

---

## Table of Contents

- [Homebrew (macOS – recommended)](#homebrew-macos--recommended)
- [pip install (any platform)](#pip-install-any-platform)
- [One-command installer script](#one-command-installer-script)
- [Manual install](#manual-install)
- [First Launch](#first-launch)
- [Updating](#updating)
- [Uninstalling](#uninstalling)

---

## Homebrew (macOS – recommended)

The fastest way on macOS. One command to install, one command to run.

### Prerequisites

- macOS 12 Monterey or later
- [Homebrew](https://brew.sh) installed — if not, run:

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

### Install

```bash
brew tap halfsohappy/theatergwd https://github.com/halfsohappy/TheaterGWD
brew install gooey
```

Or in one line:

```bash
brew tap halfsohappy/theatergwd https://github.com/halfsohappy/TheaterGWD && brew install gooey
```

### Run

```bash
gooey
```

That's it — your browser opens automatically.

> See [Homebrew Guide](homebrew.md) for update/uninstall/troubleshooting details.

---

## pip install (any platform)

Works on macOS, Linux, and Windows wherever Python 3.8+ is available.

### Prerequisites

- Python 3.8 or later
- pip (usually included with Python)

### Install

```bash
# Clone the repository
git clone https://github.com/halfsohappy/TheaterGWD.git
cd TheaterGWD/gooey

# Install (creates a `gooey` command)
pip install .
```

> **Tip:** Use a virtual environment to keep things tidy:
>
> ```bash
> python3 -m venv .venv
> source .venv/bin/activate   # Windows: .venv\Scripts\activate
> pip install .
> ```

### Run

```bash
gooey
```

---

## One-command installer script

A self-contained script that creates a virtual environment, installs dependencies, and launches the app in one step. Great for quick demos or evaluation.

### Prerequisites

- Python 3.8+
- Git (to clone the repo)

### Install & Run

```bash
git clone https://github.com/halfsohappy/TheaterGWD.git
cd TheaterGWD/gooey
bash install.sh
```

The installer will:
1. Find Python 3.8+ on your system
2. Create a virtual environment in `gooey/venv/`
3. Install dependencies from `requirements.txt`
4. Start the control center

### Run again later

```bash
cd TheaterGWD/gooey
bash install.sh
```

The script skips setup if the virtual environment already exists.

---

## Manual install

If you prefer full control over the process.

### Prerequisites

- Python 3.8+ with pip
- Git

### Steps

```bash
# 1. Clone the repository
git clone https://github.com/halfsohappy/TheaterGWD.git
cd TheaterGWD/gooey

# 2. Create a virtual environment (optional but recommended)
python3 -m venv venv
source venv/bin/activate        # macOS/Linux
# venv\Scripts\activate         # Windows

# 3. Install dependencies
pip install -r requirements.txt

# 4. Run
python run.py
```

---

## First Launch

No matter which method you used, the control center behaves the same:

1. **Browser opens automatically** at [http://127.0.0.1:5000](http://127.0.0.1:5000)
2. Enter your **device IP and port** in the top bar
3. Click **Start Listener** to receive device replies
4. Use the **Dashboard** to send your first commands

> If the browser doesn't open, navigate to `http://127.0.0.1:5000` manually.

### Command-line options

All methods support these flags:

| Flag | Description |
|------|-------------|
| `--port PORT` | Web server port (default: 5000) |
| `--host HOST` | Web server host (default: 127.0.0.1) |
| `--no-browser` | Don't auto-open browser on startup |
| `--debug` | Enable debug mode |

Examples:

```bash
gooey --port 8080              # Different port
gooey --host 0.0.0.0           # Allow access from other devices on the network
gooey --no-browser             # Don't open browser
python run.py --port 8080      # Same options work with run.py
```

---

## Updating

### Homebrew

```bash
brew update
brew upgrade gooey
```

### pip

```bash
cd TheaterGWD
git pull
cd gooey
pip install --upgrade .
```

### Installer script / manual

```bash
cd TheaterGWD
git pull
cd gooey
pip install -r requirements.txt   # In case deps changed
```

---

## Uninstalling

### Homebrew

```bash
brew uninstall gooey
brew untap halfsohappy/theatergwd     # Optional: remove the tap
```

### pip

```bash
pip uninstall gooey-theatergwd
```

### Installer script / manual

Delete the cloned repository folder. If you created a virtual environment, it's inside `gooey/venv/` and will be removed with the folder.

---

## Next Steps

- [Quick Start Guide](quickstart.md) — first-time walkthrough of the UI
- [Homebrew Guide](homebrew.md) — detailed Homebrew reference
- [Troubleshooting](troubleshooting.md) — common issues and fixes
