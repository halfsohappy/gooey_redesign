# Troubleshooting

Solutions for common issues when installing or running the annieData Control Center.

---

## Table of Contents

- [Installation Issues](#installation-issues)
- [Startup Issues](#startup-issues)
- [Connection Issues](#connection-issues)
- [UI Issues](#ui-issues)
- [OSC Issues](#osc-issues)
- [Platform-Specific](#platform-specific)
- [Getting Help](#getting-help)

---

## Installation Issues

### "command not found: gooey" after Homebrew install

Homebrew's `bin` directory is not in the `PATH`.

**Apple Silicon Macs (M1/M2/M3/M4):**
```bash
echo 'export PATH="/opt/homebrew/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

**Intel Macs:**
```bash
echo 'export PATH="/usr/local/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

### "Python 3.8+ is required but was not found"

Install Python:

| Platform | Command |
|----------|---------|
| macOS | `brew install python` |
| Ubuntu/Debian | `sudo apt install python3 python3-pip python3-venv` |
| Fedora | `sudo dnf install python3 python3-pip` |
| Windows | Download from [python.org](https://www.python.org/downloads/) |

### "Failed to create venv" on Ubuntu/Debian

The `venv` module is not installed by default on some Linux distributions:

```bash
sudo apt install python3-venv
```

Then retry installation.

### pip install fails with "externally-managed-environment"

On newer systems (Python 3.11+), pip may refuse system-wide installation. Use a virtual environment:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install .
```

Or use Homebrew on macOS, which manages its own virtual environment.

### pip install fails with permission errors

Avoid `sudo pip install`. Use a virtual environment (see above) or install with `--user`:

```bash
pip install --user .
```

---

## Startup Issues

### Port 5000 is already in use

**macOS Ventura+ (most common cause):** AirPlay Receiver uses port 5000.

**Fix 1:** Use a different port:
```bash
gooey --port 5001
```

**Fix 2:** Disable AirPlay Receiver:
1. **System Settings** → **General** → **AirDrop & Handoff**
2. Toggle off **AirPlay Receiver**

**Other platforms:** Another application is using the port. Find it:
```bash
# macOS/Linux
lsof -i :5000

# Windows
netstat -ano | findstr :5000
```

### "Address already in use" error

A previous annieData process may still be running. Identify and stop it:

```bash
# macOS/Linux
lsof -i :5000 | grep python
# Note the PID, then:
kill <PID>
```

Or simply use a different port: `gooey --port 5001`

### Browser doesn't open automatically

Some terminal environments (SSH, tmux, certain IDE terminals) cannot open browsers. Navigate manually:

```
http://127.0.0.1:5000
```

Or use `--no-browser` to suppress the attempt:
```bash
gooey --no-browser
```

### "ModuleNotFoundError: No module named 'flask'"

Dependencies are not installed. The fix depends on the installation method:

**Homebrew:** `brew reinstall gooey`

**pip:** `pip install .` (from the `gooey/` directory)

**Manual:** `pip install -r requirements.txt`

---

## Connection Issues

### Device not responding to commands

1. **Check the IP address** — ensure the device IP in the top bar matches the actual device IP
2. **Check the port** — the device's OSC port must match the entered value
3. **Check network connectivity:**
   ```bash
   ping <device-ip>
   ```
4. **Check firewall** — ensure the firewall allows UDP traffic on the device port
5. **Start the listener** — click "Start Listener" in the top bar to enable reply reception

### No replies appearing in the live feed

1. **Is the listener running?** Confirm "Start Listener" is active
2. **Is the reply port correct?** The device must know where to send replies — use **Dashboard** → **Status Config** to set the reply IP/port
3. **Firewall:** Ensure incoming UDP is allowed on the listener port

### Commands work but sensor data isn't flowing

1. **Create a message** with the correct sensor value, target IP, and port
2. **Create a scene** and add the message to it
3. **Start the scene** — data only flows when the scene is running
4. **Check the target application** is listening on the correct port

---

## UI Issues

### Page is blank or styles are missing

Perform a hard refresh:
- **macOS:** `Cmd + Shift + R`
- **Windows/Linux:** `Ctrl + Shift + R`

### Cards or panels are in the wrong order

Card order is saved in your browser's local storage. To reset:
1. Open browser developer tools (`F12`)
2. Go to **Application** → **Local Storage**
3. Delete the `gooey_card_order` entry
4. Refresh the page

### Live feed not updating

1. Check the browser console (`F12` → **Console**) for WebSocket errors
2. Refresh the page — this reconnects the WebSocket
3. Ensure the server is still running in your terminal

---

## OSC Issues

### Config strings are being split

Config payloads like `value:accelX, ip:192.168.1.50` must be sent as a **single string argument**. If using the Raw OSC page, check the **"Send as single string"** checkbox.

### Message bounds not working

Bounds are specified in the config string as `low:0.0, high:1.0`. Make sure:
- Both `low` and `high` are specified as floats
- The values are comma-separated within the config string

### OSC address not what you expected

Check the **address mode** on your scene:
- `fallback` — uses the message's address, falls back to scene address
- `override` — always uses the scene address
- `prepend` — scene address + message address
- `append` — message address + scene address

---

## Platform-Specific

### macOS: "App can't be opened because it is from an unidentified developer"

This should not occur with the Homebrew or pip install. If it does:
1. **System Settings** → **Privacy & Security**
2. Click **Open Anyway** next to the blocked app

### Linux: No browser opens

On headless Linux systems or minimal desktop environments, the default browser may not be configured:

```bash
gooey --no-browser
# Then open http://127.0.0.1:5000 in any browser
```

### Windows: "python" is not recognized

Add Python to the PATH during installation. If already installed, locate it:

```powershell
where python
# or
py --version
```

Use `py` instead of `python` if that works:
```powershell
py -m pip install .
```

---

## Getting Help

For additional assistance:

1. **Check the docs:**
   - [Installation Guide](installation.md)
   - [Homebrew Guide](homebrew.md)
   - [Quick Start](quickstart.md)

2. **Open an issue:** [github.com/halfsohappy/TheaterGWD/issues](https://github.com/halfsohappy/TheaterGWD/issues)

   Include:
   - What you tried
   - What happened (copy the full error message)
   - Your OS and Python version (`python3 --version`)
   - How you installed annieData (Homebrew / pip / script / manual)
