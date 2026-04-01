# TheaterGWD

Wireless motion-sensor-to-OSC translation system for live theater. An ESP32-S3 device reads IMU, barometric, and quaternion data, normalizes all values to `[0, 1]`, and transmits them as OSC over WiFi at configurable rates.

Two board variants: **Bart** (ISM330DHCX IMU + BMP5xx barometer) and **ab7** (BNO085 IMU + orientation tracking).

The **annieData Control Center** is the companion web GUI — it enables operators to configure sensor devices, manage shows, and monitor OSC traffic from a browser. Devices can also be controlled entirely through raw OSC commands from any software that speaks OSC.

## Documentation

| Guide | Audience | What's Covered |
|-------|----------|---------------|
| [Engineering Guide](docs/engineering.md) | Computer engineers | System architecture, firmware internals, build system, extending the code |
| [GUI Guide](docs/gooey_guide.md) | Theater people (GUI) | Installing and using the annieData Control Center |
| [OSC Guide](docs/osc_guide.md) | Theater people (OSC) | Controlling the device with raw OSC from any software |

## Quick Start

### Firmware

```bash
# Build and upload (PlatformIO)
pio run -e bart -t upload    # Bart board
pio run -e ab7 -t upload     # ab7 board
```

### annieData Control Center

```bash
# Install
pip install gooey-theatergwd

# Or with Homebrew (macOS)
brew install halfsohappy/theatergwd/gooey

# Run
gooey
```

Opens at http://127.0.0.1:5000. See the [GUI Guide](docs/gooey_guide.md) for full instructions.
