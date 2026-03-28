# Quick Start Guide

A step-by-step walkthrough to go from zero to sending OSC commands to your TheaterGWD device.

---

## 1. Install Gooey

Pick your preferred method (see [Installation Guide](installation.md) for details):

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

Your browser opens to [http://127.0.0.1:5000](http://127.0.0.1:5000).

> If port 5000 is busy (common on macOS), use `gooey --port 5001`.

---

## 3. Connect to Your Device

At the **top of the page**, you'll see the connection bar:

1. **Device Host** — Enter your device's IP address (e.g. `192.168.1.50`)
2. **Device Port** — Enter the device's OSC port (default: `8000`)
3. Click **Start Listener** to begin receiving replies from the device

> **Tip:** If you don't know your device's IP, connect it to your network first and check your router's DHCP client list, or use the device's serial console.

---

## 4. Try Your First Commands

### Dashboard tab

The Dashboard gives you quick-access buttons:

- **List All** — asks the device to report all configured messages, scenes, and settings. Watch the **live feed** on the right for the reply.
- **Status Info** — queries device status (firmware, uptime, etc.)
- **Blackout** — emergency stop: immediately pauses all OSC output from the device
- **Restore** — resumes output after a blackout
- **Save** — persists the current configuration to the device's NVS (non-volatile storage)

Try clicking **List All** now. You should see the device's response appear in the live feed panel on the right side of the screen.

### Messages tab

Messages define *what* the device sends — which sensor value, to which IP/port/address.

1. Enter a **Message Name** (e.g. `myAccel`)
2. Pick a **Sensor Value** from the dropdown (e.g. `accelX`)
3. Enter the **Target IP** where OSC data should go (your computer's IP)
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

The fastest path — creates a message, a scene, links them, and starts streaming in one step:

1. Pick a sensor value
2. Enter target IP, port, and OSC address
3. Click **Send** — data starts flowing immediately

---

## 5. Monitor the Live Feed

The **right panel** shows all OSC traffic in real time:

- 🟣 **Sent** messages (commands you sent to the device)
- 🟢 **Received** messages (replies from the device)

The feed updates automatically via WebSocket — no need to refresh.

---

## 6. Next Steps

Now that you're up and running:

| Want to... | Go to... |
|------------|----------|
| Learn all available commands | **Reference** tab in the UI |
| Send raw OSC messages | **Advanced** → **Raw OSC Send** |
| Send multiple commands at once | **Advanced** → **JSON Batch** |
| Forward OSC between ports | **Advanced** → **Bridge** |
| Understand the full architecture | [Technical Guide](../../docs/technical_guide.md) |
| Fix something that's not working | [Troubleshooting](troubleshooting.md) |

---

## Common Patterns

### Theater rehearsal setup

```bash
# Start with network access so the stage manager's tablet can connect
gooey --host 0.0.0.0 --port 8080
```

Then on the tablet, open `http://<your-computer-ip>:8080`.

### Multi-device setup

1. Add each device's IP and port in the **connection bar**
2. Use the **device registry** to track all devices
3. Send commands to any device by selecting it from the dropdown

### Quick demo

```bash
gooey --port 5001
```

1. Go to **Direct** tab
2. Pick `accelX`, enter target `127.0.0.1:9000 /demo/accel`
3. Click Send — done! Open any OSC receiver on port 9000 to see data flowing.
