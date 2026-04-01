# Copilot Instructions for TheaterGWD

## Project Overview

TheaterGWD is a sensor-to-OSC (Open Sound Control) ecosystem for theater
applications. It consists of two main components:

| Component | Directory | Language | Purpose |
|-----------|-----------|----------|---------|
| **Firmware** | `src/` | C++ (Arduino) | ESP32-S3 sensor firmware — reads barometer, IMU, magnetometer and streams data as OSC messages over WiFi |
| **Gooey** | `gooey/` | Python + vanilla JS | Web-based control center (Flask + SocketIO) for managing devices, messages, and scenes |

Additional directories:
- `platforms/` — PlatformIO configs for 25+ alternative microcontroller boards.
- `Formula/` — Homebrew formula for installing the Gooey control center on macOS.
- `docs/` — Engineering guide, Gooey guide, and OSC guide.
- `data/` — Font assets used by the firmware captive portal.

## Build & Run

### Firmware (PlatformIO)

```bash
cd /path/to/TheaterGWD
pio run                       # compile for ESP32-S3
pio run -t upload             # compile and flash
pio device monitor            # open serial monitor (115200 baud)
```

Use alternative board configs from `platforms/` by copying them over
`platformio.ini` or using the `-c` flag.

### Gooey Control Center (Python)

```bash
cd gooey
pip install -r requirements.txt
python run.py                 # starts web UI at http://127.0.0.1:5000
```

Or install as a package:

```bash
cd gooey
pip install .
gooey                        # runs the CLI entry point
```

Or via Homebrew (macOS):

```bash
brew install halfsohappy/theatergwd/gooey
```

## Testing

There is currently no formal test suite. Validate firmware changes by
flashing and monitoring serial output. Validate Gooey changes by running
the web UI and verifying behavior in the browser.

## Coding Conventions

### General

- Indentation: 4 spaces everywhere (C++, Python, JS, HTML, CSS).
- No trailing whitespace.
- OSC commands accept `camelCase`, `snake_case`, and lowercase via the
  `normalise_cmd()` helper, which strips underscores and lowercases.
  User-defined names preserve their original case.

### C++ Firmware (`src/`)

- **Header-only modules** — all implementation lives in `.h` files
  (except `bart_hardware.cpp`).
- Classes: `PascalCase` (e.g. `OscMessage`, `OscScene`).
- Functions: `snake_case` (e.g. `begin_pins()`, `osc_trim_copy()`).
- Constants/macros: `UPPER_SNAKE_CASE` (e.g. `MAX_OSC_PATCHES`, `CS_IMU`).
- Private/static variables: leading `_` (e.g. `_message_log`).
- Section dividers: `// ==== ... ====` ASCII-art banners.
- Fixed-size arrays over dynamic allocation for deterministic memory
  usage on embedded targets.
- Sensor values are normalised to the `[0, 1]` range.
- FreeRTOS tasks handle concurrency; two mutexes guard the registry
  and send operations.

### Python (`gooey/`)

- Python ≥ 3.8. Dependencies: Flask, Flask-SocketIO, python-osc.
- Classes: `PascalCase`; functions/methods: `snake_case`; private
  members: leading `_`.
- Use `threading.Lock()` for thread safety.
- Validate IP addresses with the `_IP_RE` regex in `osc_handler.py`.
- OSC string payloads must be sent as a **single string argument** —
  wrap in a list (`args:[payload]`) to prevent space-splitting.

### JavaScript (`gooey/app/static/js/`)

- Vanilla JS — no framework, no build step.
- IIFE pattern: `(function () { … })()` for namespace isolation.
- `"use strict"` mode.
- `$()` / `$$()` helper functions for DOM selection.
- Section markers: `/* ── Section name ── */`.

### CSS (`gooey/app/static/css/`)

- Design language: light theme, lavender `#DAC7FF` header, accent `#90849c`.
- Fonts: Playwrite IE (title), Playwrite DE Grund (nav), Martian Mono (body).
- Body font size: 18 px; header height: 98 px.

## Architecture Notes

- **OSC address format**: `/annieData/{device_adr}/{command}`.
- **Scenes** can override message bounds (`low`/`high`) for output scaling.
  Address composition modes: `fallback`, `override`, `prepend`, `append`.
- **WiFi provisioning** stores credentials in NVS namespace `device_config`
  with keys: `ssid`, `network_password`, `use_dhcp`, `static_ip`, `port`,
  `device_adr`, `provisioned`.
- **Gooey device registry** is client-side (JS). Backend API at `/api/devices/`.
  Reply parsing auto-populates the registry from `list`/`info` status messages.
- **Draggable cards** use `data-card-id` attributes; order persists via
  `localStorage` key `gooey_card_order`. Star tab uses `gooey_starred_cards`.
