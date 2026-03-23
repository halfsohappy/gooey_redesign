---
applyTo: "src/**"
---

# Firmware Instructions

These conventions apply to the C++ firmware in `src/`.

## Build

```bash
pio run            # compile (ESP32-S3)
pio run -t upload  # flash to device
```

Serial monitor at 115200 baud: `pio device monitor`.

## Style

- Header-only modules (`.h` files) — the only `.cpp` file is
  `bart_hardware.cpp`.
- Classes: `PascalCase`. Functions: `snake_case`. Constants: `UPPER_SNAKE_CASE`.
- Private/static variables use a leading `_`.
- Use fixed-size arrays; avoid dynamic allocation.
- Include detailed block comments for data flow and boot sequences.
- Use `// ==== ... ====` banners to separate sections within a file.

## Key Patterns

- Sensor values are normalised to `[0, 1]`.
- `normalise_cmd()` makes command matching case-insensitive by stripping
  underscores and lowercasing.
- OSC payloads are either a single string or a single float.
- Two FreeRTOS mutexes: one for the registry, one for send operations.
  Always acquire and release in the correct order to avoid deadlocks.
- NVS namespace: `device_config`.
