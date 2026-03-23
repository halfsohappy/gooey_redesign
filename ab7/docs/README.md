# ab7 — TheaterGWD Firmware for the ab7 PCB

## Overview

The **ab7** is a PlatformIO project targeting a custom PCB built around the
ESP32-S3 and a **BNO-085** smart IMU.  It runs the full TheaterGWD firmware
(OSC messaging, patches, provisioning, status reporting) while reading
**real sensor data** from the BNO-085 instead of simulated sine waves.

### Hardware

| Component | Chip / Part | Interface | GPIO Pins |
|-----------|-------------|-----------|-----------|
| IMU | BNO-085 | SPI | CS=10, MOSI=11, SCK=12, MISO=13, INT=4, RST=5, WAKE=6 |
| Status LED | SK6812 | Single-wire | 7 |
| Button A | Momentary (GND) | Digital | 0 (active-low, internal pull-up) |
| Button B | Momentary (GND) | Digital | 14 (active-low, internal pull-up) |

There is **no barometer** on the ab7 board.  The `baro` data stream always
returns `0.0`.

### Data Streams

All data streams are normalised to `[0, 1]` as on the Bart board:

| Index | Name | Source | Raw Range → [0, 1] |
|-------|------|--------|---------------------|
| 0 | `accelX` | BNO-085 linear accel X | ±4 m/s² → [0, 1] |
| 1 | `accelY` | BNO-085 linear accel Y | ±4 m/s² → [0, 1] |
| 2 | `accelZ` | BNO-085 linear accel Z | ±4 m/s² → [0, 1] |
| 3 | `accelLength` | magnitude(X,Y,Z) | 0–4 m/s² → [0, 1] |
| 4 | `gyroX` | BNO-085 gyroscope X | ±4 rad/s → [0, 1] |
| 5 | `gyroY` | BNO-085 gyroscope Y | ±4 rad/s → [0, 1] |
| 6 | `gyroZ` | BNO-085 gyroscope Z | ±4 rad/s → [0, 1] |
| 7 | `gyroLength` | magnitude(X,Y,Z) | 0–4 rad/s → [0, 1] |
| 8 | `baro` | (not present) | always 0 |
| 9 | `eulerX` | roll (from rotation vector) | [-180°, 180°] → [0, 1] |
| 10 | `eulerY` | pitch (from rotation vector) | [-90°, 90°] → [0, 1] |
| 11 | `eulerZ` | yaw (from rotation vector) | [-180°, 180°] → [0, 1] |

### Building

```bash
cd ab7
pio run                  # build
pio run -t upload        # flash to device
pio device monitor       # serial monitor (115200 baud)
```

---

## Orientation Tracking ("Ori" System)

The ori system is an **ab7-exclusive feature** that lets users save named
orientations and continuously track which saved orientation the device is
closest to.  This enables gesture-based control — for example, an actor can
point a handheld ab7 device at different stage lights and automatically send
"on" messages to the light they are pointing at.

### Concepts

- **Ori**: A saved orientation, stored as a unit quaternion captured from the
  BNO-085's rotation vector.  Each ori has a unique name (e.g. `light1`,
  `spot_center`, `ori_0`).

- **Active ori**: At any moment, exactly one ori is the "active" match — the
  saved ori whose quaternion is closest to the device's current orientation.
  The match is determined by the geodesic angle (shortest rotation) between
  quaternions.

- **Motion gate**: If the device is rotating quickly (gyroscope magnitude
  exceeds a configurable threshold, default 1.5 rad/s ≈ 86°/s), the tracker
  freezes and continues reporting the last stable match.  This prevents
  flickering during fast sweeps and ensures only deliberate, settled gestures
  trigger changes.

### Saving Oris

#### With buttons

- **Button A (GPIO 0)**: Press to save the current orientation as a new ori
  with an auto-generated name (`ori_0`, `ori_1`, ...).  The LED flashes white
  briefly to confirm.

- **Button B (GPIO 14)**: Press to print the currently active ori and list
  of all saved oris to the serial monitor.

#### With OSC commands

All ori commands use the address prefix `/annieData{device_name}/ori/...`:

| Command | Payload | Description |
|---------|---------|-------------|
| `/ori/save` | (none) | Save current orientation with auto-name |
| `/ori/save/{name}` | (none) | Save current orientation with given name |
| `/ori/save` | `"myName"` (string) | Save with name from payload |
| `/ori/delete/{name}` | (none) | Delete a saved ori |
| `/ori/clear` | (none) | Delete all saved oris |
| `/ori/list` | (none) | Reply with list of all oris (active marked with `*`) |
| `/ori/active` | (none) | Reply with the name of the currently active ori |
| `/ori/threshold` | float (rad/s) | Set the motion gate threshold |

### Conditional Messaging

Messages can be configured to send **only when a specific ori is active**,
or **only when a specific ori is NOT active**.  This is done with two special
keys in the message config string:

| Key | Value | Effect |
|-----|-------|--------|
| `ori_only` | ori name | Message sends ONLY when this ori is the active match |
| `ori_not` | ori name | Message sends ONLY when this ori is NOT the active match |

If neither key is set, the message sends unconditionally (normal behaviour).

#### Example: Stage light pointing

Suppose you have three stage lights and an actor holding the ab7 device.

**1. Save orientations** — have the actor point at each light and press
Button A (or send `/ori/save/light1`, etc.):

```
/annieData/mydevice/ori/save/light1
/annieData/mydevice/ori/save/light2
/annieData/mydevice/ori/save/light3
```

**2. Create "on" messages** — each sends a value of 1.0 when its light is
pointed at:

```
/annieData/mydevice/direct/light1_on
  value:eulerX, ip:192.168.1.50, port:7000, adr:/light1/intensity, low:1, high:1, ori_only:light1

/annieData/mydevice/direct/light2_on
  value:eulerX, ip:192.168.1.50, port:7000, adr:/light2/intensity, low:1, high:1, ori_only:light2

/annieData/mydevice/direct/light3_on
  value:eulerX, ip:192.168.1.50, port:7000, adr:/light3/intensity, low:1, high:1, ori_only:light3
```

**3. Create "off" messages** (optional) — each sends 0.0 when its light is
NOT pointed at:

```
/annieData/mydevice/direct/light1_off
  value:eulerX, ip:192.168.1.50, port:7000, adr:/light1/intensity, low:0, high:0, ori_not:light1
```

Now, when the actor points at light 1, the `light1_on` message fires and the
others are suppressed.  When they swing to light 2, `light1_on` stops and
`light2_on` starts.  The motion gate ensures that during fast sweeps between
positions, no spurious messages are sent.

### How Matching Works

The tracker uses the **geodesic angle** between unit quaternions:

```
angle = 2 × acos(|q_saved · q_current|)
```

The absolute value of the dot product handles the quaternion double-cover
(q and -q represent the same rotation).  The ori with the smallest angle
is always the active match.

- There is always exactly **one active ori** (the closest match), unless no
  oris have been saved.
- A typical threshold for "close enough" is not needed — the system always
  picks the nearest one, so coverage is continuous.
- The motion gate (default 1.5 rad/s) prevents updates during fast rotation.
  Adjust with `/ori/threshold` if the default is too sensitive or not
  sensitive enough for your use case.

### Serial Debug

Type `streams` in the serial monitor to see live data stream values,
including Euler angles derived from the BNO-085.  Press Button B to see the
current active ori.

### Storage

Ori data is stored in RAM and does not persist across reboots.  Save your
oris again after each power cycle, or integrate with the NVS save/load
system by calling `/save` and `/load`.  Message ori_only/ori_not conditions
are saved with the message config and will persist.
