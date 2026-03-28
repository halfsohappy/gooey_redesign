# TheaterGWD Technical Guide

## For Computer Engineers

This document explains the complete TheaterGWD firmware codebase.  It covers the
architecture, every module, and every OSC command the device supports.  The
firmware runs on an ESP32-S3 microcontroller and turns sensor data into OSC
messages that can be consumed by lighting consoles, audio mixers, or any other
OSC-capable software.

---

## Table of Contents

1. [System Overview](#1-system-overview)
2. [Build System](#2-build-system)
3. [Hardware Layer](#3-hardware-layer)
4. [Network & Provisioning](#4-network--provisioning)
5. [Data Streams](#5-data-streams)
6. [Core Object Model](#6-core-object-model)
7. [The OscRegistry](#7-the-oscregistry)
8. [Sending Engine & FreeRTOS Tasks](#8-sending-engine--freertos-tasks)
9. [Status Reporting](#9-status-reporting)
10. [Command Reference](#10-command-reference)
11. [Data Flow Diagram](#11-data-flow-diagram)
12. [File Map](#12-file-map)
13. [Concurrency & Mutex Strategy](#13-concurrency--mutex-strategy)
14. [Memory Model](#14-memory-model)
15. [Extending the Firmware](#15-extending-the-firmware)

---

## 1. System Overview

TheaterGWD is firmware for an ESP32-S3 microcontroller.  A single PlatformIO
project supports two board variants, selected at compile time:

### Bart board (default)

- **IMU** (SparkFun ISM330DHCX) — 3-axis accelerometer + 3-axis gyroscope
- **Magnetometer** (SparkFun MMC5983MA) — 3-axis magnetic field
- **Barometer** (Adafruit BMP5xx) — atmospheric pressure / altitude
- **NeoPixel LED** — status indicator
- **USB-C / Serial** — for debugging and firmware upload

### ab7 board (`-DAB7_BUILD`)

- **IMU** — BNO085 (SPI) — rotation vector, linear acceleration, calibrated
  gyro
- **SK6812 addressable LED** on GPIO 7 — status indicator
- **Two buttons** — GPIO 0 (A) and GPIO 14 (B), active-low
- **No barometer** — `data_streams[BARO]` is always 0
- **Orientation tracker** — save/recall named orientations ("oris") via
  quaternion geodesic matching with a gyro-based motion gate

Both boards connect to a WiFi network (configured through a captive portal on
first boot), then continuously read their sensors and make the data available
as normalised `[0, 1]` floating-point values.

Users configure **OscMessages** and **OscScenes** remotely by sending OSC
commands to the device.  Once configured, the device autonomously sends sensor
values as OSC float messages to the destinations defined by those scenes, at
the configured polling rates.

### Key concepts

| Term        | Meaning |
|-------------|---------|
| **OscMessage** | Maps one sensor value to one OSC destination (IP + port + address). |
| **OscScene**   | Groups one or more OscMessages.  A FreeRTOS task sends them all at a configurable rate. |
| **Override**   | A scene can force its own IP / port / address / bounds on all its messages. |
| **Address Mode** | Controls how scene and message OSC addresses are composed (fallback, override, prepend, append). |
| **StatusReporter** | Sends status / error / progress strings to a monitoring device. |
| **Ori** *(ab7 only)* | A saved orientation (quaternion).  Messages can be conditioned on which ori is active. |

---

## 2. Build System

The project uses **PlatformIO** with the **Arduino framework** for
**ESP32-S3**.  A single `platformio.ini` supports all board variants.

### Environments

| Environment | Board | Build flags | Libraries |
|-------------|-------|-------------|-----------|
| `bart` *(default)* | Bart PCB | — | BMP5xx, ISM330DHCX, MMC5983MA, SensorFusion |
| `ab7` | ab7 PCB | `-DAB7_BUILD` | Adafruit BNO08x |

All environments share common settings and use the
[halfsohappy/MicroOsc](https://github.com/halfsohappy/MicroOsc) fork,
[FastLED](https://github.com/FastLED/FastLED), and the
[WiFiProvisioner](https://github.com/halfsohappy/WiFiProvisioner.git) library.

### platformio.ini highlights

```ini
[platformio]
default_envs = bart

[env]
platform  = espressif32
board     = esp32-s3-devkitc-1
framework = arduino
lib_deps  =
    FastLED
    MicroOSC=https://github.com/halfsohappy/MicroOsc
    WiFiProvisioner.git=https://github.com/halfsohappy/WiFiProvisioner.git#...

[env:bart]
lib_deps = ${env.lib_deps}
    adafruit/Adafruit BMP5xx Library@^1.0.2
    sparkfun/SparkFun 6DoF ISM330DHCX@^1.0.6
    ...

[env:ab7]
build_flags = ${env.build_flags}  -DAB7_BUILD
lib_deps = ${env.lib_deps}
    adafruit/Adafruit BNO08x@^1.2.5
```

### Build commands

```bash
pio run                    # build default environment (bart)
pio run -e ab7             # build for ab7 with BNO085 IMU
pio run -t upload          # compile and flash
```

### Conditional compilation

Source files use `#ifdef AB7_BUILD` to gate ab7-specific code (orientation
tracker, buttons, LED, real IMU sensor task).  Bart-specific code (simulated
data, barometer, ISM330DHCX/MMC5983MA sensors) is gated with
`#ifndef AB7_BUILD`.  Shared OSC logic compiles identically for both boards.

---

## 3. Hardware Layer

### Bart — `bart_hardware.h` / `bart_hardware.cpp`

Pin mappings, sensor object declarations (`extern`), and initialisation
routines for the Bart PCB:

| Function              | Purpose |
|-----------------------|---------|
| `begin_pins(b13, b46, cen1, cen2)` | Configure GPIO direction and mux selects. |
| `begin_baro(CS_BAR)`  | Initialise the BMP5xx barometer over SPI. |
| `begin_imu(ICS, MCS)` | Initialise the ISM330DHCX IMU and MMC5983MA magnetometer. |
| `process_imu_data()`  | Normalise raw IMU readings. |

All Bart sensor drivers communicate over a shared SPI bus.

### ab7 — `ab7_hardware.h` / `ab7_hardware.cpp`

The ab7 board uses a BNO085 IMU over SPI (CS=10, MOSI=11, SCK=12, MISO=13,
INT=4, RST=5, WAKE=6).

| Function              | Purpose |
|-----------------------|---------|
| `begin_pins()`        | Configure buttons (GPIO 0, 14) with internal pull-ups. |
| `begin_imu()`         | Initialise the BNO085.  Blocks on failure. |
| `imu_data_available()` | Poll the IMU; returns true if fresh data was cached. |
| `imu_get_quat(qi,qj,qk,qr)` | Read cached rotation quaternion. |
| `imu_get_accel(ax,ay,az)` | Read cached linear acceleration (m/s²). |
| `imu_get_gyro(gx,gy,gz)` | Read cached gyroscope (rad/s). |
| `quat_to_euler(qi,qj,qk,qr, roll,pitch,yaw)` | Convert quaternion to Euler angles (degrees). |

> **SPI note:** On ESP32-S3 with Adafruit BNO08x, do **not** pass the CS pin
> to `SPI.begin()`.  Use `SPI.begin(SCK, MISO, MOSI)` without SS.  Passing CS
> calls `spiAttachSS()`, which conflicts with the Adafruit library's software
> CS management and causes all-zero sensor reads.

---

## 4. Network & Provisioning

**File:** `network_setup.h`

On first boot (or after a factory reset), the device has no WiFi credentials.
It starts a **captive portal** using the WiFiProvisioner library:

1. The ESP32 creates a soft-AP named *"annieData Setup"*.
2. Connecting to it presents a web page where you enter:
   - WiFi SSID and password
   - Static IP address (or `"dhcp"`)
   - UDP port number
   - Device name (used as the OSC address prefix)
3. On success the credentials are written to `Preferences` (NVS flash) and the
   device reboots into normal mode.

NVS keys used (namespace `device_config`):

| Key | Type | Description |
|-----|------|-------------|
| `provisioned` | bool | Whether the device has been provisioned. |
| `ssid` | String | WiFi SSID. |
| `net_pass` | String | WiFi password. |
| `use_dhcp` | bool | Whether to use DHCP. |
| `static_ip` | String | Static IP address (if not DHCP). |
| `port` | int | UDP port number. |
| `device_adr` | String | Device name (OSC address prefix). |

After provisioning, `begin_udp()` (in `osc_engine.h`) connects to WiFi and
opens a UDP listener on the configured port.  The WiFi connection timeout is
20 seconds; if it fails, provisioning is cleared and the device reboots into
portal mode.

---

## 5. Data Streams

**File:** `data_streams.h`

The global array `float data_streams[NUM_DATA_STREAMS]` (where
`#define NUM_DATA_STREAMS 22`) holds the current normalised sensor values.
All values are in the range **[0, 1]**.

| Index | Name | Physical meaning |
|-------|------|-----------------|
| 0  | `ACCELX` | Accelerometer X (body frame) |
| 1  | `ACCELY` | Accelerometer Y (body frame) |
| 2  | `ACCELZ` | Accelerometer Z (body frame) |
| 3  | `ACCELLENGTH` | Acceleration magnitude |
| 4  | `GYROX` | Gyroscope X |
| 5  | `GYROY` | Gyroscope Y |
| 6  | `GYROZ` | Gyroscope Z |
| 7  | `GYROLENGTH` | Gyroscope magnitude |
| 8  | `BARO` | Barometric pressure |
| 9  | `EULERX` | Euler angle X (roll) |
| 10 | `EULERY` | Euler angle Y (pitch) |
| 11 | `EULERZ` | Euler angle Z (yaw) |
| 12 | `GACCELX` | Global-frame acceleration X |
| 13 | `GACCELY` | Global-frame acceleration Y |
| 14 | `GACCELZ` | Global-frame acceleration Z |
| 15 | `GACCELLENGTH` | Global-frame acceleration magnitude |
| 16 | `CONST_ZERO` | Fixed 0.0 — always outputs the `low` bound |
| 17 | `CONST_ONE` | Fixed 1.0 — always outputs the `high` bound |
| 18 | `QUAT_I` | Quaternion X component, normalised: `qi*0.5+0.5` |
| 19 | `QUAT_J` | Quaternion Y component, normalised: `qj*0.5+0.5` |
| 20 | `QUAT_K` | Quaternion Z component, normalised: `qk*0.5+0.5` |
| 21 | `QUAT_R` | Quaternion scalar (W), normalised: `qr*0.5+0.5` |

### Simulated data (Bart only)

For development and testing, `update_simulated_data()` fills the array with
sine waves at distinct frequencies (0.05 Hz – 2.3 Hz).  This is called from
the sensor FreeRTOS task every 10 ms.  This function is only available when
building without `AB7_BUILD` (i.e. Bart).

### Real IMU data (ab7 only)

When `AB7_BUILD` is defined, the sensor task reads the selected IMU at ~100 Hz,
converts quaternions to Euler angles, computes gravity-free linear acceleration
in both body frame (`ACCELX/Y/Z`) and global frame (`GACCELX/Y/Z`, rotated by
the quaternion), writes the raw quaternion components (`QUAT_I/J/K/R`), and
writes all values to `data_streams[]`.  The barometer stream is always 0.  The
orientation tracker is also updated each cycle.

The Euler decomposition is auto-selected at tare time (`euler_order` global,
0 = ZYX default, 1 = ZXY).  ZXY is chosen when the device Y-axis is most
vertical at the tare pose, avoiding ZYX's singularity.  Quaternion streams are
always raw (untared); normalise with `low:-1 high:1` to recover `[-1, 1]`.

### Helper functions

- `data_stream_name(int index)` → human-readable name for an index.
- `data_stream_index_from_name(String)` → case-insensitive name-to-index
  lookup.
- `data_stream_index_from_ptr(float*)` → reverse-lookup from a pointer into
  `data_streams[]`.

---

## 6. Core Object Model

### OscMessage (`osc_message.h`)

An OscMessage binds a single sensor stream to an outbound OSC destination:

```
┌─────────────────────────────────────────┐
│ OscMessage                              │
├─────────────────────────────────────────┤
│ name         : String                   │
│ ip           : IPAddress                │
│ port         : unsigned int             │
│ osc_address  : String                   │
│ scene        : OscScene* (optional)     │
│ value_ptr    : float*  → data_streams[] │
│ bounds[2]    : float  (low, high)       │
│ enabled      : bool                     │
│ ori_only     : String  (ab7 only)       │
│ ori_not      : String  (ab7 only)       │
│ ternori      : String  (ab7 only)       │
│ exist        : ExistFlags               │
├─────────────────────────────────────────┤
│ sendable()   : bool                     │
│ from_config_str(csv) : bool             │
│ to_info_string(verbose) : String        │
│ operator*(other) : OscMessage           │
└─────────────────────────────────────────┘
```

**ExistFlags** tracks which fields have been explicitly set.  This enables
sparse configuration: you can set just `ip` and `port` in one command, then
set `value` later, and the message remembers both.

**The merge operator `*`:** `a * b` returns a new message whose fields come
from `a` where set, falling back to `b` otherwise.  This is used when applying
a partial config on top of an existing message.

**Bounds / scaling:** Since all sensor values are normalised to [0, 1], the
bounds define the output range.  The send task maps the value:
`output = low + value × (high − low)`.  Default bounds are [0, 1] (no
scaling).

**`from_config_str()`** parses a CSV string like
`"ip:192.168.1.100, port:9000, adr:/mixer/fader1, value:accelX, low:0, high:255"`.
It supports two separator modes:
- `:` (colon) — the value is a literal.
- `-` (dash) — the value is a name in the registry to look up (reference mode).

**Ori-conditional fields** (ab7 only): `ori_only` and `ori_not` allow messages
to send only when a specific orientation is (or is not) the active match.
Config keys: `ori_only:{name}`, `ori_not:{name}`.  These are stored on the
message and checked in the send task.  On Bart builds the fields exist but are
never checked.

**Ternary ori field** (ab7 only): `ternori` names an ori.  When set, the
message ignores `value_ptr` and sends `bounds[1]` (high) when the named ori is
active, `bounds[0]` (low) when it is not.  Config key: `ternori:{name}`.
Saved to NVS.

### OscScene (`osc_scene.h`)

An OscScene groups messages and manages their transmission:

```
┌──────────────────────────────────────────────┐
│ OscScene                                     │
├──────────────────────────────────────────────┤
│ name           : String                      │
│ ip / port / osc_address                      │
│ bounds[2]      : float  (scene-level scale)  │
│ address_mode   : AddressMode enum            │
│ send_period_ms : unsigned int  (default 50)  │
│ enabled        : bool                        │
│ task_handle    : TaskHandle_t                │
│ overrides      : OverrideFlags               │
│ msg_indices[]  : int[MAX_MSGS_PER_PATCH]     │
│ msg_count      : uint8_t                     │
├──────────────────────────────────────────────┤
│ add_msg(idx)   remove_msg(idx)               │
│ has_msg(idx)   to_info_string(verbose)       │
└──────────────────────────────────────────────┘
```

**Period clamping:** The send period is enforced to the range
`MIN_PATCH_PERIOD_MS` (20 ms) to `MAX_PATCH_PERIOD_MS` (60 000 ms) via
`clamp_scene_period_ms()`.  This prevents accidental runaway send rates and
excessively long sleeps.  The clamping is applied in command parsing, NVS
load, clone, and the task delay itself.

#### OverrideFlags

When an override flag is **true**, every message in the scene uses the
**scene's** value for that field instead of its own:

| Flag   | Effect when ON |
|--------|----------------|
| `ip`   | All messages use the scene's IP. |
| `port` | All messages use the scene's port. |
| `adr`  | All messages use the scene's OSC address (see also AddressMode). |
| `low`  | All messages use the scene's `bounds[0]` (low scale). |
| `high` | All messages use the scene's `bounds[1]` (high scale). |

When an override flag is **false**, each message uses its own value, with the
scene providing a fallback if the message's value is not set.

#### AddressMode

Controls how the scene's `osc_address` and a message's `osc_address` are
combined when sending:

| Mode | Result | Example (scene=`/mixer`, msg=`/fader1`) |
|------|--------|----------------------------------------|
| `ADR_FALLBACK` (default) | Message address if set, else scene address. | `/fader1` |
| `ADR_OVERRIDE` | Scene address replaces message address. | `/mixer` |
| `ADR_PREPEND` | Scene address + message address. | `/mixer/fader1` |
| `ADR_APPEND` | Message address + scene address. | `/fader1/mixer` |

---

## 7. The OscRegistry

**File:** `osc_registry.h`

The OscRegistry is a **Meyer's singleton** that owns every OscScene and
OscMessage in fixed-size arrays:

```cpp
OscScene   scenes[MAX_OSC_PATCHES];   // 64 slots
OscMessage messages[MAX_OSC_MESSAGES]; // 256 slots
```

### Key methods

| Method | Purpose |
|--------|---------|
| `find_scene(name)` | Case-insensitive lookup.  Returns `nullptr` if not found. |
| `find_msg(name)` | Same for messages. |
| `get_or_create_scene(name)` | Find or create.  Returns `nullptr` if full. |
| `get_or_create_msg(name)` | Same for messages. |
| `update_scene(src)` | Merge only the `exist`-flagged fields from `src`. |
| `update_msg(src)` | Same for messages. |
| `delete_scene(name)` | Delete + clean up all references. |
| `delete_msg(name)` | Delete + remove from any scene. |
| `scene_index(ptr)` / `msg_index(ptr)` | Convert pointer to array index. |

### Thread safety

The registry provides a FreeRTOS mutex via `lock()` / `unlock()`.  All command
handlers and send tasks hold this mutex during access.

---

## 8. Sending Engine & FreeRTOS Tasks

**File:** `osc_engine.h`

### Transport globals

```cpp
WiFiUDP           Udp;             // shared UDP socket
MicroOscUdp<1024> osc(&Udp);      // OSC encoder/decoder
```

### Send mutex

A separate `osc_send_mutex()` serialises all `osc.sendFloat()` /
`osc.sendString()` calls across tasks, since the MicroOscUdp instance is not
thread-safe.

### Scene send task

Each running scene creates a FreeRTOS task (`scene_send_task`) that loops:

```
loop:
  vTaskDelay(send_period_ms)         // clamped to [20, 60000]
  if not enabled → continue
  lock registry
  for each message in scene:
    skip if disabled or no value_ptr
    [ab7] skip if ori_only/ori_not condition fails
    resolve effective IP, port, address, bounds
    read sensor value from data_streams[]
    map [0,1] → [low, high]
    if send logging → prepare log line
    lock send mutex
    osc.setDestination(ip, port)
    osc.sendFloat(address, value)
    unlock send mutex
    if send logging → print log line
  unlock registry
```

### Send logging

The engine supports per-message send logging.  When enabled (via the
`sends on` serial command), each outbound message is logged to serial:
```
[SEND] 192.168.1.100:9000 /mixer/fader1 = 0.654321
```
This is controlled by `set_send_logging_enabled()` /
`get_send_logging_enabled()` in `osc_engine.h`.

### Resolution logic

For each field (ip, port, address, bounds), the resolution follows:

1. If the **scene overrides** the field AND the scene has it set → **scene
   wins**.
2. Else if the **message** has it set → **message wins**.
3. Else if the **scene** has it set (no override, just fallback) → **scene**.
4. Else → empty/zero (message is skipped).

For addresses, the `AddressMode` adds composition:

- **Prepend:** `scene.adr + msg.adr` (e.g. `/mixer/fader1`)
- **Append:** `msg.adr + scene.adr` (e.g. `/fader1/mixer`)

For bounds, the mapping is:
```
output = low + sensor_value × (high − low)
```
where `sensor_value` is always in [0, 1].

### Lifecycle helpers

| Function | Purpose |
|----------|---------|
| `start_scene(p)` | Create the FreeRTOS task, set `enabled = true`. |
| `stop_scene(p)` | Delete the task, set `enabled = false`. |
| `blackout_all()` | Stop every scene task. |
| `restore_all()` | Restart every scene that has at least one message. |

---

## 9. Status Reporting

**File:** `osc_status.h`

The `StatusReporter` singleton sends human-readable strings to a single
configurable OSC destination (typically a laptop running a monitoring tool).

### Severity levels

| Level | Value | Label | Purpose |
|-------|-------|-------|---------|
| `STATUS_ERROR` | 0 | `ERROR` | Something failed. |
| `STATUS_WARNING` | 1 | `WARN` | Non-fatal problem. |
| `STATUS_INFO` | 2 | `INFO` | Normal confirmations. |
| `STATUS_DEBUG` | 3 | `DEBUG` | Verbose diagnostics. |

The reporter has a configurable minimum level.  Only messages at or above this
importance threshold are sent.  For example, setting the level to `ERROR` means
only errors are forwarded.

### Payload format

```
[LEVEL] category: message
```

Example: `[INFO] scene: Started scene 'mixer1'`

All status messages are also printed to Serial for debugging.

---

## 10. Command Reference

All commands are OSC messages sent to the device's UDP port.  The address
format is:

```
/annieData{device_adr}/{category}/{name}/{command}
```

Where `{device_adr}` is the name set during provisioning (e.g. `/bart`).

If `{command}` is omitted, it defaults to `"assign"`.

**Case flexibility:** All command segments accept camelCase, snake_case, and
plain lowercase.  `addMsg`, `add_msg`, and `addmsg` all normalise to the same
command.  User-defined names preserve their case.

**Payload format:** All payloads are a single string or a single number.
Commands that need two values accept a single CSV string: `"name1, name2"`.

### Message Commands

`/annieData{dev}/msg/{name}/{command}`

| Command | Payload | Description |
|---------|---------|-------------|
| *(none)* / `assign` | `"key:value, ..."` config string | Create or update a message. |
| `delete` | *(none)* | Remove the message from the registry. |
| `enable` / `unmute` | *(none)* | Enable the message for sending. |
| `disable` / `mute` | *(none)* | Disable the message (skipped during send). |
| `info` | *(none)* | Reply with the message's current parameters. |

#### Config string keys

| Key | Type | Description |
|-----|------|-------------|
| `ip` | IP address | Destination IP (e.g. `192.168.1.100`). |
| `port` | integer | Destination port (1–65535). |
| `adr` / `addr` / `address` | string | OSC address (e.g. `/mixer/fader1`). |
| `value` / `val` | sensor name | Sensor stream to send (e.g. `accelX`, `gyroZ`, `baro`). |
| `low` / `min` / `lo` | float | Output bounds low (default 0). |
| `high` / `max` / `hi` | float | Output bounds high (default 1). |
| `scene` | scene name | Assign this message to a scene. |
| `enabled` | `true`/`false` | Enable or disable the message. |
| `ori_only` | ori name | *(ab7 only)* Send only when this ori is active. |
| `ori_not` | ori name | *(ab7 only)* Send only when this ori is NOT active. |
| `ternori` | ori name | *(ab7 only)* Ignore `value_ptr`; send `bounds[1]` (high) when named ori is active, `bounds[0]` (low) when not. Saved to NVS. |

**Reference mode:** Use `-` instead of `:` to inherit a value from a named
registry object:

```
ip-mixer1, port-mixer1, value:accelX
```

This copies `ip` and `port` from the registered object named `mixer1`.

**Default/all reference:** `default-mixer1` copies all set fields from
`mixer1` as fallback values (only filling in fields not already set in this
config string).

### Scene Commands

`/annieData{dev}/scene/{name}/{command}`

| Command | Payload | Description |
|---------|---------|-------------|
| *(none)* / `assign` | config string | Create or update a scene (ip, port, adr, low, high). |
| `delete` | *(none)* | Delete the scene and stop its task. |
| `start` / `enable` / `go` | *(none)* | Create the FreeRTOS task and begin sending. |
| `stop` / `disable` / `mute` | *(none)* | Stop the send task. |
| `addMsg` / `add` | `"msg1, msg2, ..."` | Add messages to this scene (CSV names). |
| `removeMsg` / `rmMsg` | `"msgName"` | Remove a message from this scene. |
| `period` / `rate` | `"50"` (string or int) | Set the send period in milliseconds (20–60000). |
| `override` | `"field1, field2, ..."` | Set which fields the scene overrides on its messages. |
| `adrMode` / `addressMode` | mode string | Set address composition mode. |
| `setAll` | config string | Apply a config to every message in this scene. |
| `solo` | `"msgName"` | Enable one message, disable all others. |
| `unsolo` / `unmute` | *(none)* | Re-enable all messages in the scene. |
| `enableAll` | *(none)* | Enable all messages in this scene. |
| `info` | *(none)* | Reply with the scene's parameters and message list. |

#### Override field names

`"ip"`, `"port"`, `"adr"`/`"addr"`/`"address"`, `"low"`/`"min"`,
`"high"`/`"max"`, `"scale"`/`"bounds"` (sets both low and high),
`"all"`, `"none"`.

Prefix with `-` to turn off: `"-ip"`.

#### Address modes

`"fallback"` (default), `"override"`, `"prepend"`, `"append"`.

### Clone Commands

`/annieData{dev}/clone/msg` — payload: `"srcName, destName"`
`/annieData{dev}/clone/scene` — payload: `"srcName, destName"`

Creates a copy of a message or scene under a new name.  For scenes, the
message list is copied (task state is not).

### Rename Commands

`/annieData{dev}/rename/msg` — payload: `"oldName, newName"`
`/annieData{dev}/rename/scene` — payload: `"oldName, newName"`

### Move Command

`/annieData{dev}/move` — payload: `"msgName, sceneName"`

Removes the message from its current scene and adds it to the named scene.

### List Commands

`/annieData{dev}/list/msgs` — optional payload: `"verbose"` or `"v"`
`/annieData{dev}/list/scenes` — optional payload: `"verbose"` or `"v"`
`/annieData{dev}/list/all` — optional payload: `"verbose"` or `"v"`

Replies with a text listing of all registered messages and/or scenes.
When a section has no entries, it is returned as `none` (for example:
`Scenes (0): none`).

### Global Commands

| Address | Description |
|---------|-------------|
| `/annieData{dev}/blackout` | Stop all scene tasks immediately. |
| `/annieData{dev}/restore` | Restart all scenes that have messages. |
| `/annieData{dev}/dedup` | Payload `"on"`/`"off"` — enable or disable duplicate-value suppression. No payload queries the current state. |

### Status Commands

| Address | Payload | Description |
|---------|---------|-------------|
| `/annieData{dev}/status/config` | config string | Set status destination (ip, port, adr). |
| `/annieData{dev}/status/level` | level string | Set minimum importance (`error`, `warn`, `info`, `debug`). |

### Save / Load Commands

| Address | Payload | Description |
|---------|---------|-------------|
| `/annieData{dev}/save` | *(none)* | Save all scenes and messages to NVS. |
| `/annieData{dev}/save/msg` | `"msgName"` | Save one message to NVS. |
| `/annieData{dev}/save/scene` | `"sceneName"` | Save one scene to NVS. |
| `/annieData{dev}/load` | *(none)* | Load all scenes and messages from NVS. |
| `/annieData{dev}/nvs/clear` | *(none)* | Erase all saved OSC data from NVS. |

### Direct Command

| Address | Payload | Description |
|---------|---------|-------------|
| `/annieData{dev}/direct/{name}` | config string | One-step: create msg + scene, link, and start sending. |

The config string is parsed with `from_config_str()` and also accepts an
optional `period:N` key.  A message and scene are both created (or updated)
with the name `{name}`, the message is added to the scene, and the scene
task is started.  This is the fastest path to getting data flowing.

### Ori Commands (ab7 only)

These commands are only available when building with `-DAB7_BUILD`.  See
`ori_tracker.h` for implementation details.

| Address | Payload | Description |
|---------|---------|-------------|
| `/annieData{dev}/ori/save` | *(optional)* `"name"` | Save current orientation.  Auto-named (`ori_0`, `ori_1`, ...) if no name given. |
| `/annieData{dev}/ori/save/{name}` | *(none)* | Save current orientation with the given name. |
| `/annieData{dev}/ori/delete/{name}` | *(none)* | Delete a saved orientation. |
| `/annieData{dev}/ori/clear` | *(none)* | Delete all saved orientations. |
| `/annieData{dev}/ori/list` | *(none)* | List all saved orientations and which is active. |
| `/annieData{dev}/ori/threshold` | float (rad/s) | Set the motion gate gyro threshold (default 1.5 rad/s). |
| `/annieData{dev}/ori/active` | *(none)* | Query the currently active orientation. |

Button A (GPIO 0) also saves an auto-named ori when pressed.
Button B (GPIO 14) prints the active ori to serial.

### Serial Debug Commands

Type these commands into the serial monitor (115200 baud):

| Command | Description |
|---------|-------------|
| `help` | List available commands. |
| `status` | WiFi / network / device status. |
| `streams` | Current sensor data stream values. |
| `config` | Provisioned network configuration. |
| `nvs` | NVS storage summary (osc_store). |
| `registry` | OSC registry (scenes + messages). |
| `serial [level]` | Get/set serial debug level (`error`, `warn`, `info`, `debug`). |
| `sends [on\|off]` | Show or toggle per-message send logging to serial. |
| `hardware` | Hardware diagnostics (chip info, pin states). |
| `restart` | Reboot the device. |
| `provision` | Erase config & reboot into captive portal. |
| `uptime` | Time since boot. |

---

## 11. Data Flow Diagram

```
                    ┌──────────────────────┐
                    │   Bart: Sensors      │  (ISM330DHCX, MMC5983MA, BMP5xx)
                    │     or Simulation    │  update_simulated_data()
                    ├──────────────────────┤
                    │   ab7: BNO085 IMU   │  (SPI)
                    │     + OriTracker     │  quaternion geodesic matching
                    └──────────┬───────────┘
                               │ writes
                               ▼
                    ┌──────────────────────┐
                    │ data_streams[0..21]  │  volatile float[22], all in [0, 1]
                    └──────────┬───────────┘
                               │ read by
                               ▼
┌──────────────────────────────────────────────────────┐
│  Scene Send Task (FreeRTOS, one per scene)            │
│                                                       │
│  for each message:                                    │
│    val = *msg.value_ptr                     [0, 1]    │
│    [ab7] check ori_only / ori_not conditions          │
│    low, high = resolve_bounds(msg, scene)              │
│    output = low + val * (high - low)                  │
│    ip, port, adr = resolve_destination(msg, scene)    │
│    osc.setDestination(ip, port)                       │
│    osc.sendFloat(adr, output)                         │
└──────────────────────────────────┬────────────────────┘
                                   │ UDP
                                   ▼
                          ┌─────────────────┐
                          │  Network Target  │
                          │  (mixer, QLab,   │
                          │   console, etc.) │
                          └─────────────────┘

┌──────────────────┐
│ Incoming OSC     │  from network (control laptop, etc.)
│ (MicroOscUdp)    │
└───────┬──────────┘
        │ osc.onOscMessageReceived()
        ▼
┌──────────────────────────────────────────────────────┐
│ osc_handle_message()  (osc_commands.h)                │
│                                                       │
│  Parse address → category / name / command            │
│  Disscene to handler → modify registry                │
│  [ab7] Ori commands: save/delete/list/threshold       │
│  Send status reply                                    │
└──────────────────────────────────────────────────────┘
```

---

## 12. File Map

```
src/
├── main.cpp            Entry point: setup(), loop(), sensor task.
│                       Uses #ifdef AB7_BUILD for board-specific boot.
├── main.h              Include orchestrator; conditional hardware/ori includes.
├── bart_hardware.h     Bart pin constants, sensor externs, struct norm_imu_data.
├── bart_hardware.cpp   Bart sensor init (SPI, ISM330DHCX, MMC5983MA, BMP5xx).
├── ab7_hardware.h      ab7 pin constants, BNO085 IMU API.
├── ab7_hardware.cpp    ab7 BNO085 IMU driver, quaternion-to-Euler.
├── ori_tracker.h       Orientation save/recall/matching (ab7 only).
├── network_setup.h     WiFi captive-portal provisioning (uses net_pass).
├── data_streams.h      data_streams[22] array (NUM_DATA_STREAMS), index constants, simulated data.
├── osc_message.h       OscMessage class, ExistFlags, ori_only/ori_not fields.
├── osc_scene.h         OscScene class, OverrideFlags, AddressMode, period clamping.
├── osc_registry.h      OscRegistry singleton, method implementations.
├── osc_status.h        StatusReporter class, StatusLevel enum.
├── osc_engine.h        WiFiUDP/MicroOscUdp globals, send task, send logging.
├── osc_storage.h       NVS persistence: save/load scenes and messages.
├── osc_commands.h      Incoming OSC command dispatcher (including ori commands).
└── serial_commands.h   Serial monitor debug interface (help, status, sends, etc.).
```

---

## 13. Concurrency & Mutex Strategy

The firmware runs multiple FreeRTOS tasks:

| Task | Priority | Purpose |
|------|----------|---------|
| `loop()` (Arduino main) | 1 | Receive and process incoming OSC commands. |
| `sensor_task` | 1 | Read sensors (or generate simulated data) at ~100 Hz. |
| `p_{sceneName}` (one per started scene) | 1 | Send OSC messages at the scene's rate. |

> **ab7 core pinning:** On the ab7 board, the sensor task is pinned to
> **core 1** using `xTaskCreatePinnedToCore()`.  `SPI.begin()` registers the
> SPI interrupt handler on the calling core; running `getSensorEvent()` from a
> different core causes silent SPI failures and all-zero reads.  The Bart
> build uses `xTaskCreate()` without core pinning.

Two mutexes protect shared state:

1. **Registry mutex** (`reg.lock()` / `reg.unlock()`) — protects all reads
   and writes to the `OscRegistry` arrays.  Both the command handler (main
   loop) and the send tasks take this mutex.

2. **Send mutex** (`osc_send_mutex()`) — serialises all `osc.sendFloat()` and
   `osc.sendString()` calls.  The MicroOscUdp instance is not thread-safe, so
   only one task may write to the UDP socket at a time.

The `data_streams[]` array is written by the sensor task and read by the send
tasks.  These are simple float writes (atomic on ARM Cortex-M / Xtensa), so
no mutex is needed — a send task might read a value that is one cycle stale,
which is acceptable for sensor data.

---

## 14. Memory Model

All objects live in fixed-size arrays in the BSS segment.  No heap allocation
(`new` / `malloc`) is used for registry storage.

Approximate memory footprint:

| Array | Size | Rough bytes |
|-------|------|-------------|
| `scenes[64]` | 64 × ~500 bytes (includes `msg_indices[64]` array) | ~32 KB |
| `messages[256]` | 256 × ~180 bytes (includes String fields, ori fields) | ~46 KB |
| `data_streams[22]` | 22 × 4 bytes | 88 bytes |

The ESP32-S3 has ~512 KB of SRAM, of which ~300 KB is available after WiFi and
BLE stacks.  The registry uses about 78 KB, leaving plenty of room.

To change capacity, modify `MAX_OSC_PATCHES` and `MAX_OSC_MESSAGES` in
`osc_message.h` and recompile.

---

## 15. Extending the Firmware

### Adding a new sensor stream

1. Increase `NUM_DATA_STREAMS` in `data_streams.h`.
2. Add a new `#define` constant for the index.
3. Update `data_stream_name()` and `data_stream_index_from_name()`.
4. In the sensor task (`main.cpp`), write to `data_streams[NEW_INDEX]`.
5. If using simulated data, add a sine wave in `update_simulated_data()`.

### Adding a new command

1. In `osc_commands.h`, add a new `else if` clause in the appropriate command
   section (message commands or scene commands).
2. Read arguments with `osc_msg.nextAsString()` or `osc_msg.nextAsInt()`.
3. Modify the registry under `reg.lock()` / `reg.unlock()`.
4. Send a status message with `status_reporter().info(...)`.
5. Document the command in this guide and in the user guide.

### Adding a new override field

1. Add the field to `OverrideFlags` in `osc_scene.h`.
2. Add a `resolve_*()` function in `osc_engine.h`.
3. Use the resolved value in `scene_send_task()`.
4. Add parsing in the `override` command handler in `osc_commands.h`.
5. Update `to_info_string()` in `osc_registry.h`.
