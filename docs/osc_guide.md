# TheaterGWD OSC Guide

This guide is for **theater people** who want to control the TheaterGWD sensor device using raw OSC messages — from a lighting console, QLab, TouchDesigner, or any other software that speaks OSC.

No programming required. No GUI required.

---

## Table of Contents

- [What This System Does](#what-this-system-does)
- [Device Setup (Provisioning)](#device-setup-provisioning)
- [OSC Address Format](#osc-address-format)
- [The 29 Sensor Values](#the-29-sensor-values)
- [Config String Format](#config-string-format)
- [Creating Messages](#creating-messages)
- [String Messages](#string-messages)
- [Creating Scenes](#creating-scenes)
- [Direct Command (Fastest Way)](#direct-command-fastest-way)
- [Scaling Output Values](#scaling-output-values)
- [Address Modes](#address-modes)
- [Global Controls](#global-controls)
- [Save & Load (Persistence)](#save--load-persistence)
- [Shows (Named Snapshots)](#shows-named-snapshots)
- [Status Monitoring](#status-monitoring)
- [Listing & Querying](#listing--querying)
- [Clone, Rename, Move](#clone-rename-move)
- [Orientation System (ab7 only)](#orientation-system-ab7-only)
- [Quick Reference Card](#quick-reference-card)
- [Practical Examples](#practical-examples)

---

## What This System Does

The TheaterGWD device is a small wireless sensor that measures motion, rotation, pressure, and orientation. It sends those values as OSC messages over WiFi to your show-control software — a lighting console, QLab, TouchDesigner, Max/MSP, or anything that receives OSC.

The flow:

```
Sensor on performer → WiFi → Your computer → Lighting console / QLab / etc.
```

You tell the device *which* sensor to read, *where* to send it (IP, port, OSC address), and *how fast*. It does the rest.

---

## Device Setup (Provisioning)

When you power on a new (or factory-reset) device for the first time:

1. It creates a WiFi hotspot called **"annieData Setup"**
2. Connect to it from your phone or laptop
3. A setup page opens automatically (or go to `192.168.4.1`)
4. Fill in:
   - **WiFi name (SSID):** your production network
   - **WiFi password**
   - **IP address:** enter a static IP like `192.168.1.100`, or type `dhcp` for automatic
   - **Port:** the UDP port the device listens on (default: `8000`)
   - **Device name:** a short name like `bart` or `sensor1` — this becomes part of every OSC address
5. Submit. The device reboots and joins your WiFi.

After provisioning, the device is ready to receive OSC commands.

---

## OSC Address Format

Every command you send to the device starts with:

```
/annieData/{deviceName}/{command}
```

For example, if your device is named **bart**:

```
/annieData/bart/msg/myMessage
/annieData/bart/scene/myScene/start
/annieData/bart/blackout
```

> Commands are **not case-sensitive**: `addMsg`, `add_msg`, and `addmsg` all work. But message and scene names you create **do** preserve case.

---

## The 29 Sensor Values

The device reads these sensor values and normalizes them all to a range of **0 to 1**:

| Name | What It Measures | Typical Use |
|------|-----------------|-------------|
| `accelX` | Linear acceleration — left/right | Detect lateral movement |
| `accelY` | Linear acceleration — up/down | Detect jumping, lifting |
| `accelZ` | Linear acceleration — forward/back | Detect lunging, stepping |
| `accelLength` | Overall acceleration intensity | General motion intensity |
| `gyroX` | Rotation speed — X axis | Detect rolling |
| `gyroY` | Rotation speed — Y axis | Detect pitching |
| `gyroZ` | Rotation speed — Z axis | Detect spinning |
| `gyroLength` | Overall rotation speed | General rotation intensity |
| `baro` | Barometric pressure / altitude | Detect height changes |
| `roll` | Roll angle | Tilt left/right |
| `pitch` | Pitch angle | Tilt forward/back |
| `yaw` | Yaw angle | Compass heading |
| `twist` | Wrist rotation around arm axis | Gimbal-lock-free wrist roll |
| `heading` | Horizontal pointing direction | Arm sweep left/right |
| `tilt` | Vertical angle above/below horizon | Arm raise/lower |
| `limbFwd` | Acceleration along limb direction | Detect forward/back thrust |
| `limbLat` | Acceleration lateral to limb | Detect sideways movement |
| `limbVert` | Acceleration vertical | Detect raise/drop |
| `twitch` | Limb-frame acceleration magnitude | Overall limb movement intensity |
| `gaccelX` | World-frame acceleration — X | Motion regardless of device tilt |
| `gaccelY` | World-frame acceleration — Y | Motion regardless of device tilt |
| `gaccelZ` | World-frame acceleration — Z | Motion regardless of device tilt |
| `gaccelLength` | World-frame acceleration magnitude | Motion regardless of device tilt |
| `quatI` | Quaternion I | Advanced rotation math |
| `quatJ` | Quaternion J | Advanced rotation math |
| `quatK` | Quaternion K | Advanced rotation math |
| `quatR` | Quaternion R (real/scalar) | Advanced rotation math |
| `low` | Always 0.0 | Constant zero |
| `high` | Always 1.0 | Constant one |

For most theater work, **accelX/Y/Z**, **accelLength**, **gyroLength**, and **roll/pitch/yaw** are the most useful. For body-mounted sensors where gimbal lock is a concern, use **twist/heading/tilt** instead — these use swing-twist decomposition and are stable at any arm angle.

---

## Config String Format

Many commands accept a **config string** — a comma-separated list of `key:value` pairs. For example:

```
"value:accelX, ip:192.168.1.50, port:9000, adr:/sensor/x, low:0, high:255"
```

Here are all the config keys:

| Key | What It Sets | Example |
|-----|-------------|---------|
| `value` | Which sensor to read (or a string pool ref) | `value:accelX` or `value:str1` |
| `ip` | Destination IP address | `ip:192.168.1.50` |
| `port` | Destination UDP port | `port:9000` |
| `adr` | OSC address to send on | `adr:/sensor/x` |
| `low` | Output minimum value | `low:0` |
| `high` | Output maximum value | `high:255` |
| `enabled` | Enable or disable | `enabled:true` |
| `scene` | Assign to a scene | `scene:myScene` |
| `period` | Send interval in ms (scenes) | `period:50` |
| `adrmode` | Address composition mode | `adrmode:prepend` |
| `override` | Scene override flags | `override:ip+port` |
| `gate_src` | Gate source: data stream or `ori:name` (messages and scenes) | `gate_src:twist` or `gate_src:ori:forward` |
| `gate_mode` | Gate behavior: `only`, `not`, or `toggle` (messages); `only` or `not` (scenes) | `gate_mode:only` |
| `gate_lo` | Gate lower threshold (optional) | `gate_lo:0.3` |
| `gate_hi` | Gate upper threshold (optional) | `gate_hi:0.7` |
| `ori_only` | *(backward compat)* Same as `gate_src:ori:X, gate_mode:only` | `ori_only:forward` |
| `ori_not` | *(backward compat)* Same as `gate_src:ori:X, gate_mode:not` | `ori_not:backward` |
| `ternori` | *(backward compat)* Same as `gate_src:ori:X, gate_mode:toggle` | `ternori:upright` |

---

## Creating Messages

A **message** is a single mapping: one sensor value → one OSC destination.

### Create a message

Send to:
```
/annieData/{device}/msg/{messageName}
```

Payload (config string):
```
"value:accelX, ip:192.168.1.50, port:9000, adr:/dimmer/1"
```

This creates a message named `{messageName}` that reads `accelX` and sends it to `192.168.1.50:9000` at address `/dimmer/1`.

### Example: control a DMX dimmer with acceleration

```
Address: /annieData/bart/msg/dimmer1
Payload: "value:accelX, ip:192.168.1.50, port:9000, adr:/dmx/1, low:0, high:255"
```

Now `dimmer1` maps the X-axis acceleration (0–1) to a DMX value (0–255).

### Update a message

Send the same command with new values. Only the keys you include are changed:

```
Address: /annieData/bart/msg/dimmer1
Payload: "high:127"
```

Now the max output is 127 instead of 255.

### Enable / Disable / Delete

```
/annieData/bart/msg/dimmer1/enable
/annieData/bart/msg/dimmer1/disable
/annieData/bart/msg/dimmer1/delete
```

### Get info

```
/annieData/bart/msg/dimmer1/info
```

The device replies with all the message's current settings.

### IP Reference Mode

Instead of a numeric IP, you can reference another message's IP:

```
"ip:ip-dimmer1"
```

This copies `dimmer1`'s IP address, so if you change dimmer1's IP, this message follows.

---

## String Messages

Instead of sending a float value, a message can send a fixed **text string** as an OSC string argument. This is useful for triggering cue labels, mode names, or any string-based command in show-control software.

### Register a string

The device maintains a pool of up to 16 unique strings in flash (`str1` through `str16`). Register a string value first:

```
Address: /annieData/bart/msg/string
Payload: "GO"
```

The device assigns a pool slot and replies to your listener at:

```
/reply/bart/str/registered    →  "str1"
```

Use the returned name (`str1`, `str2`, etc.) as the `value` in a message config:

```
Address: /annieData/bart/msg/cuefire
Payload: "value:str1, ip:192.168.1.20, port:53000, adr:/cue/name"
```

When this message fires, it sends the OSC message `/cue/name` with a string argument `"GO"`.

Identical strings share the same pool slot — re-registering `"GO"` returns `str1` again with no duplicate created.

### Query the string pool

```
Address: /annieData/bart/msg/string
Payload: 0           ← list all registered strings (replies: "str1=GO, str2=SCENE_4, ...")
Payload: 1           ← get str1's value
Payload: 2           ← get str2's value
```

### Example

```
# Register the string
/annieData/bart/msg/string  "scene_4"
# → reply: str1

# Create a message that sends "scene_4" as a string OSC arg
/annieData/bart/msg/sceneTrigger  "value:str1, ip:192.168.1.20, port:53000, adr:/cue/scene"

# Add to a scene and start
/annieData/bart/scene/triggers/addMsg  "sceneTrigger"
/annieData/bart/scene/triggers/start
```

---

## Creating Scenes

A **scene** is a group of messages that send together at a shared rate.

### Create a scene

```
Address: /annieData/bart/scene/myScene
Payload: "ip:192.168.1.50, port:9000, period:50"
```

This creates a scene sending at 50ms intervals (20 times per second).

### Add messages to a scene

```
Address: /annieData/bart/scene/myScene/addMsg
Payload: "dimmer1, dimmer2, dimmer3"
```

### Start / Stop a scene

```
/annieData/bart/scene/myScene/start
/annieData/bart/scene/myScene/stop
```

When started, the scene sends all its messages at the configured rate. When stopped, it goes silent.

### Change send rate

```
Address: /annieData/bart/scene/myScene/period
Payload: 100
```

Now it sends every 100ms (10 times per second). Range: 20ms to 60000ms.

### Remove a message from a scene

```
Address: /annieData/bart/scene/myScene/removeMsg
Payload: "dimmer1"
```

### Enable/Disable individual messages while scene runs

```
/annieData/bart/scene/myScene/solo "dimmer1"     ← only dimmer1 sends
/annieData/bart/scene/myScene/unsolo              ← all messages send again
/annieData/bart/scene/myScene/enableAll           ← re-enable everything
```

### Set a property on all messages at once

```
Address: /annieData/bart/scene/myScene/setAll
Payload: "low:0, high:100"
```

### Scene-level gate

Scenes support their own gate that stacks with per-message gates. The scene gate is evaluated **first** — if it fails, no messages in the scene fire that iteration. If it passes, each message's own gate is evaluated normally.

Configure a scene gate in the scene config string using the same `gate_src`, `gate_mode`, `gate_lo`, `gate_hi` keys. Only `only` and `not` are valid for scene gates (no `toggle`):

```
Address: /annieData/bart/scene/myScene
Payload: "gate_src:ori:upstage, gate_mode:only"
```

Now the entire scene only fires when the `upstage` ori is active. Messages within the scene can still have their own gates for additional fine control.

---

## Direct Command (Fastest Way)

The **direct** command does everything in one step: creates a message, creates a scene, adds the message, and starts sending.

```
Address: /annieData/bart/direct/quickTest
Payload: "value:accelX, ip:192.168.1.50, port:9000, adr:/sensor/x, period:50"
```

This is great for quick tests and demos. You can always refine the setup with individual message/scene commands afterward.

---

## Scaling Output Values

By default, all sensor values output in the range **0 to 1**. Use `low` and `high` to scale to any range:

| Setting | Output Range | Use Case |
|---------|-------------|----------|
| `low:0, high:1` | 0–1 | Default (normalized) |
| `low:0, high:255` | 0–255 | DMX channels |
| `low:0, high:100` | 0–100 | Percentage faders |
| `low:-1, high:1` | -1 to 1 | Centered values (pan) |
| `low:50, high:200` | 50–200 | Custom range |

### Scene-level override

If a scene has `override:low+high` set, the scene's low/high values override those of every message in the scene:

```
Address: /annieData/bart/scene/myScene
Payload: "low:0, high:255, override:low+high"
```

Now all messages in `myScene` output 0–255 regardless of their individual settings.

---

## Address Modes

Address modes control how the final OSC address is built when a scene and message both have addresses.

### Set address mode

```
Address: /annieData/bart/scene/myScene/adrMode
Payload: "prepend"
```

### The four modes

**fallback** (default) — Use the message address. If the message has no address, use the scene address.

```
Scene: /mixer    Message: /fader1    → sends to /fader1
Scene: /mixer    Message: (none)     → sends to /mixer
```

**override** — Always use the scene address, ignoring the message address.

```
Scene: /mixer    Message: /fader1    → sends to /mixer
```

**prepend** — Scene address + message address joined together.

```
Scene: /mixer    Message: /fader1    → sends to /mixer/fader1
```

**append** — Message address + scene address joined together.

```
Scene: /mixer    Message: /fader1    → sends to /fader1/mixer
```

`prepend` is the most common for organizing outputs under a shared prefix.

---

## Global Controls

### Blackout — stop everything immediately

```
/annieData/bart/blackout
```

All scenes stop sending. Use this during emergencies or between cues.

### Restore — restart everything

```
/annieData/bart/restore
```

All scenes resume sending.

### On_Change — suppress duplicate values

```
/annieData/bart/on_change "on"
/annieData/bart/on_change "off"
```

When on, the device skips sending a value if it hasn't changed since last time. Reduces network traffic for slow-moving sensors.

### Tare — zero the orientation

```
/annieData/bart/tare
/annieData/bart/tare/reset
/annieData/bart/tare/status
```

Tare captures the current orientation as the "zero" reference. Euler values become relative to this pose. Reset clears it.

---

## Save & Load (Persistence)

### Save current state to device flash

```
/annieData/bart/save
```

This writes all current scenes and messages to the device's non-volatile storage (NVS). They survive power cycles.

### Load saved state

```
/annieData/bart/load
```

Restores whatever was last saved.

### Save/load individual items

```
/annieData/bart/save/msg "dimmer1"
/annieData/bart/save/scene "myScene"
```

### Factory reset

```
/annieData/bart/nvs/clear
```

Erases all saved OSC data (scenes, messages, oris). The WiFi provisioning settings are NOT erased.

---

## Shows (Named Snapshots)

Shows let you save the entire device state under a name and switch between configurations.

### Save a show

```
/annieData/bart/show/save/rehearsal_v3
```

### Load a show (two steps)

Loading is a two-step process to prevent accidents:

```
/annieData/bart/show/load/rehearsal_v3
```

The device replies asking for confirmation. Then send:

```
/annieData/bart/show/load/confirm
```

### List shows

```
/annieData/bart/show/list
```

The device replies with a list of saved show names.

### Delete / Rename

```
/annieData/bart/show/delete/rehearsal_v3

Address: /annieData/bart/show/rename
Payload: "rehearsal_v3, opening_night"
```

### Limits

The device can store up to **16 shows** in flash memory. If you need more, use the annieData Control Center — it saves shows as JSON files on your computer with no limit.

---

## Status Monitoring

You can tell the device to send status messages (errors, warnings, info) to your computer.

### Set up status reporting

```
Address: /annieData/bart/status/config
Payload: "ip:192.168.1.50, port:9001, adr:/device/status"
```

### Set minimum level

```
Address: /annieData/bart/status/level
Payload: "info"
```

Levels from least to most verbose: `error`, `warn`, `info`, `debug`.

Status messages arrive as strings like:
```
"[INFO] scene: myScene started with 3 messages"
```

---

## Listing & Querying

### List all messages

```
/annieData/bart/list/msgs
/annieData/bart/list/msgs "verbose"
```

### List all scenes

```
/annieData/bart/list/scenes
/annieData/bart/list/scenes "verbose"
```

### List everything

```
/annieData/bart/list/all
/annieData/bart/list/all "verbose"
```

`verbose` includes full details (IP, port, address, sensor, bounds, etc.).

### Get details for one item

```
/annieData/bart/msg/dimmer1/info
/annieData/bart/scene/myScene/info
```

### Flush (sync)

```
/annieData/bart/flush
```

The device replies "OK" once all preceding commands have been processed. Useful when sending batches of commands and you need to know when they're done.

---

## Clone, Rename, Move

### Clone a message

```
Address: /annieData/bart/clone/msg
Payload: "dimmer1, dimmer1_copy"
```

### Clone a scene (with all its messages)

```
Address: /annieData/bart/clone/scene
Payload: "myScene, myScene_copy"
```

### Rename

```
Address: /annieData/bart/rename/msg
Payload: "dimmer1, mainDimmer"

Address: /annieData/bart/rename/scene
Payload: "myScene, actOneLights"
```

### Move a message to a different scene

```
Address: /annieData/bart/move
Payload: "dimmer1, otherScene"
```

---

## Orientation System
> Available on all TheaterGWD devices with a 9-DOF IMU.

### What are orientations?

An **orientation** (or "ori") is a saved physical pose of the device — like "arm raised," "pointing forward," or "resting on table." The device remembers the pose and can detect when it's in that position again.

You can then make messages conditional: "only send this value when the performer's arm is raised" or "send 1 when pointing forward, 0 otherwise."

### Recording an ori via OSC

**Start a timed recording session:**

```
/annieData/bart/ori/record/start/armRaised
```

Hold the device in the desired position. The device collects samples for several seconds.

**Stop recording:**

```
/annieData/bart/ori/record/stop
```

The device processes the samples and saves the orientation.

**Or save instantly** (single snapshot):

```
/annieData/bart/ori/save/armRaised
```

### Button shortcut (on the device)

- **Short tap Button A** (< 300ms): instant save to the selected ori slot
- **Hold Button A** (> 300ms): start/stop timed recording
- **Button B**: cycle through ori slots

### Using gates with messages

Gates let you conditionally control when a message sends. The gate source can be an orientation or any data stream.

**Orientation-based gates** (only send when a specific ori is active):
```
"value:accelX, ip:192.168.1.50, port:9000, adr:/arm/accel, gate_src:ori:armRaised, gate_mode:only"
```

**Suppress when an ori is active:**
```
"value:gyroLength, ip:192.168.1.50, port:9000, adr:/spin, gate_src:ori:resting, gate_mode:not"
```

**Toggle based on ori** (send 1 when ori matches, 0 otherwise — ignores the sensor value):
```
"value:high, ip:192.168.1.50, port:9000, adr:/trigger/armUp, gate_src:ori:armRaised, gate_mode:toggle"
```

**Data-stream gate** (only send when twist is in a range):
```
"value:accelX, ip:192.168.1.50, port:9000, adr:/wrist/accel, gate_src:twist, gate_mode:only, gate_lo:0.3, gate_hi:0.7"
```

This message only sends when the `twist` value is between 0.3 and 0.7.

**Data-stream gate with threshold shorthand:**
- Both `gate_lo` and `gate_hi` set: active when value is in [lo, hi]
- Only `gate_lo`: active when value >= lo ("greater than")
- Only `gate_hi`: active when value <= hi ("less than")
- Neither: active when value >= 0.5

> **Backward compatibility:** The old keys `ori_only`, `ori_not`, and `ternori` still work and translate automatically to their gate equivalents.

### Adjusting sensitivity

**Tolerance** — how close the device needs to be to a saved pose (in degrees):
```
Address: /annieData/bart/ori/tolerance
Payload: 15
```

Higher = more forgiving. Default is 10 degrees.

**Threshold** — minimum motion speed (rad/s) to consider matching:
```
Address: /annieData/bart/ori/threshold
Payload: 0.5
```

**Strict mode** — when on, if no ori is a close enough match, none is active:
```
/annieData/bart/ori/strict "on"
/annieData/bart/ori/strict "off"
```

### General Ori (fallback in strict mode)

When strict mode is on, you can designate one ori as the **general fallback** — if no other ori matches closely enough, the general ori is considered active instead of "nothing."

```
/annieData/bart/ori/general/resting
```

Now if the device is between recorded poses, `resting` is treated as active. Clear it:

```
/annieData/bart/ori/general/none
```

You can also send the name as a string payload:
```
Address: /annieData/bart/ori/general
Payload: "resting"
```

### Ori Watch (push notifications)

Enable push notifications when the active ori changes:

```
/annieData/bart/ori/watch "on"
/annieData/bart/ori/watch "off"
/annieData/bart/ori/watch          ← toggle
```

When enabled, the device sends an OSC reply to your listener whenever the active ori changes (including transitions to "nothing active" in strict mode). The reply arrives at:

```
/reply/bart/ori/watch    →  "active: armRaised"
```

Useful for logging orientation transitions or triggering external events without polling `/ori/active`.

### Managing oris

```
/annieData/bart/ori/list                    ← list all saved oris
/annieData/bart/ori/active                  ← which ori is currently detected
/annieData/bart/ori/info/armRaised          ← details for one ori
/annieData/bart/ori/delete/armRaised        ← delete one ori
/annieData/bart/ori/clear                   ← delete all oris
/annieData/bart/ori/reset/armRaised         ← clear samples (re-record)
/annieData/bart/ori/select/armRaised        ← select for button editing
/annieData/bart/ori/color/armRaised "255,0,0"  ← set LED color (red)
```

---

## Quick Reference Card

All commands use the prefix `/annieData/{device}/`. Payload column shows the expected argument.

### Messages

| Address | Payload | Action |
|---------|---------|--------|
| `msg/{name}` | config string | Create/update message |
| `msg/{name}/delete` | — | Delete |
| `msg/{name}/enable` | — | Enable |
| `msg/{name}/disable` | — | Disable |
| `msg/{name}/info` | — | Query info |
| `msg/string` | string | Register string → reply: `str1`/`str2`/… |
| `msg/string` | `0` | List all pool entries |
| `msg/string` | `N` | Get strN value |

### Scenes

| Address | Payload | Action |
|---------|---------|--------|
| `scene/{name}` | config string | Create/update scene |
| `scene/{name}/delete` | — | Delete scene |
| `scene/{name}/start` | — | Start sending |
| `scene/{name}/stop` | — | Stop sending |
| `scene/{name}/enable` | — | Enable (restart) |
| `scene/{name}/disable` | — | Disable (pause) |
| `scene/{name}/addMsg` | `"msg1, msg2"` | Add messages |
| `scene/{name}/removeMsg` | `"msgName"` | Remove message |
| `scene/{name}/period` | ms (int) | Set send interval |
| `scene/{name}/override` | `"ip+port+adr+low+high"` | Set overrides |
| `scene/{name}/adrMode` | mode string | Set address mode |
| `scene/{name}/setAll` | config string | Set property on all msgs |
| `scene/{name}/solo` | `"msgName"` | Solo one message |
| `scene/{name}/unsolo` | — | Unsolo |
| `scene/{name}/enableAll` | — | Enable all messages |
| `scene/{name}/info` | — | Query info |

### Direct

| Address | Payload | Action |
|---------|---------|--------|
| `direct/{name}` | config string | One-step create + start |

### Global

| Address | Payload | Action |
|---------|---------|--------|
| `blackout` | — | Stop all scenes |
| `restore` | — | Restart all scenes |
| `on_change` | `"on"` / `"off"` | Duplicate suppression |
| `tare` | — | Zero orientation |
| `tare/reset` | — | Clear tare |
| `tare/status` | — | Query tare |
| `flush` | — | Sync (reply when done) |

### Save / Load

| Address | Payload | Action |
|---------|---------|--------|
| `save` | — | Save all to flash |
| `save/msg` | `"name"` | Save one message |
| `save/scene` | `"name"` | Save one scene |
| `load` | — | Load all from flash |
| `nvs/clear` | — | Factory reset OSC data |

### Shows

| Address | Payload | Action |
|---------|---------|--------|
| `show/save/{name}` | — | Save show |
| `show/load/{name}` | — | Stage load |
| `show/load/confirm` | — | Confirm load |
| `show/list` | — | List shows |
| `show/delete/{name}` | — | Delete show |
| `show/rename` | `"old, new"` | Rename show |

### Status

| Address | Payload | Action |
|---------|---------|--------|
| `status/config` | config string | Set status dest |
| `status/level` | level string | Set min level |

### List / Query

| Address | Payload | Action |
|---------|---------|--------|
| `list/msgs` | `["verbose"]` | List messages |
| `list/scenes` | `["verbose"]` | List scenes |
| `list/all` | `["verbose"]` | List everything |

### Clone / Rename / Move

| Address | Payload | Action |
|---------|---------|--------|
| `clone/msg` | `"src, dest"` | Clone message |
| `clone/scene` | `"src, dest"` | Clone scene |
| `rename/msg` | `"old, new"` | Rename message |
| `rename/scene` | `"old, new"` | Rename scene |
| `move` | `"msg, scene"` | Move message to scene |

### Orientations
| Address | Payload | Action |
|---------|---------|--------|
| `ori/register/{name}` | `[r,g,b]` | Pre-register slot |
| `ori/save/{name}` | — | Instant save |
| `ori/delete/{name}` | — | Delete |
| `ori/clear` | — | Delete all |
| `ori/list` | — | List oris |
| `ori/active` | — | Query active |
| `ori/info/{name}` | — | Ori details |
| `ori/reset/{name}` | — | Clear samples |
| `ori/select/{name}` | — | Select for button |
| `ori/color/{name}` | `"r,g,b"` | Set LED color |
| `ori/threshold` | float | Motion gate |
| `ori/tolerance` | degrees | Match tolerance |
| `ori/strict` | `"on"` / `"off"` | Strict matching |
| `ori/general/{name}` | — | Set general fallback ori |
| `ori/general/none` | — | Clear general ori |
| `ori/watch` | `"on"` / `"off"` / — | Push on ori change (toggle) |
| `ori/record/start/{name}` | — | Start recording |
| `ori/record/stop` | — | Stop recording |
| `ori/record/cancel` | — | Cancel recording |
| `ori/record/status` | — | Recording status |

---

## Practical Examples

### Example 1: Send acceleration to a lighting console (ETC Eos)

Your Eos console is at `192.168.1.10:8001`.

```
# Create a message mapping X-axis acceleration to a fader
/annieData/bart/msg/fader1  "value:accelX, ip:192.168.1.10, port:8001, adr:/eos/fader/1, low:0, high:100"

# Create a scene and add it
/annieData/bart/scene/lighting  "period:50"
/annieData/bart/scene/lighting/addMsg  "fader1"
/annieData/bart/scene/lighting/start
```

Now the performer's lateral motion controls fader 1 on Eos, updating 20 times per second.

### Example 2: Multi-sensor setup for audio (QLab)

QLab is at `192.168.1.20:53000`.

```
# Three messages for different audio parameters
/annieData/bart/msg/volume     "value:accelLength, ip:192.168.1.20, port:53000, adr:/cue/1/level, low:-60, high:0"
/annieData/bart/msg/pan        "value:yaw, ip:192.168.1.20, port:53000, adr:/cue/1/pan, low:-1, high:1"
/annieData/bart/msg/reverb     "value:gyroLength, ip:192.168.1.20, port:53000, adr:/cue/1/reverb, low:0, high:100"

# Group them in a scene
/annieData/bart/scene/audio  "period:30"
/annieData/bart/scene/audio/addMsg  "volume, pan, reverb"
/annieData/bart/scene/audio/start
```

### Example 3: Trigger video in TouchDesigner

TouchDesigner listens at `192.168.1.30:7000`.

```
# Use gyroLength as a motion trigger
/annieData/bart/direct/motionTrigger  "value:gyroLength, ip:192.168.1.30, port:7000, adr:/motion/intensity, period:50"
```

Done in one command with `direct`.

### Example 4: Duplicate a full rig to a second sensor

You've set up `bart` and want `sensor2` to have the same setup:

```
# Clone the entire scene (copies all its messages too)
/annieData/bart/clone/scene  "lighting, lighting_copy"

# Now on sensor2, create similar messages pointing to different addresses
```

Or use annieData's multi-device features for more complex multi-sensor setups.

### Example 5: Debugging — solo a message

Something's not working in your scene. Isolate one message:

```
/annieData/bart/scene/lighting/solo  "fader1"
```

Now only `fader1` sends. Check that its values arrive correctly. When done:

```
/annieData/bart/scene/lighting/unsolo
```

### Example 6: Switch configurations during a show

Save different setups for different acts:

```
# During tech rehearsal, set up Act 1
# ... (create all messages and scenes) ...
/annieData/bart/show/save/act1

# Set up Act 2
# ... (modify messages and scenes) ...
/annieData/bart/show/save/act2

# During the show, switch:
/annieData/bart/show/load/act1
/annieData/bart/show/load/confirm

# Later:
/annieData/bart/show/load/act2
/annieData/bart/show/load/confirm
```
