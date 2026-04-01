# TheaterGWD Engineering Guide

This document is for **computer engineers** building, extending, or debugging the TheaterGWD firmware and its companion software. It covers the full system architecture from hardware to application layer.

---

## Table of Contents

- [System Overview](#system-overview)
- [Repository Layout](#repository-layout)
- [Build System](#build-system)
- [Hardware Layer](#hardware-layer)
- [Data Streams](#data-streams)
- [Core Object Model](#core-object-model)
- [Send Engine & FreeRTOS](#send-engine--freertos)
- [Network & Provisioning](#network--provisioning)
- [OSC Command Dispatcher](#osc-command-dispatcher)
- [Orientation System (ab7 only)](#orientation-system-ab7-only)
- [Show System](#show-system)
- [Status Reporter](#status-reporter)
- [Memory Model](#memory-model)
- [Concurrency & Mutex Strategy](#concurrency--mutex-strategy)
- [annieData Control Center Backend](#anniedata-control-center-backend)
- [Extending the Firmware](#extending-the-firmware)

---

## System Overview

TheaterGWD is a wireless motion-sensor-to-OSC translation system for live theater. It consists of:

1. **Firmware** running on ESP32-S3 boards (two variants: **Bart** and **ab7**)
2. **annieData Control Center** — a Python/Flask web GUI for configuring the device over OSC
3. **OSC protocol** — the transport layer between device, control center, and show-control software (lighting consoles, QLab, TouchDesigner, etc.)

The device reads IMU, barometric, and quaternion data from onboard sensors, normalizes all values to `[0, 1]`, and sends them as OSC float messages over WiFi UDP at configurable rates. Users define **messages** (individual sensor-to-OSC mappings) and **scenes** (groups of messages with a shared send task), all managed through OSC commands or the annieData GUI.

---

## Repository Layout

### Firmware (`src/` and `include/`)

| File | Purpose |
|------|---------|
| `src/main.cpp` | Entry point: boot sequence, FreeRTOS task creation, main loop |
| `include/osc_message.h` | `OscMessage` struct, `ExistFlags`, config parsing |
| `include/osc_scene.h` | `OscScene` struct, `AddressMode`, `OverrideFlags` |
| `include/osc_registry.h` | Meyer's singleton registry, find/create/update/delete |
| `include/osc_commands.h` | All 50+ incoming OSC command handlers |
| `include/osc_engine.h` | FreeRTOS send tasks, address/bounds resolution, UDP transport |
| `include/osc_status.h` | Status reporting (error/warn/info/debug) |
| `include/osc_storage.h` | NVS persistence — messages, scenes, oris, shows |
| `include/osc_pattern.h` | OSC 1.0 wildcard matching (`*`, `?`, `[]`, `{}`) |
| `include/data_streams.h` | 22 sensor data streams, names, indices, simulated data |
| `include/ori_tracker.h` | Orientation recording, matching, cloud subsampling |
| `include/network_setup.h` | WiFi provisioning, captive portal, NVS device config |
| `include/euler_utils.h` | Euler angle decomposition (ZYX, ZXY) |
| `include/serial_commands.h` | Serial debug/command interface |
| `include/ab7_hardware.h` | ab7 board drivers (BNO085, LEDs, buttons) |
| `include/bart_hardware.h` | Bart board drivers (ISM330DHCX/LSM6DSV16X, BMP5xx, NeoPixel) |
| `include/micro_osc_udp.h` | MicroOSC UDP transport wrapper |
| `include/main.h` | Include orchestrator |

### annieData Control Center (`gooey/`)

| File | Purpose |
|------|---------|
| `gooey/app/main.py` | Flask app, ~115 routes, SocketIO events |
| `gooey/app/osc_handler.py` | OSCEngine — send, receive, bridge, log |
| `gooey/app/script_runner.py` | Python scripting sandbox with device proxies |
| `gooey/app/templates/index.html` | Main web UI |
| `gooey/app/templates/remote.html` | Mobile remote PWA |
| `gooey/app/static/js/app.js` | Main UI logic (~3900 lines) |
| `gooey/app/static/js/remote.js` | Mobile remote logic |
| `gooey/run.py` | CLI entry point |
| `gooey/pyproject.toml` | Package metadata (gooey-theatergwd) |

### Build & Config

| File | Purpose |
|------|---------|
| `platformio.ini` | PlatformIO build environments and dependencies |
| `gooey/install.sh` | annieData installer (venv + requirements) |
| `gooey/requirements.txt` | Python dependencies |
| `gooey/package.json` | Tauri desktop bundler config |

---

## Build System

### PlatformIO Environments

| Environment | Board | Description |
|-------------|-------|-------------|
| `bart` | ESP32-S3 | Default — ISM330DHCX/LSM6DSV16X IMU, BMP5xx barometer, NeoPixel |
| `ab7` | ESP32-S3 | BNO085 IMU, SK6812 LED, two buttons, orientation system |
| `ab7_test` | ESP32-S3 | Unit tests for ab7 |

### Build Commands

```bash
# Build default (Bart)
pio run

# Build specific environment
pio run -e bart
pio run -e ab7

# Upload to connected board
pio run -t upload
pio run -e ab7 -t upload
```

### Conditional Compilation

The ab7 build defines `-DAB7_BUILD`. Use this to guard ab7-specific code:

```cpp
#ifdef AB7_BUILD
    // BNO085, orientation, buttons
#else
    // Bart: barometer, simulated data
#endif
```

### Shared Dependencies

- `FastLED` — LED control
- `MicroOSC` — OSC protocol (GitHub fork)
- `WiFiProvisioner` — captive portal provisioning (GitHub fork)

### Bart-Specific Dependencies

- `Adafruit BMP5xx Library` — barometer
- `slime_swipe` — sensor fusion IMU (ISM330DHCX/LSM6DSV16X via SPI)

### ab7-Specific Dependencies

- `slime_swipe` — BNO085 sensor fusion (SPI)

---

## Hardware Layer

### Bart Board

| Component | Chip | Interface | Pin(s) |
|-----------|------|-----------|--------|
| IMU | ISM330DHCX / LSM6DSV16X | SPI | CS=42, SCK=36, MOSI=35, MISO=37 |
| Magnetometer | MMC5983MA | SPI (shared) | — |
| Barometer | BMP5xx | I2C | — |
| Status LED | NeoPixel | GPIO | WS2812 data pin |

**Initialization:** `setup()` in `main.cpp` → `init_bart_hardware()` → SPI bus, sensor objects, calibration. The `sensor_task` FreeRTOS task reads IMU at ~100 Hz and calls `process_imu_data()` to fill `data_streams[]`.

**Simulated data:** When no real sensor is connected (development), `update_simulated_data()` fills all streams with sine waves at distinct frequencies, each oscillating in `[0, 1]`.

### ab7 Board

| Component | Chip | Interface | Pin(s) |
|-----------|------|-----------|--------|
| IMU | BNO085 | SPI | CS=10, SCK=12, MOSI=11, MISO=13, INT=4, RST=5, WAK=6 |
| Status LED | SK6812 | GPIO | 7 (1 pixel) |
| Button A | Tactile | GPIO | 0 (ori record/save) |
| Button B | Tactile | GPIO | 14 (ori cycling) |

**LED color states:**
- Dim purple — booting
- Green — connected to WiFi
- Orange — provisioning mode (soft-AP)
- Ori color — when an orientation is active

> **BNO085 gotcha:** Do NOT pass the CS pin to `SPI.begin()`. The BNO085 driver manages CS internally. Passing it to `SPI.begin()` causes initialization failures.

**ab7 has no barometer** — the `baro` data stream is forced to `1.0`.

---

## Data Streams

All sensor values are normalized to `[0, 1]` and stored in a global array:

```cpp
volatile float data_streams[NUM_DATA_STREAMS]; // 22 elements
```

| Index | Constant | Name | Description |
|-------|----------|------|-------------|
| 0 | `ACCELX` | accelX | Linear acceleration — X axis |
| 1 | `ACCELY` | accelY | Linear acceleration — Y axis |
| 2 | `ACCELZ` | accelZ | Linear acceleration — Z axis |
| 3 | `ACCELLENGTH` | accelLength | Acceleration magnitude |
| 4 | `GYROX` | gyroX | Rotation rate — X axis |
| 5 | `GYROY` | gyroY | Rotation rate — Y axis |
| 6 | `GYROZ` | gyroZ | Rotation rate — Z axis |
| 7 | `GYROLENGTH` | gyroLength | Rotation rate magnitude |
| 8 | `BARO` | baro | Barometric pressure / altitude |
| 9 | `EULERX` | eulerX | Roll (Euler angle) |
| 10 | `EULERY` | eulerY | Pitch (Euler angle) |
| 11 | `EULERZ` | eulerZ | Yaw (Euler angle) |
| 12 | `GACCELX` | gaccelX | Global-frame acceleration — X |
| 13 | `GACCELY` | gaccelY | Global-frame acceleration — Y |
| 14 | `GACCELZ` | gaccelZ | Global-frame acceleration — Z |
| 15 | `GACCELLENGTH` | gaccelLength | Global-frame acceleration magnitude |
| 16 | `CONST_ZERO` | low | Constant `0.0` |
| 17 | `CONST_ONE` | high | Constant `1.0` |
| 18 | `QUAT_I` | quatI | Quaternion I (mapped to `[0,1]` via `*0.5+0.5`) |
| 19 | `QUAT_J` | quatJ | Quaternion J |
| 20 | `QUAT_K` | quatK | Quaternion K |
| 21 | `QUAT_R` | quatR | Quaternion R (real/scalar) |

**Helper functions:**
- `data_stream_name(idx)` — index → name string
- `data_stream_index_from_name(name)` — name → index (case-insensitive)
- `data_stream_index_from_ptr(ptr)` — pointer → index

---

## Core Object Model

### OscMessage

An individual sensor-to-OSC mapping.

```cpp
struct ExistFlags {
    bool name, ip, port, adr, val, low, high;
};

class OscMessage {
    ExistFlags exist;
    String name;
    IPAddress ip;
    unsigned int port;
    String osc_address;
    OscScene* scenes[MAX_SCENES_PER_MSG]; // up to 8 parent scenes
    volatile float* value_ptr;            // pointer into data_streams[]
    float bounds[2];                      // output scaling [low, high], default [0, 1]
    bool enabled;
    // Ori conditionals (ab7 only)
    String ori_only;   // only send when this ori is active
    String ori_not;    // suppress when this ori is active
    String ternori;    // send 1.0 when ori matches, 0.0 otherwise
    // Dedup cache
    float _last_sent_val;
    bool _has_last_sent;
};
```

**Key methods:**
- `from_config_str(str)` — parse `"key:value, key:value, ..."` format
- `sendable()` — returns true if the message has enough info to send (value + destination + address)
- `to_info_string()` — human-readable summary for replies
- `add_scene() / remove_scene() / has_scene() / clear_scenes()`

**Config string format:**
```
"name:myMsg, ip:192.168.1.50, port:9000, adr:/sensor/x, value:accelX, low:0, high:255, enabled:true, scene:myScene, ori_only:forward, ori_not:backward, ternori:upright"
```

**Reference mode:** Setting `ip:ip-anotherMessage` copies the IP address from the named message at send time.

### OscScene

A group of messages sharing a FreeRTOS send task.

```cpp
enum AddressMode {
    ADR_FALLBACK = 0,  // message address, scene as fallback
    ADR_OVERRIDE = 1,  // scene address replaces message address
    ADR_PREPEND  = 2,  // scene.adr + msg.adr
    ADR_APPEND   = 3   // msg.adr + scene.adr
};

struct OverrideFlags {
    bool ip, port, adr, low, high;
};

class OscScene {
    ExistFlags exist;
    String name;
    IPAddress ip;
    unsigned int port;
    String osc_address;
    float bounds[2];
    AddressMode address_mode;
    unsigned int send_period_ms; // default 50, range [20, 60000]
    bool enabled;
    TaskHandle_t task_handle;    // FreeRTOS task
    OverrideFlags overrides;
    int msg_indices[MAX_MSGS_PER_PATCH]; // up to 64
    uint8_t msg_count;
};
```

### OscRegistry

Singleton managing all scenes and messages. Thread-safe via `reg_mutex`.

```cpp
class OscRegistry {
    OscScene   scenes[MAX_OSC_SCENES];    // 64 slots
    uint16_t   scene_count;
    OscMessage messages[MAX_OSC_MESSAGES]; // 256 slots
    uint16_t   msg_count;
    SemaphoreHandle_t reg_mutex;
};

// Meyer's singleton accessor
OscRegistry& osc_registry();
```

**Key methods:**
- `find_scene(name) / find_msg(name)` — case-insensitive lookup, returns pointer or `nullptr`
- `find_msgs_matching(pattern, out[], max) / find_scenes_matching(...)` — OSC 1.0 wildcard matching
- `get_or_create_scene(name) / get_or_create_msg(name)` — find existing or allocate new slot
- `update_scene(src) / update_msg(src)` — merge fields from source into existing (or create)
- `delete_scene(name) / delete_msg(name)` — remove and swap-shrink array
- `lock() / unlock()` — mutex management

**Constants:**
- `MAX_OSC_SCENES = 64`
- `MAX_OSC_MESSAGES = 256`
- `MAX_SCENES_PER_MSG = 8`
- `MAX_MSGS_PER_PATCH = 64`

---

## Send Engine & FreeRTOS

### Task Architecture

| Task | Priority | Stack | Purpose |
|------|----------|-------|---------|
| `loop()` | — | main | Polls UDP for incoming OSC commands |
| `sensor_task` | 1 | 8192 | Reads hardware sensors, fills `data_streams[]` |
| Per-scene send task | 1 | 4096 | One task per active scene; sends messages at `send_period_ms` |

### Scene Send Task (`scene_send_task`)

Each running scene spawns a FreeRTOS task that:

1. Sleeps for `send_period_ms`
2. Skips iteration if scene is disabled
3. Resets dedup caches on ori change
4. Locks `reg_mutex`
5. For each message in the scene:
   - Skips if message is disabled
   - Checks ori conditionals (`ori_only`, `ori_not`)
   - Resolves effective IP, port, address, bounds (override → message → scene fallback)
   - Reads value from `data_streams[]` (or computes ternori binary)
   - Applies dedup filter (skip if value unchanged)
   - Locks `send_mutex`
   - Sends float via `MicroOscUdp`
   - Unlocks `send_mutex`
6. Unlocks `reg_mutex`, loops

### Address Resolution

For each field (IP, port, address, bounds), resolution follows this priority:

1. If scene **overrides** the field AND scene has a value → use scene's value
2. Else if message has a value → use message's value
3. Else if scene has a value → use scene's value (fallback)
4. Else → empty/zero (message is not sendable)

**Address mode composition:**

| Mode | Result |
|------|--------|
| `ADR_FALLBACK` | Message address; scene address if message has none |
| `ADR_OVERRIDE` | Scene address replaces message address |
| `ADR_PREPEND` | `scene.adr + msg.adr` (e.g., `/mixer` + `/fader1` → `/mixer/fader1`) |
| `ADR_APPEND` | `msg.adr + scene.adr` |

Handles trailing/leading slashes and empty sides gracefully.

### UDP Transport

```cpp
WiFiUDP Udp;                    // shared UDP socket
MicroOscUdp<1024> osc(&Udp);   // 1024-byte receive buffer
```

`osc_send_mutex()` serializes all UDP writes to prevent interleaved packets across tasks.

### Dedup System

When enabled, each message caches its last sent value. If the new value equals the cached value, the send is skipped. Dedup caches are cleared on ori transitions to ensure all messages re-send.

---

## Network & Provisioning

### First-Boot Flow

1. Device checks NVS key `provisioned` in namespace `"device_config"`
2. If not provisioned → launches soft-AP named **"annieData Setup"**
3. Captive portal serves a web form with fields:
   - WiFi SSID and password
   - Static IP address (or `"dhcp"`)
   - UDP port (default 8000)
   - Device name / OSC address prefix
4. On submit → validates inputs, stores to NVS, reboots
5. On next boot → reads NVS, connects to WiFi, starts UDP listener

### NVS Keys (namespace `"device_config"`)

| Key | Type | Description |
|-----|------|-------------|
| `provisioned` | bool | Whether device has been set up |
| `ssid` | string | WiFi network name |
| `net_pass` | string | WiFi password |
| `use_dhcp` | bool | Use DHCP or static IP |
| `static_ip` | string | Static IP (if not DHCP) |
| `port` | int | UDP listen port |
| `device_adr` | string | Device name / OSC address prefix |

### Captive Portal Config

- AP name: `"annieData Setup"`
- Page title: `"annieData Device Setup"`
- Theme color: `#E4CBFF` (purple)
- IP validation: dotted-quad or `"dhcp"`
- Port validation: 1–65535
- Device name: alphanumeric + special characters, max 32 chars

---

## OSC Command Dispatcher

All incoming OSC messages are matched against the device address prefix:

```
/annieData{device_adr}/{command}
```

### Command Normalization

Commands accept **camelCase**, **snake_case**, and **lowercase** interchangeably:
- `addMsg`, `add_msg`, `addmsg` all match the same handler

User-defined names (messages, scenes, oris) **preserve case** as given.

### Complete Command Reference

#### Message Commands — `/msg/{name}/...`

| Command | Payload | Description |
|---------|---------|-------------|
| *(none)* / `assign` | config string | Create or update message |
| `delete` | — | Remove message |
| `enable` | — | Enable sending |
| `disable` / `mute` | — | Disable sending |
| `info` | — | Reply with message details |

#### Scene Commands — `/scene/{name}/...`

| Command | Payload | Description |
|---------|---------|-------------|
| *(none)* / `assign` | config string | Create or update scene |
| `delete` | — | Remove scene and its task |
| `start` | — | Create FreeRTOS task, begin sending |
| `stop` | — | Delete task, stop sending |
| `enable` | — | Restart task |
| `disable` / `mute` | — | Stop task without deleting |
| `addMsg` | `"msg1, msg2, ..."` | Add messages to scene |
| `removeMsg` | `"msgName"` | Remove message from scene |
| `period` | int (ms) | Set send interval (20–60000) |
| `override` | `"ip+port+adr+low+high"` | Set override flags |
| `adrMode` | `"fallback\|override\|prepend\|append"` | Set address composition mode |
| `setAll` | config string | Set property on all messages in scene |
| `solo` | `"msgName"` | Enable one message, disable all others |
| `unsolo` | — | Re-enable all messages |
| `enableAll` | — | Enable all messages |
| `info` | — | Reply with scene details |

#### Direct Command — `/direct/{name}`

One-step: creates message + scene, adds message to scene, starts sending.

```
Payload: "value:accelX, ip:192.168.1.50, port:9000, adr:/sensor/x, period:50"
```

#### Clone Commands — `/clone/...`

| Command | Payload | Description |
|---------|---------|-------------|
| `msg` | `"srcName, destName"` | Duplicate message |
| `scene` | `"srcName, destName"` | Duplicate scene and its messages |

#### Rename Commands — `/rename/...`

| Command | Payload | Description |
|---------|---------|-------------|
| `msg` | `"oldName, newName"` | Rename message |
| `scene` | `"oldName, newName"` | Rename scene |

#### Move Command — `/move`

| Payload | Description |
|---------|-------------|
| `"msgName, sceneName"` | Move message to scene |

#### List Commands — `/list/...`

| Command | Payload | Description |
|---------|---------|-------------|
| `msgs` | `[verbose]` | List all messages |
| `scenes` | `[verbose]` | List all scenes |
| `all` | `[verbose]` | List everything |

#### Global Commands

| Command | Payload | Description |
|---------|---------|-------------|
| `/blackout` | — | Stop all scene tasks immediately |
| `/restore` | — | Restart all scenes |
| `/dedup` | `"on"` / `"off"` | Toggle duplicate suppression |
| `/tare` | — | Capture current orientation as zero reference |
| `/tare/reset` | — | Clear tare |
| `/tare/status` | — | Query tare state |
| `/flush` | — | Reply "OK" once all preceding commands are processed |

#### Save/Load Commands

| Command | Payload | Description |
|---------|---------|-------------|
| `/save` / `/save/all` | — | Save all scenes + messages to NVS |
| `/save/msg` | `"name"` | Save one message |
| `/save/scene` | `"name"` | Save one scene |
| `/load` / `/load/all` | — | Load all from NVS |
| `/nvs/clear` | — | Erase all OSC data from NVS |

#### Show Commands — `/show/...`

| Command | Payload | Description |
|---------|---------|-------------|
| `/show/save/{name}` | — | Snapshot current state as named show |
| `/show/load/{name}` | — | Stage pending load (requires confirmation) |
| `/show/load/confirm` | — | Execute pending load |
| `/show/list` | — | Reply CSV of show names |
| `/show/delete/{name}` | — | Delete show |
| `/show/rename` | `"oldName, newName"` | Rename show |

#### Status Commands — `/status/...`

| Command | Payload | Description |
|---------|---------|-------------|
| `config` | `"ip:x.x.x.x, port:N, adr:/status"` | Set status destination |
| `level` | `"error\|warn\|info\|debug"` | Set minimum report level |

#### Orientation Commands — `/ori/...` (ab7 only)

| Command | Payload | Description |
|---------|---------|-------------|
| `/ori/register/{name}` | `[r,g,b]` | Pre-register ori slot with optional LED color |
| `/ori/save/{name}` | — | Instant single-sample save |
| `/ori/delete/{name}` | — | Remove ori |
| `/ori/clear` | — | Remove all oris |
| `/ori/list` | — | Reply CSV of ori names |
| `/ori/threshold` | `[float]` | Set motion gate (rad/s) |
| `/ori/tolerance` | `[degrees]` | Set match tolerance |
| `/ori/strict` | `"on"` / `"off"` | Toggle strict matching |
| `/ori/active` | — | Query currently active ori |
| `/ori/reset/{name}` | — | Clear samples for re-recording |
| `/ori/info/{name}` | — | Show ori details |
| `/ori/color/{name}` | `"r,g,b"` | Set LED color |
| `/ori/select/{name}` | — | Select ori for button editing |
| `/ori/record/start/{name}` | — | Begin timed recording session |
| `/ori/record/stop` | — | Finalize recording |
| `/ori/record/cancel` | — | Discard recording |
| `/ori/record/status` | — | Query session state |

### Reply Format

Replies are sent back to the sender's IP and port at:
```
/reply{device_adr}/{category}/{name}
```

---

## Orientation System (ab7 only)

The orientation system allows the device to recognize physical poses and conditionally send messages based on the device's current orientation.

### Data Structures

```cpp
struct SavedOri {
    String name;
    bool used;
    float qi[MAX_CLOUD_SAMPLES], qj[], qk[], qr[]; // quaternion cloud
    uint8_t sample_count;     // 0 = pre-registered, >0 = active
    bool use_axis;            // true = axis-aware matching
    float axis_x, axis_y, axis_z;  // local unit vector
    float tolerance;          // per-ori degrees (default 10.0)
    uint8_t color_r, color_g, color_b;  // LED color
};

struct RecordingSession {
    bool active;
    String name;
    uint16_t count;
    unsigned long start_ms;
    float qi_buf[300], qj_buf[300], qk_buf[300], qr_buf[300]; // ~6 sec @ 50Hz
};
```

### Constants

- `MAX_ORIS = 32`
- `MAX_CLOUD_SAMPLES = 8` — final storage after farthest-first subsampling
- `RECORDING_BUFFER = 300` — transient buffer for recording (~6 seconds at 50 Hz)
- `AUTO_AXIS_THRESHOLD = 0.015f` rad² — variance threshold for auto-axis detection

### Recording Flow

1. `start_recording(name)` — begin accumulating quaternion samples
2. Device pushes samples via `push_sample()` each sensor tick
3. `stop_recording()` — finalize:
   - Run auto-axis detection (test variance along ±X/Y/Z; if one axis has low variance, lock to it)
   - Farthest-first subsampling: reduce buffer to `MAX_CLOUD_SAMPLES` points maximizing spread
   - Store in `SavedOri`

### Matching Algorithm

Each sensor tick, the tracker compares the current quaternion against all saved oris:

1. If `use_axis`: compute angle between world-space axis projections (ignores wrist roll)
2. Else: compute full geodesic quaternion distance (`quat_angle_between()`)
3. Score = `min_distance - tolerance` (lower is better)
4. The ori with the lowest score wins
5. If `strict_matching` is on and the winning score > 0, no ori is active

Only one ori can be active at a time. Ori changes trigger dedup cache clears.

### Button Workflow (ab7)

- **Button A — short tap** (< 300ms): instant single-sample save to the selected ori slot
- **Button A — hold** (> 300ms): start/stop timed recording session
- **Button B**: cycle through oris for selection

### Auto-Assigned LED Colors (12-color palette)

Red, Green, Blue, Yellow, Magenta, Cyan, Orange, Purple, Spring Green, Rose, Chartreuse, Sky Blue

### Ori Conditionals on Messages

| Field | Behavior |
|-------|----------|
| `ori_only:name` | Message only sends when the named ori is active |
| `ori_not:name` | Message is suppressed when the named ori is active |
| `ternori:name` | Sends `1.0` when ori matches, `0.0` otherwise (ignores sensor value) |

---

## Show System

Shows are named snapshots of the entire device state (scenes, messages, oris).

### NVS Storage Layout

| Namespace | Key(s) | Purpose |
|-----------|--------|---------|
| `"osc_store"` | `s_count`, `m_count`, `s_0`..`s_N`, `m_0`..`m_N` | Live workspace |
| `"ori_store"` | `o_count`, `o_0`..`o_N`, `o_thresh`, `o_tol`, `o_strict` | Orientations + settings |
| `"shows_idx"` | `count`, `name_0`..`name_15` | Show name index |
| `"sw_0"`.."sw_15"` | `s_count`, `m_count`, `o_count`, etc. | Per-show combined data |

### Serialization Formats

**Message:**
```
"name:xxx, ip:x.x.x.x, port:N, adr:/xxx, value:sensorName, low:0.0000, high:1.0000, enabled:true, scene:sceneName, ori_only:oriName, ori_not:oriName, ternori:oriName"
```

**Scene:**
```
"name:xxx, ip:x.x.x.x, port:N, adr:/xxx, low:0, high:1, period:50, adrmode:prepend, override:ip+port+adr+low+high, msgs:msg1+msg2+msg3"
```

**Ori (v2):**
```
"name:xxx,sc:N,r:R,g:G,b:B,tol:T,axis:A,ax:X,ay:Y,az:Z,q0i:F,q0j:F,q0k:F,q0r:F,q1i:F,..."
```

Backward-compatible with v1 format (single sample with `qi:`, `qj:`, `qk:`, `qr:`).

### Limits

- **On-device:** 16 shows max (`MAX_SHOWS = 16`)
- **annieData:** unlimited (JSON files on disk at `gooey/data/shows/`)

### Two-Step Load Safety

To prevent accidental state loss:
1. `/show/load/{name}` — stages the load, replies with confirmation prompt
2. `/show/load/confirm` — executes the load

---

## Status Reporter

Singleton for sending diagnostic messages to a remote listener.

```cpp
enum StatusLevel {
    STATUS_ERROR   = 0,  // critical failures
    STATUS_WARNING = 1,  // non-fatal issues
    STATUS_INFO    = 2,  // normal progress
    STATUS_DEBUG   = 3   // verbose details
};
```

### Configuration

```cpp
StatusReporter& status_reporter();  // singleton accessor

// Set destination
status_reporter().configure(ip, port, "/status");
status_reporter().set_level(STATUS_INFO);         // OSC threshold
status_reporter().set_serial_level(STATUS_DEBUG);  // Serial threshold
```

### Payload Format

```
"[LEVEL] category: message"
```

Example: `"[INFO] scene: myScene started with 3 messages"`

Messages are sent to the configured destination over OSC and echoed to Serial (at the serial level threshold). Thread-safe — can be called from any task.

---

## Memory Model

The firmware uses **no heap allocation** after startup. All data lives in fixed BSS arrays.

### Size Breakdown

| Array | Element Size | Count | Total |
|-------|-------------|-------|-------|
| `scenes[64]` | ~512 B | 64 | ~32 KB |
| `messages[256]` | ~180 B | 256 | ~46 KB |
| `oris[32]` | ~300 B | 32 | ~10 KB |
| `data_streams[22]` | 4 B | 22 | 88 B |

To expand capacity, change `MAX_OSC_SCENES`, `MAX_OSC_MESSAGES`, or `MAX_ORIS` in their respective headers. Ensure the ESP32-S3's available RAM can accommodate the increase.

---

## Concurrency & Mutex Strategy

| Mutex | Scope | Protects |
|-------|-------|----------|
| `reg_mutex` | `OscRegistry` | All reads/writes to scenes[], messages[], counts |
| `send_mutex` | `osc_engine.h` | UDP socket writes (serializes packets across tasks) |
| `ori_mutex` | `OriTracker` | Orientation data, recording sessions, active ori |

### Rules

1. **Never hold two mutexes simultaneously** — prevents deadlock
2. **Lock `reg_mutex` before iterating** scenes/messages
3. **Lock `send_mutex` only for the duration of the UDP write** — minimizes contention
4. **Ori reads in send task use `volatile`** for the active index — avoids locking ori_mutex in the hot path

---

## annieData Control Center Backend

### Architecture

The annieData Control Center is a **Flask + Flask-SocketIO** application serving a browser-based GUI.

```
Browser (index.html + app.js)
    ↕ HTTP REST + WebSocket (SocketIO)
Flask (main.py)
    ↕ python-osc UDP
OSCEngine (osc_handler.py)
    ↕ UDP
TheaterGWD Device
```

### Key API Routes

| Method | Path | Purpose |
|--------|------|---------|
| `POST` | `/api/send` | Send single OSC message |
| `POST` | `/api/send/json` | Batch send from JSON array |
| `POST` | `/api/send/repeat` | Start repeated send at interval |
| `POST` | `/api/recv/start` | Start OSC listener |
| `POST` | `/api/bridge/start` | Start OSC bridge (relay) |
| `GET` | `/api/devices` | List tracked devices |
| `GET/POST` | `/api/devices/{id}/messages` | Per-device message registry |
| `GET/POST` | `/api/devices/{id}/scenes` | Per-device scene registry |
| `GET/POST` | `/api/shows/{name}` | Show snapshot CRUD |
| `GET/POST` | `/api/scripts/{name}` | Python script CRUD |
| `GET` | `/api/remote-qr` | SVG QR code for mobile remote |
| `GET` | `/api/my-ip` | Server's local IP |

### SocketIO Events

| Event | Direction | Purpose |
|-------|-----------|---------|
| `osc_message` | server → client | Live OSC message logged |
| `serial_data` | server → client | Serial port data |
| `serial_connect` | client → server | Open serial port |
| `serial_send` | client → server | Send data over serial |
| `script_run` | client → server | Execute Python script |
| `script_output` | server → client | Script console output |
| `remote_configure` | client → server | Set up mobile remote session |
| `remote_reply` | server → client | OSC reply for remote clients |

### Script Runner

The script sandbox exposes:
- `osc_send(host, port, address, *args)` — direct OSC send
- `sensor(name)` / `sensors()` — latest sensor values
- `on_osc(pattern, callback)` — register callback for incoming OSC
- `device` proxy — high-level device control (`device.msg("x").enable()`)
- `MsgProxy` / `SceneProxy` — fluent API for message/scene manipulation
- Math utilities: `clamp()`, `remap()`, `elapsed()`, `dt()`, `state` dict

---

## Extending the Firmware

### Adding a New Sensor Stream

1. Add a new index constant in `data_streams.h` (increment `NUM_DATA_STREAMS`)
2. Add the name string to `data_stream_name()`
3. Add the reverse mapping in `data_stream_index_from_name()`
4. Write the normalized `[0, 1]` value in the sensor task
5. If Bart: add a simulated version in `update_simulated_data()`

### Adding a New OSC Command

1. Add a handler function in `osc_commands.h`
2. Register it in the command dispatch table (the `if/else` chain matching normalized command names)
3. If the command takes a config string, reuse `from_config_str()` patterns
4. Send a reply via `status_reporter()` or direct UDP reply

### Adding a New Override Field

1. Add a `bool` to `OverrideFlags` in `osc_scene.h`
2. Handle it in the override parsing logic in `osc_commands.h` (the `override` command)
3. Add resolution logic in the send task's field resolution chain in `osc_engine.h`
