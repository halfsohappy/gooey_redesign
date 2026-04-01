---
applyTo: "gooey/**"
---

# annieData Control Center Instructions

These conventions apply to the Python backend and JavaScript/CSS frontend
in `gooey/`.

## Build & Run

```bash
cd gooey
pip install -r requirements.txt
python run.py                 # http://127.0.0.1:5000
```

## Python Style

- Python ≥ 3.8. Use type hints where practical.
- Classes: `PascalCase`. Functions/methods: `snake_case`.
- Private members: leading `_` (e.g. `_receivers`, `_lock`).
- Thread safety via `threading.Lock()`.
- OSC string payloads must be sent as a single string argument; wrap in
  a list (`args:[payload]`) to prevent the frontend from space-splitting.

## JavaScript Style

- Vanilla JS — no frameworks, no build step.
- Wrap code in an IIFE: `(function () { … })()`.
- Enable `"use strict"`.
- Use `$()` and `$$()` helpers for DOM queries.
- When cloning cards for the star tab, strip all `id` attributes from the
  clone and its descendants to prevent duplicate IDs in the DOM.

## CSS / Design

- Light theme. Header: lavender `#DAC7FF`. Accent: `#90849c`.
- Fonts: Playwrite IE (title), Playwrite DE Grund (nav tabs), Martian Mono
  (body).
- Draggable card order persists in `localStorage` key `gooey_card_order`.
  Starred cards use `gooey_starred_cards`.
