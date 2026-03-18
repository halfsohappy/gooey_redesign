# TheaterGWD User Guide

## A Practical Guide for Theater Technicians

This guide explains how to configure and operate a TheaterGWD device.  The
device is a small wireless sensor module that reads motion and environmental
data and sends it to your equipment — lighting consoles, sound boards, video
servers, or any software that accepts OSC (Open Sound Control) messages.

You control the device entirely through OSC commands sent from a computer or
control surface on the same network.

---

## Table of Contents

1. [What This Device Does](#1-what-this-device-does)
2. [Initial Setup (Provisioning)](#2-initial-setup-provisioning)
3. [Key Concepts](#3-key-concepts)
4. [Quick Start Example](#4-quick-start-example)
5. [Available Sensor Values](#5-available-sensor-values)
6. [Complete Command Reference](#6-complete-command-reference)
7. [Working with Messages](#7-working-with-messages)
8. [Working with Patches](#8-working-with-patches)
9. [Overrides and Scaling](#9-overrides-and-scaling)
10. [Address Composition](#10-address-composition)
11. [Status Monitoring](#11-status-monitoring)
12. [Practical Workflows](#12-practical-workflows)
13. [Troubleshooting](#13-troubleshooting)

---

## 1. What This Device Does

The TheaterGWD device is a wireless sensor module.  It measures:

- **Acceleration** (3 axes + magnitude) — how fast the device is speeding up
  or slowing down.
- **Rotation rate** (3 axes + magnitude) — how quickly the device is spinning.
- **Barometric pressure** — changes in altitude or air pressure.
- **Orientation** (3 Euler angles) — which way the device is pointing.

All of these values are normalised to a range of **0 to 1**, where 0 is the
minimum and 1 is the maximum.  The device continuously sends these values over
your network as OSC messages, at rates and destinations that you define.

The typical use case: an actor wears the device (or it is mounted on a set
piece), and as they move, the sensor data drives lighting cues, sound effects,
video playback, or any other parameter in your show control system.

---

## 2. Initial Setup (Provisioning)

When the device is powered on for the first time (or after a factory reset),
it creates its own WiFi network:

1. **Connect** to the WiFi network named **"annieData Setup"** from your
   laptop or phone.
2. A configuration page opens automatically (captive portal).
3. **Enter the following:**
   - **WiFi network name** (SSID) — the production network you want the device
     to join.
   - **WiFi password**.
   - **Static IP address** — assign a fixed IP so you always know where the
     device is on the network.  Or enter `dhcp` to let the router assign one.
   - **Port** — the UDP port the device will listen on for incoming OSC
     commands (e.g., `8000`).
   - **Device name** — a short identifier used in OSC addresses (e.g.,
     `bart`).  This becomes part of every command address.
4. Press **Submit**.  The device saves these settings, reboots, and connects to
   your production network.

After provisioning, every OSC command addressed to this device must begin with:

```
/annieData/{deviceName}/...
```

For example, if you named the device `bart`:

```
/annieData/bart/...
```

---

## 3. Key Concepts

### Messages

A **message** is a single data stream being sent to a destination.  It
defines:

- **Which sensor value** to send (e.g., acceleration X-axis).
- **Where to send it** — an IP address, port number, and OSC address.
- **How to scale it** — optional output range (default is 0 to 1).

Example: "Send the X-axis acceleration to the lighting console at
192.168.1.50 port 7000 on address /fixture/1/dimmer, scaled from 0 to 255."

### Patches

A **patch** is a group of messages that are sent together.  Think of it like a
page on a lighting console — a collection of related outputs that you can
start, stop, or modify as a unit.

Each patch runs on its own timer.  You set how often it sends (e.g., every
50 ms = 20 times per second).

A patch can also hold its own IP address, port, OSC address, and output scale.
These can serve as defaults for its messages, or they can **override** every
message in the patch — useful when you want all sensor data going to the same
destination.

### Overrides

When a patch has "override" enabled for a particular field, every message in
that patch uses the patch's value instead of its own.  This is useful when
you have many messages all going to the same place — set the destination once
on the patch and override.

### Scaling

All sensor values come out of the device as numbers between 0 and 1.  But your
equipment might expect a different range:

- DMX fixtures expect 0–255.
- Audio faders might use 0–1 (no scaling needed).
- A pan/tilt fixture might need −180 to 180.

You set **low** and **high** bounds on either the message or the patch, and the
device maps the 0–1 sensor value to that range.

---

## 4. Quick Start Example

This example assumes the device is named `bart`, listening on port `8000`, and
your computer is on the same network.

Using any OSC sender (such as Protokol, TouchOSC, oscsend, or a QLab script):

### Fastest method — one command with `direct`

```
Address:  /annieData/bart/direct/mySetup
Payload:  "value:accelX, ip:192.168.1.50, port:9000, adr:/sensor/accel/x, period:50"
```

That single command creates a message and patch, links them, and starts
sending the X-axis acceleration to 192.168.1.50:9000 at 20 Hz.  Open an OSC
monitor on your target machine and you will see values arriving immediately.

To stop:
```
Address:  /annieData/bart/patch/mySetup/stop
```

### Step-by-step method

If you need more control over the message and patch separately:

**Step 1: Create a message**
```
Address:  /annieData/bart/msg/accelX
Payload:  "value:accelX, ip:192.168.1.50, port:9000, adr:/sensor/accel/x"
```

This creates a message named "accelX" that reads the X-axis accelerometer,
and will send it to 192.168.1.50:9000 at OSC address /sensor/accel/x.

**Step 2: Create a patch and add the message**
```
Address:  /annieData/bart/patch/sensors
Payload:  "ip:192.168.1.50, port:9000"
```
```
Address:  /annieData/bart/patch/sensors/addMsg
Payload:  "accelX"
```

**Step 3: Set the send rate**
```
Address:  /annieData/bart/patch/sensors/period
Payload:  "50"
```

**Step 4: Start sending**
```
Address:  /annieData/bart/patch/sensors/start
```

The device is now sending the X-axis acceleration value to your target 20
times per second.

**Step 5: Stop sending**
```
Address:  /annieData/bart/patch/sensors/stop
```

---

## 5. Available Sensor Values

Use these names when configuring the `value` field of a message:

| Name | Description |
|------|-------------|
| `accelX` | Acceleration, X axis |
| `accelY` | Acceleration, Y axis |
| `accelZ` | Acceleration, Z axis |
| `accelLength` (or `accelLen`, `aLen`) | Acceleration magnitude |
| `gyroX` | Rotation rate, X axis |
| `gyroY` | Rotation rate, Y axis |
| `gyroZ` | Rotation rate, Z axis |
| `gyroLength` (or `gyroLen`, `gLen`) | Rotation rate magnitude |
| `baro` | Barometric pressure |
| `eulerX` | Orientation, roll |
| `eulerY` | Orientation, pitch |
| `eulerZ` | Orientation, yaw |

All names are case-insensitive.

---

## 6. Complete Command Reference

All commands are sent as OSC messages to the device.  The address always starts
with `/annieData/{deviceName}`.  The abbreviation `{dev}` is used below to
mean your device name.

**Case flexibility:** All command segments accept camelCase, snake_case, and
plain lowercase.  For example, `addMsg`, `add_msg`, and `addmsg` are all
equivalent.  User-defined names (message and patch names) preserve their
original case.

**Payload format:** All payloads are a single string or a single number.  When
a command needs two values (like clone or rename), they are sent as a single
comma-separated string: `"name1, name2"`.

### Message Commands

| Address | Payload | What it does |
|---------|---------|--------------|
| `/annieData/{dev}/msg/{name}` | config string | Create or update a message. |
| `/annieData/{dev}/msg/{name}/delete` | *(none)* | Delete the message. |
| `/annieData/{dev}/msg/{name}/enable` | *(none)* | Enable the message. |
| `/annieData/{dev}/msg/{name}/disable` | *(none)* | Disable (mute) the message. |
| `/annieData/{dev}/msg/{name}/info` | *(none)* | Get the message's parameters. |

### Patch Commands

| Address | Payload | What it does |
|---------|---------|--------------|
| `/annieData/{dev}/patch/{name}` | config string | Create or update a patch. |
| `/annieData/{dev}/patch/{name}/delete` | *(none)* | Delete the patch. |
| `/annieData/{dev}/patch/{name}/start` | *(none)* | Start sending. |
| `/annieData/{dev}/patch/{name}/stop` | *(none)* | Stop sending. |
| `/annieData/{dev}/patch/{name}/addMsg` | `"msg1, msg2"` | Add message(s) to the patch. |
| `/annieData/{dev}/patch/{name}/removeMsg` | `"msgName"` | Remove a message from the patch. |
| `/annieData/{dev}/patch/{name}/period` | `"50"` | Set how often to send (in milliseconds). |
| `/annieData/{dev}/patch/{name}/override` | field list | Set which fields the patch overrides. |
| `/annieData/{dev}/patch/{name}/adrMode` | mode string | Set address composition mode. |
| `/annieData/{dev}/patch/{name}/setAll` | config string | Apply settings to all messages in the patch. |
| `/annieData/{dev}/patch/{name}/solo` | `"msgName"` | Enable one message, mute all others. |
| `/annieData/{dev}/patch/{name}/unsolo` | *(none)* | Unmute all messages. |
| `/annieData/{dev}/patch/{name}/enableAll` | *(none)* | Enable all messages in the patch. |
| `/annieData/{dev}/patch/{name}/info` | *(none)* | Get the patch's parameters. |

### Clone / Rename / Move

| Address | Payload | What it does |
|---------|---------|--------------|
| `/annieData/{dev}/clone/msg` | `"srcName, destName"` | Copy a message to a new name. |
| `/annieData/{dev}/clone/patch` | `"srcName, destName"` | Copy a patch to a new name. |
| `/annieData/{dev}/rename/msg` | `"oldName, newName"` | Rename a message. |
| `/annieData/{dev}/rename/patch` | `"oldName, newName"` | Rename a patch. |
| `/annieData/{dev}/move` | `"msgName, patchName"` | Move a message to a different patch. |

### List Commands

| Address | Payload | What it does |
|---------|---------|--------------|
| `/annieData/{dev}/list/msgs` | `"verbose"` *(optional)* | List all messages. |
| `/annieData/{dev}/list/patches` | `"verbose"` *(optional)* | List all patches. |
| `/annieData/{dev}/list/all` | `"verbose"` *(optional)* | List everything. |

When `"verbose"` is included, the reply includes all parameters for each item.

### Global Commands

| Address | What it does |
|---------|--------------|
| `/annieData/{dev}/blackout` | Stop all patches immediately. |
| `/annieData/{dev}/restore` | Restart all patches. |

### Status Commands

| Address | Payload | What it does |
|---------|---------|--------------|
| `/annieData/{dev}/status/config` | config string | Set where status messages are sent. |
| `/annieData/{dev}/status/level` | level string | Set minimum importance level. |

Level values: `"error"`, `"warn"` / `"warning"`, `"info"`, `"debug"`.

### Save / Load Commands

Patches and messages can be saved to non-volatile memory (NVS) so they survive
power cycles.

| Address | Payload | What it does |
|---------|---------|--------------|
| `/annieData/{dev}/save` | *(none)* | Save all patches and messages to NVS. |
| `/annieData/{dev}/save/msg` | `"msgName"` | Save one message to NVS. |
| `/annieData/{dev}/save/patch` | `"patchName"` | Save one patch to NVS. |
| `/annieData/{dev}/load` | *(none)* | Load all patches and messages from NVS. |
| `/annieData/{dev}/nvs/clear` | *(none)* | Erase all saved OSC data from NVS. |

### Direct Command

The `direct` command creates a message and patch, links them, and starts
sending — all in one step.  This is the fastest way to start receiving data.

| Address | Payload | What it does |
|---------|---------|--------------|
| `/annieData/{dev}/direct/{name}` | config string | Create msg + patch, add, and start sending. |

The config string uses the same format as message creation, with an optional
`period` key:

```
/annieData/bart/direct/mySetup   "value:accelX, ip:192.168.1.50, port:9000, adr:/sensor/x, period:50"
```

This single command:
1. Creates a message named `mySetup` with the sensor and destination.
2. Creates a patch named `mySetup` with the same destination.
3. Adds the message to the patch.
4. Starts sending at 50 ms intervals (20 Hz).

If a message or patch with that name already exists, it is updated with the
new values and restarted.

---

## 7. Working with Messages

### Creating a message

Send a config string as the payload of a message assign command:

```
Address:  /annieData/bart/msg/myFader
Payload:  "value:accelX, ip:192.168.1.50, port:9000, adr:/fader/1"
```

This creates a message named `myFader` that sends the X-axis accelerometer to
`192.168.1.50:9000` at OSC address `/fader/1`.

### Config string format

The config string is a comma-separated list of `key:value` pairs:

```
value:accelX, ip:192.168.1.50, port:9000, adr:/fader/1, low:0, high:255
```

Available keys:

| Key | What it sets |
|-----|--------------|
| `value` | Which sensor to read (see [Available Sensor Values](#5-available-sensor-values)). |
| `ip` | Destination IP address. |
| `port` | Destination port number. |
| `adr` (or `addr`, `address`) | OSC address path (e.g., `/fader/1`). |
| `low` (or `min`) | Output range minimum. |
| `high` (or `max`) | Output range maximum. |
| `patch` | Name of a patch to assign this message to. |

### Updating a message

Sending another assign command to the same name updates only the fields you
specify — existing values are preserved:

```
Address:  /annieData/bart/msg/myFader
Payload:  "low:0, high:127"
```

This changes the output range to 0–127 without affecting the IP, port, or
sensor assignment.

### Referencing other objects

Use `-` instead of `:` to copy a value from another registered message or
patch:

```
ip-mixer1, port-mixer1, value:accelX
```

This copies the IP and port from the object named `mixer1`.

Use `default-mixer1` to copy all set fields from `mixer1` as fallbacks.

---

## 8. Working with Patches

### Creating and populating a patch

```
/annieData/bart/patch/showPatch     "ip:192.168.1.50, port:9000, adr:/sensor"
/annieData/bart/msg/ax              "value:accelX"
/annieData/bart/msg/ay              "value:accelY"
/annieData/bart/msg/az              "value:accelZ"
/annieData/bart/patch/showPatch/addMsg   "ax, ay, az"
```

### Starting and stopping

```
/annieData/bart/patch/showPatch/start
/annieData/bart/patch/showPatch/stop
```

### Changing the send rate

The period is in milliseconds.  Lower = faster (more CPU and network usage):

| Period | Rate |
|--------|------|
| 100 ms | 10 Hz |
| 50 ms | 20 Hz (default) |
| 20 ms | 50 Hz |
| 10 ms | 100 Hz |

```
/annieData/bart/patch/showPatch/period    "20"
```

### Solo / unsolo

During rehearsal, you might want to isolate one sensor stream:

```
/annieData/bart/patch/showPatch/solo      "ax"
```

This mutes every message in the patch except `ax`.  To restore them:

```
/annieData/bart/patch/showPatch/unsolo
```

### Applying settings to all messages

```
/annieData/bart/patch/showPatch/setAll    "low:0, high:255"
```

This sets the output range of every message in the patch to 0–255.

---

## 9. Overrides and Scaling

### Overriding destination fields

If all messages in a patch go to the same IP and port, set them once on the
patch and enable the override:

```
/annieData/bart/patch/showPatch           "ip:192.168.1.50, port:9000"
/annieData/bart/patch/showPatch/override  "ip, port"
```

Now every message in the patch sends to 192.168.1.50:9000, regardless of what
IP/port each individual message has set.

To turn off an override, prefix the field with `-`:

```
/annieData/bart/patch/showPatch/override  "-ip"
```

Available override fields: `ip`, `port`, `adr`, `low`, `high`, `scale`
(shortcut for both low and high), `all`, `none`.

### Scaling with patch overrides

Set output bounds on the patch and override them:

```
/annieData/bart/patch/dmxPatch           "low:0, high:255"
/annieData/bart/patch/dmxPatch/override  "scale"
```

Now every message in this patch outputs values mapped from 0–255, even if
individual messages have different bounds set.  This is useful when all
messages in a patch target the same type of equipment (e.g., DMX fixtures).

---

## 10. Address Composition

By default, each message uses its own OSC address.  But the patch can modify
how addresses are assembled.

### Address modes

| Mode | What happens | Example |
|------|-------------|---------|
| `fallback` (default) | Message's address is used. If message has no address, the patch's address is used. | Message: `/fader1` → sends `/fader1` |
| `override` | Patch's address replaces the message's. | Patch: `/mixer`, Message: `/fader1` → sends `/mixer` |
| `prepend` | Patch's address is placed before the message's. | Patch: `/mixer`, Message: `/fader1` → sends `/mixer/fader1` |
| `append` | Message's address is placed before the patch's. | Patch: `/mixer`, Message: `/fader1` → sends `/fader1/mixer` |

### Setting the address mode

```
/annieData/bart/patch/showPatch/adrMode   "prepend"
```

### When to use each mode

- **fallback:** When each message has its own complete address.
- **override:** When every message should go to the same address (e.g., a
  single fader).
- **prepend:** When the patch represents a destination group and messages
  represent individual channels.  Patch address `/mixer`, messages
  `/ch1`, `/ch2`, etc. → `/mixer/ch1`, `/mixer/ch2`.
- **append:** Less common, but useful when messages represent a primary
  category and the patch adds a suffix.

---

## 11. Status Monitoring

The device can send status messages to a monitoring station on your network.

### Configure the status destination

```
/annieData/bart/status/config   "ip:192.168.1.10, port:9999, adr:/status"
```

### Set the importance level

```
/annieData/bart/status/level    "info"
```

| Level | What you see |
|-------|-------------|
| `error` | Only critical failures. |
| `warn` | Errors and warnings. |
| `info` (default) | Errors, warnings, and normal confirmations. |
| `debug` | Everything, including verbose diagnostics. |

Status messages arrive as OSC strings in the format:
```
[LEVEL] category: description
```

Example: `[INFO] patch: Started patch 'showPatch'`

---

## 12. Practical Workflows

### Workflow 1: Quick sensor test

You want to verify that the device is reading sensors correctly before
integrating with your show.

```
/annieData/bart/msg/test       "value:accelX, ip:192.168.1.10, port:9000, adr:/test"
/annieData/bart/patch/test     "ip:192.168.1.10, port:9000"
/annieData/bart/patch/test/addMsg    "test"
/annieData/bart/patch/test/start
```

Open an OSC monitor on your laptop (192.168.1.10:9000) and watch the values
come in.

### Workflow 2: Sending multiple sensors to a mixer

You have three messages going to the same mixer, each controlling a different
channel:

```
/annieData/bart/patch/mixer    "ip:192.168.1.50, port:7000, adr:/mixer"
/annieData/bart/patch/mixer/adrMode   "prepend"
/annieData/bart/patch/mixer/override  "ip, port"

/annieData/bart/msg/ch1        "value:accelX, adr:/ch1"
/annieData/bart/msg/ch2        "value:accelY, adr:/ch2"
/annieData/bart/msg/ch3        "value:gyroZ, adr:/ch3"

/annieData/bart/patch/mixer/addMsg   "ch1, ch2, ch3"
/annieData/bart/patch/mixer/start
```

The mixer receives:
- `/mixer/ch1` with X acceleration
- `/mixer/ch2` with Y acceleration
- `/mixer/ch3` with Z rotation rate

### Workflow 3: DMX fixture control

All outputs need to be scaled to 0–255:

```
/annieData/bart/patch/dmx      "ip:10.0.0.100, port:7700, low:0, high:255"
/annieData/bart/patch/dmx/override   "ip, port, scale"

/annieData/bart/msg/dim        "value:accelLength, adr:/fixture/1/dimmer"
/annieData/bart/msg/pan        "value:eulerX, adr:/fixture/1/pan"
/annieData/bart/msg/tilt       "value:eulerY, adr:/fixture/1/tilt"

/annieData/bart/patch/dmx/addMsg     "dim, pan, tilt"
/annieData/bart/patch/dmx/period     "20"
/annieData/bart/patch/dmx/start
```

All three messages are scaled to 0–255 because the patch overrides the bounds.

### Workflow 4: Duplicating a setup for a second device

Clone an existing patch to create a second copy aimed at a different IP:

```
/annieData/bart/clone/patch    "dmx, dmx2"
/annieData/bart/patch/dmx2     "ip:10.0.0.101"
/annieData/bart/patch/dmx2/start
```

### Workflow 5: Blackout during scene change

Stop all outputs instantly:

```
/annieData/bart/blackout
```

When ready to resume:

```
/annieData/bart/restore
```

### Workflow 6: Debugging with solo

You suspect the gyro Z value is noisy.  Solo it to isolate:

```
/annieData/bart/patch/mixer/solo   "ch3"
```

Only `ch3` (gyro Z) is sent.  When done:

```
/annieData/bart/patch/mixer/unsolo
```

### Workflow 7: Saving and loading your configuration

After setting up all your messages and patches, save them so they survive a
power cycle:

```
/annieData/bart/save
```

On next boot, load the saved configuration:

```
/annieData/bart/load
```

You can also save individual objects:

```
/annieData/bart/save/msg     "ch1"
/annieData/bart/save/patch   "mixer"
```

To start fresh, clear all saved data:

```
/annieData/bart/nvs/clear
```

---

## 13. Troubleshooting

### The device is not responding to commands

- Verify the device is connected to the same WiFi network as your control
  computer.
- Confirm you are sending to the correct IP address and port.
- Make sure the command address starts with `/annieData/{deviceName}` using the
  exact name you set during provisioning.
- Check that your OSC sender is using UDP (not TCP).

### Values are not arriving at my target

- Make sure the patch is started (`/start`).
- Check that messages have a `value` set — they need a sensor assigned.
- Verify the target IP, port, and address are correct.
- Use the `info` command to inspect a message or patch:
  ```
  /annieData/bart/msg/myFader/info
  /annieData/bart/patch/showPatch/info
  ```
- Use the `list` command to see everything registered:
  ```
  /annieData/bart/list/all   "verbose"
  ```

### Values seem wrong or out of range

- Check the output bounds (`low` / `high`).  The device maps 0–1 to
  [low, high].
- If a patch overrides bounds (`scale` override), the patch's bounds are used
  instead of each message's.  Check the patch's override settings.

### The device needs to be re-provisioned

Power off the device and re-flash it with a factory reset, or clear its stored
preferences through the serial console.  On next boot it will start the
captive portal again.
