# Gooey Tutorial

Welcome to **Gooey**, the web-based control center for TheaterGWD sensor
devices. This tutorial walks you through the interface and shows you how to
use every major feature.

---

## What Is Gooey?

Gooey is a browser application that lets you configure, monitor, and control
TheaterGWD devices over a local network. It communicates using **OSC**
(Open Sound Control) — a lightweight protocol widely used in theater, music,
and interactive installations.

With Gooey you can:

- Create and edit **messages** that map sensor values to OSC output
- Organise messages into **scenes** with shared timing and addressing
- Stream live sensor data to any OSC-capable application
- Monitor all traffic in a real-time **feed**
- Save and load complete device configurations as **shows**

---

## Layout Overview

The Gooey window is divided into three areas:

### Header Bar

The header sits at the top and contains:

| Element | Purpose |
|---------|---------|
| **Logo** | Click to return to the main view |
| **Connection controls** | Device IP, port, and the *Start Listener* button |
| **Toolbar buttons** | Quick actions — *List*, *Status*, *Blackout*, *Restore*, *Save*, *Load* |
| **Panel toggles** | Open or close the right-side panels — *Feed*, *Serial*, *Notifs*, *Ref* |
| **Device selector** | Drop-down to switch between registered devices |
| **Query bar** | Filter the current tab's content by keyword |

### Main Content (Left)

The main area holds six tabs. Click a tab name in the navigation bar to
switch between them. Each tab is described in detail below.

### Right Panels

The right side hosts optional overlay panels that can be toggled
independently:

| Panel | Toggle button | What it shows |
|-------|---------------|---------------|
| **Feed** | *Feed* | Live stream of sent and received OSC messages |
| **Serial** | *Serial* | Serial-port monitor for USB-connected devices |
| **Notifications** | *Notifs* | History of toast notifications |
| **Reference** | *Ref* | Searchable command and keyword reference (you are here!) |

You can open several panels at once — they split the available space equally.

---

## Main Tabs

### Messages

The Messages tab is where you define *what* the device sends.

**Key fields when creating a message:**

| Field | Description |
|-------|-------------|
| **Name** | A unique label for the message (e.g. `myAccel`) |
| **Sensor** | Which hardware sensor to read (`accelX`, `gyroZ`, `baro`, etc.) |
| **Target IP** | The destination IP address for OSC data |
| **Port** | The destination UDP port (e.g. `9000`) |
| **Address** | The OSC address pattern (e.g. `/sensor/accel/x`) |
| **Low / High** | Output range — sensor values are scaled from `[0, 1]` to `[low, high]` |

**How to create a message:**

1. Fill in the form fields
2. Optionally assign the message to a **Scene** using the scene field
3. Click **Apply to Device**
4. The message appears in the table below

**Editing and deleting:** Click a row in the message table to load it into the
form. Change any fields and click **Apply to Device** to update, or use the
delete button on the row.

---

### Scenes

Scenes group messages together and control their timing and addressing.

**Key fields when creating a scene:**

| Field | Description |
|-------|-------------|
| **Name** | A unique scene label (e.g. `mainScene`) |
| **Period** | How often (in ms) the scene sends its messages |
| **Address** | An optional shared OSC address for all messages in the scene |
| **Address Mode** | How the scene address combines with message addresses — `fallback`, `override`, `prepend`, or `append` |

**How to use scenes:**

1. Create a scene by entering a name and clicking **Apply**
2. Add messages to the scene — either from the message editor or the scene
   detail view
3. Click **Start** to begin streaming all messages in the scene
4. Click **Stop** to halt streaming
5. Use **Solo** to mute every other scene and only stream the selected one

> **Tip:** Scene-level `low` / `high` overrides let you rescale messages
> without editing each one individually.

---

### Ori (Orientation)

The Ori tab is available when the connected device has an orientation sensor
(IMU). It provides:

- **Euler angles** (X, Y, Z) and **quaternion** readouts (I, J, K, R)
- **Ori conditions** on messages — `ori_only` restricts a message to send
  only when a specific orientation condition is met; `ori_not` excludes it

This tab appears only when Ori mode is enabled on the device.

---

### Shows

Shows let you snapshot and restore the entire device configuration.

- **Save** — stores all current messages, scenes, and settings under a name
- **Load** — pushes a saved show back to the device
- **Delete** — removes a saved show

Shows are stored on the Gooey server. Use them to switch between different
performance setups quickly.

---

### Direct

Direct is the fastest way to get sensor data flowing:

1. Pick a **sensor value**
2. Enter a **target IP**, **port**, and **OSC address**
3. Click **Send**

Gooey automatically creates a message, wraps it in a scene, and starts
streaming — all in one step. This is ideal for quick tests and demos.

---

### Advanced

The Advanced tab contains power-user tools:

| Tool | Description |
|------|-------------|
| **Raw OSC Send** | Send a hand-crafted OSC message to any address |
| **JSON Batch** | Upload or paste a JSON object to create multiple messages and scenes at once |
| **Bridge** | Forward OSC traffic from one port to another — useful for routing between applications |

---

## Right-Side Panels

### Feed

The Feed panel shows a live, scrolling log of every OSC message that passes
through Gooey:

- **Purple** entries → messages *sent* to the device
- **Green** entries → messages *received* from the device

Use the **filter** input to narrow the feed by keyword.
Use the **Pause** button to freeze the view while you inspect entries.

### Serial Monitor

Connect a device via USB and open the Serial panel to see its debug output.
Select a port from the dropdown, set the baud rate, and click **Connect**.

### Notifications

Every toast notification that appears in the UI is also logged here. Open
this panel to review past warnings and confirmations.

### Reference

The Reference panel (where this tutorial lives) provides a searchable
catalogue of every OSC command, sensor keyword, config key, and address
mode supported by TheaterGWD. Type in the **Search** box to filter across
all sections.

---

## Typical Workflow

Here is a common end-to-end workflow for a new user:

### 1. Launch Gooey

```
gooey
```

Your browser opens to `http://127.0.0.1:5000`.

### 2. Connect to a Device

Enter the device's **IP** and **port** in the header connection controls and
click **Start Listener** to begin receiving replies.

### 3. Discover What's on the Device

Click the **List** button in the header toolbar. The device replies with all
of its configured messages, scenes, and settings. Watch the **Feed** panel
for the response.

### 4. Create a Message

Switch to the **Messages** tab:

1. Name: `demoAccel`
2. Sensor: `accelX`
3. Target IP: your computer's IP (e.g. `192.168.1.10`)
4. Port: `9000`
5. Address: `/demo/accel`
6. Click **Apply to Device**

### 5. Create a Scene and Start Streaming

Switch to the **Scenes** tab:

1. Name: `demoScene`
2. Click **Apply**
3. Add `demoAccel` to the scene
4. Click **Start**

Sensor data is now streaming to `192.168.1.10:9000` on address `/demo/accel`.

### 6. Save Your Work

Click **Save** in the header toolbar to persist the configuration to the
device's non-volatile storage — it will survive a power cycle.

Or switch to the **Shows** tab to save a named snapshot on the Gooey server.

---

## Quick-Reference: Keyboard & Mouse

| Action | How |
|--------|-----|
| Switch tabs | Click a tab name in the navigation bar |
| Toggle a right panel | Click *Feed* / *Serial* / *Notifs* / *Ref* in the header |
| Filter the feed | Type in the feed's filter input |
| Collapse a table card | Click the card title |
| Search the reference | Type in the *Search* box at the top of the Ref panel |

---

## Getting Help

- Open the **Reference** panel (`Ref` button) for command and keyword look-up
- Visit the [User Guide](/docs/user-guide) for full documentation
- Check the [Troubleshooting Guide](https://github.com/halfsohappy/TheaterGWD/blob/main/gooey/docs/troubleshooting.md)
  if something isn't working
- Report issues at
  [github.com/halfsohappy/TheaterGWD](https://github.com/halfsohappy/TheaterGWD/issues)
