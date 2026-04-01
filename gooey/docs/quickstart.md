# Quick Start Guide

A step-by-step walkthrough — from installation to sending live OSC commands to a TheaterGWD sensor device.

---

## 1. Install annieData

Select the preferred method (see [Installation Guide](installation.md) for details):

**macOS (Homebrew):**
```bash
brew install halfsohappy/theatergwd/gooey
```

**Any platform (pip):**
```bash
git clone https://github.com/halfsohappy/TheaterGWD.git
cd TheaterGWD/gooey
pip install .
```

**Any platform (installer script):**
```bash
git clone https://github.com/halfsohappy/TheaterGWD.git
cd TheaterGWD/gooey
bash install.sh
```

---

## 2. Launch the Control Center

```bash
gooey
```

The default browser opens to [http://127.0.0.1:5000](http://127.0.0.1:5000).

> If port 5000 is occupied (common on macOS), use `gooey --port 5001`.

---

## 3. Connect to Your Device

The **connection bar** appears at the top of the page:

1. **Device Host** — enter the device's IP address (e.g. `192.168.1.50`)
2. **Device Port** — enter the device's OSC port (default: `8000`)
3. Click **Start Listener** to enable reply reception from the device

> **Tip:** If the device IP is unknown, check the router's DHCP client list or connect via the device's serial console.

---

## 4. Try Your First Commands

### Dashboard tab

The Dashboard provides quick-access buttons:

- **List All** — queries the device for all configured messages, scenes, and settings. The reply appears in the **live feed** on the right.
- **Status Info** — queries device status (firmware, uptime, etc.)
- **Blackout** — emergency stop: immediately halts all OSC output from the device
- **Restore** — resumes output after a blackout
- **Save** — persists the current configuration to the device's NVS (non-volatile storage)

Click **List All** to verify connectivity. The device's response should appear in the live feed panel.

### Messages tab

Messages define *what* the device sends — which sensor value, to which IP/port/address.

1. Enter a **Message Name** (e.g. `myAccel`)
2. Select a **Sensor Value** from the dropdown (e.g. `accelX`)
3. Enter the **Target IP** where OSC data should be sent (the host machine's IP)
4. Enter the **Target Port** (e.g. `9000`)
5. Enter an **OSC Address** (e.g. `/sensor/accel/x`)
6. Click **Create**

### Scenes tab

Scenes group messages together and control their timing.

1. Enter a **Scene Name** (e.g. `myScene`)
2. Click **Create** to make an empty scene
3. Use **Add Message** to add your message to the scene
4. Click **Start** to begin streaming

### Direct tab

The fastest path — creates a message, a scene, links them, and starts streaming in a single step:

1. Select a sensor value
2. Enter target IP, port, and OSC address
3. Click **Send** — data begins flowing immediately

---

## 5. Monitor the Live Feed

The **right panel** displays all OSC traffic in real time:

- **Purple** — sent messages (commands transmitted to the device)
- **Green** — received messages (replies from the device)

The feed updates automatically via WebSocket — no manual refresh required.

---

## 6. Next Steps

With the basic setup complete:

| Want to... | Go to... |
|------------|----------|
| Learn all available commands | **Reference** tab in the UI |
| Send raw OSC messages | **Advanced** → **Raw OSC Send** |
| Send multiple commands at once | **Advanced** → **JSON Batch** |
| Forward OSC between ports | **Advanced** → **Bridge** |
| Understand the full architecture | [Engineering Guide](../../docs/engineering.md) |
| Diagnose an issue | [Troubleshooting](troubleshooting.md) |

---

## Common Patterns

### Theater rehearsal setup

```bash
# Enable network access so the stage manager's tablet can connect
gooey --host 0.0.0.0 --port 8080
```

Then on the tablet, open `http://<host-ip>:8080`.

### Multi-device setup

1. Add each device's IP and port via the **connection bar**
2. Use the **device registry** to track all connected devices
3. Select a device from the dropdown to direct commands to it

### Quick demo

```bash
gooey --port 5001
```

1. Open the **Direct** tab
2. Select `accelX`, enter target `127.0.0.1:9000 /demo/accel`
3. Click Send — open any OSC receiver on port 9000 to verify data is flowing.
