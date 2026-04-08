#!/usr/bin/env python3
"""osc_dmx.py — OSC-to-DMX bridge for Enttec DMX USB Pro.

Python port of the bc127 ESP32-S3 firmware.  Listens for OSC messages on
a UDP port and outputs DMX via an Enttec DMX USB Pro connected over USB.

Usage:
    python osc_dmx.py                           # auto-detect port, listen 8000
    python osc_dmx.py --port 9000               # custom OSC port
    python osc_dmx.py --device /dev/ttyUSB0     # explicit serial device
    python osc_dmx.py --list-ports              # show available serial ports

OSC address scheme (same as firmware):
    /annieData/dmx/{channel}                  value 0–255
    /annieData/dmx/fix/{head}/{attr}          attr: dimmer, red, green, blue,
                                                   shutter, color, rgb
    /annieData/dmx/group/{name}/{attr}        name: dimmer, colorsourcepar,
                                                   all, dimmers, pars
    /annieData/dmx/blackout                   (any payload)
    /annieData/dmx/restore                    (any payload)
"""

import argparse
import signal
import sys
import threading
import time

from pythonosc import dispatcher, osc_server

# DMXEnttecPro is imported lazily so --help works without the device plugged in.

# =============================================================================
# Constants
# =============================================================================

DMX_UNIVERSE_SIZE = 512
DMX_FPS = 40                      # transmit rate — matches firmware
OSC_DEFAULT_PORT = 8000

# =============================================================================
# XKCD Colour Table (same subset as firmware xkcd_colors.h)
# =============================================================================

XKCD_COLORS = {
    # ── Reds ─────────────────────────────────────────────────────────────────
    "red":                0xe50000,
    "dark red":           0x840000,
    "light red":          0xff474c,
    "bright red":         0xff000d,
    "blood red":          0x980002,
    "cherry red":         0xf7022a,
    "brick red":          0x8f1402,
    "rust red":           0xaa2704,
    "wine red":           0x7b0323,
    "raspberry":          0xb00149,
    "scarlet":            0xbe0119,
    "crimson":            0x8c000f,
    "vermillion":         0xf4320c,
    "tomato red":         0xec2d01,
    "fire engine red":    0xfe0002,
    "cherry":             0xcf0234,
    "ruby":               0xca0147,
    "rose red":           0xbe013c,
    "cranberry":          0x9e003a,
    "blood":              0x770001,
    "rusty red":          0xaf2f0d,

    # ── Pinks ────────────────────────────────────────────────────────────────
    "pink":               0xff81c0,
    "dark pink":          0xcb416b,
    "light pink":         0xffd1df,
    "hot pink":           0xff028d,
    "bright pink":        0xfe01b1,
    "pale pink":          0xffcfdc,
    "baby pink":          0xffb7ce,
    "salmon pink":        0xfe7b7c,
    "dusty pink":         0xd58a94,
    "rose pink":          0xf7879a,
    "bubblegum pink":     0xfe83cc,
    "neon pink":          0xfe019a,
    "magenta":            0xc20078,
    "fuchsia":            0xed0dd9,
    "hot magenta":        0xf504c9,
    "deep pink":          0xcb0162,
    "dusty rose":         0xc0737a,
    "mauve":              0xae7181,
    "old rose":           0xc87f89,
    "blush":              0xf29e8e,
    "blush pink":         0xfe828c,
    "soft pink":          0xfdb0c0,
    "pastel pink":        0xffbacd,
    "carnation pink":     0xff7fa7,
    "rose":               0xcf6275,

    # ── Oranges ──────────────────────────────────────────────────────────────
    "orange":             0xf97306,
    "dark orange":        0xc65102,
    "light orange":       0xfdaa48,
    "bright orange":      0xff5b00,
    "burnt orange":       0xc04e01,
    "red orange":         0xfd3c06,
    "tangerine":          0xff9408,
    "pumpkin":            0xe17701,
    "amber":              0xfeb308,
    "rust":               0xa83c09,
    "burnt sienna":       0xb04e0f,
    "peach":              0xffb07c,
    "apricot":            0xffb16d,
    "pale orange":        0xffa756,
    "melon":              0xff7855,
    "neon orange":        0xff6c11,
    "pastel orange":      0xff964f,
    "coral":              0xfc5a50,
    "light coral":        0xff6163,

    # ── Yellows ──────────────────────────────────────────────────────────────
    "yellow":             0xffff14,
    "dark yellow":        0xd5b60a,
    "light yellow":       0xfffe7a,
    "bright yellow":      0xfffd01,
    "pale yellow":        0xffff84,
    "lemon":              0xfdff52,
    "lemon yellow":       0xfdff38,
    "gold":               0xdbb40c,
    "golden yellow":      0xfec615,
    "golden":             0xf5bf03,
    "mustard":            0xceb301,
    "mustard yellow":     0xd2bd0a,
    "dandelion":          0xf8d568,
    "sandy yellow":       0xfdee73,
    "butter yellow":      0xfffd74,
    "banana yellow":      0xfafe4b,
    "cream":              0xffffc2,
    "canary yellow":      0xfffe40,
    "neon yellow":        0xcfff04,
    "banana":             0xffff7e,
    "sand yellow":        0xfce166,

    # ── Greens ───────────────────────────────────────────────────────────────
    "green":              0x15b01a,
    "dark green":         0x033500,
    "light green":        0x76ff7b,
    "bright green":       0x01ff07,
    "lime green":         0x89fe05,
    "lime":               0xaaff32,
    "neon green":         0x0cff0c,
    "forest green":       0x06470c,
    "olive green":        0x677a04,
    "olive":              0x6e750e,
    "dark olive green":   0x3c4d03,
    "grass green":        0x3f9b0b,
    "mint green":         0x8fff9f,
    "mint":               0x9ffeb0,
    "sage green":         0x88b378,
    "sage":               0x87ae73,
    "emerald green":      0x028f1e,
    "emerald":            0x01a049,
    "jade green":         0x2baf6a,
    "jade":               0x1fa774,
    "sea green":          0x53fca1,
    "seafoam green":      0x7af9ab,
    "seafoam":            0x80f9ad,
    "kelly green":        0x02ab2e,
    "hunter green":       0x0b4008,
    "army green":         0x4b5d16,
    "moss green":         0x658b38,
    "moss":               0x769958,
    "fern green":         0x548d44,
    "fern":               0x63a950,
    "pea green":          0x8eab12,
    "chartreuse":         0xc1f80a,
    "spring green":       0xa9f971,
    "pastel green":       0xb0ff9d,
    "pale green":         0xc7fdb5,
    "avocado green":      0x87a922,
    "pistachio":          0xc0fa8b,
    "pine green":         0x0a481e,
    "teal green":         0x25a36f,
    "leaf green":         0x5ca904,
    "evergreen":          0x05472a,
    "jungle green":       0x048243,

    # ── Blues ─────────────────────────────────────────────────────────────────
    "blue":               0x0343df,
    "dark blue":          0x00035b,
    "light blue":         0x95d0fc,
    "bright blue":        0x0165fc,
    "sky blue":           0x75bbfd,
    "baby blue":          0xa2cffe,
    "navy blue":          0x001146,
    "navy":               0x01153e,
    "royal blue":         0x0504aa,
    "cobalt blue":        0x030aa7,
    "cobalt":             0x1e488f,
    "steel blue":         0x5a7d9a,
    "ocean blue":         0x03719c,
    "powder blue":        0xb1d1fc,
    "ice blue":           0xd7fffe,
    "cornflower blue":    0x5170d7,
    "periwinkle blue":    0x8f99fb,
    "periwinkle":         0x8e82fe,
    "azure":              0x069af3,
    "cerulean":           0x0485d1,
    "midnight blue":      0x020035,
    "electric blue":      0x0652ff,
    "neon blue":          0x04d9ff,
    "slate blue":         0x5b7c99,
    "dusty blue":         0x5a86ad,
    "robin egg blue":     0x8af1fe,
    "robin's egg blue":   0x98eff9,
    "aqua blue":          0x02d8e9,
    "french blue":        0x436bad,
    "denim blue":         0x3b638c,
    "denim":              0x3b638c,
    "blue grey":          0x607c8e,
    "blue gray":          0x607c8e,
    "pastel blue":        0xa2bffe,
    "pale blue":          0xd0fefe,
    "true blue":          0x010fcc,
    "ultramarine":        0x2000b1,

    # ── Cyans / Teals ────────────────────────────────────────────────────────
    "cyan":               0x00ffff,
    "teal":               0x029386,
    "dark teal":          0x014d4e,
    "light teal":         0x90e4c1,
    "turquoise":          0x06c2ac,
    "dark turquoise":     0x045c5a,
    "aqua":               0x13eac9,
    "aquamarine":         0x04d8b2,
    "dark aqua":          0x05696b,
    "light aqua":         0x8cffdb,
    "light cyan":         0xacfffc,

    # ── Purples / Violets ────────────────────────────────────────────────────
    "purple":             0x7e1e9c,
    "dark purple":        0x35063e,
    "light purple":       0xbf77f6,
    "bright purple":      0xbe03fd,
    "deep purple":        0x36013f,
    "royal purple":       0x4b006e,
    "violet":             0x9a0eea,
    "dark violet":        0x34013f,
    "light violet":       0xd6b4fc,
    "lavender":           0xc79fef,
    "pale lavender":      0xeecffe,
    "light lavender":     0xdfc5fe,
    "dark lavender":      0x856798,
    "lilac":              0xcea2fd,
    "pale lilac":         0xe4cbff,
    "plum":               0x580f41,
    "plum purple":        0x4e0550,
    "grape":              0x6c3461,
    "grape purple":       0x5d1451,
    "indigo":             0x380282,
    "dark indigo":        0x1f0954,
    "eggplant":           0x380835,
    "eggplant purple":    0x430541,
    "amethyst":           0x9b5fc0,
    "orchid":             0xc875c4,
    "heliotrope":         0xd94ff5,
    "iris":               0x6258c4,
    "wisteria":           0xa87dc2,
    "mauve purple":       0x89689e,
    "pastel purple":      0xcaa0ff,
    "pale purple":        0xb790d4,
    "neon purple":        0xbc13fe,
    "electric purple":    0xaa23ff,
    "barney purple":      0xa00498,

    # ── Browns ───────────────────────────────────────────────────────────────
    "brown":              0x653700,
    "dark brown":         0x341c02,
    "light brown":        0xad8150,
    "chocolate brown":    0x411900,
    "chocolate":          0x3d1c02,
    "coffee":             0xa6814c,
    "mocha":              0x9d7651,
    "sienna":             0xa9561e,
    "tan":                0xd1b26f,
    "beige":              0xe6daa6,
    "khaki":              0xaaa662,
    "dark khaki":         0x9b8f55,
    "taupe":              0xb9a281,
    "caramel":            0xaf6f09,
    "chestnut":           0x742802,
    "mahogany":           0x4a0100,
    "auburn":             0x9a3001,
    "umber":              0xb26400,
    "raw umber":          0xa75e09,
    "burnt umber":        0xa0450e,
    "sand brown":         0xcba560,
    "sand":               0xe2ca76,
    "sandy":              0xf1da7a,
    "camel":              0xc69f59,
    "cocoa":              0x875f42,
    "hazel":              0x8e7618,

    # ── Greys / Neutrals ─────────────────────────────────────────────────────
    "grey":               0x929591,
    "gray":               0x929591,
    "dark grey":          0x363737,
    "dark gray":          0x363737,
    "light grey":         0xd8dcd6,
    "light gray":         0xd8dcd6,
    "charcoal":           0x343837,
    "charcoal grey":      0x3c4142,
    "slate grey":         0x59656d,
    "slate gray":         0x59656d,
    "slate":              0x516572,
    "silver":             0xc5c9c7,
    "warm grey":          0x978a84,
    "cool grey":          0x95a3a6,
    "gunmetal":           0x536267,
    "ash grey":           0xb4b6b7,
    "battleship grey":    0x6b7c85,
    "steel grey":         0x6f828a,
    "pewter":             0x8e8d85,

    # ── Whites / Off-Whites ──────────────────────────────────────────────────
    "white":              0xffffff,
    "off white":          0xffffe4,
    "ivory":              0xffffcb,
    "eggshell":           0xffffd4,
    "pearl":              0xd8d5c7,
    "bone":               0xe8dcc8,
    "linen":              0xfff0db,

    # ── Black ────────────────────────────────────────────────────────────────
    "black":              0x000000,
    "almost black":       0x070d0d,
    "very dark":          0x040005,

    # ── Warm / Cool Mixtures ─────────────────────────────────────────────────
    "salmon":             0xff796c,
    "dark salmon":        0xc85a53,
    "light salmon":       0xfea993,
    "terracotta":         0xca6641,
    "clay":               0xb66a50,
    "copper":             0xb66325,
    "bronze":             0xa87900,
    "pumpkin orange":     0xfb7d07,
    "marigold":           0xfcc006,
    "sunflower yellow":   0xffda03,
    "sunflower":          0xffc512,
    "goldenrod":          0xfac205,
    "ochre":              0xbf9005,
    "yellow ochre":       0xcb9d06,
    "burnt yellow":       0xd5ab09,
    "saffron":            0xfeb209,
    "yellowish green":    0xb0dd16,
    "greenish yellow":    0xcdfd02,
    "yellow green":       0xc0fb2d,
    "yellowgreen":        0xbbf90f,
    "green yellow":       0xc9ff27,
    "greenish blue":      0x0b8b87,
    "bluish green":       0x10a674,
    "blue green":         0x137e6d,
    "bluegreen":          0x017a79,
    "teal blue":          0x01889f,
    "ocean":              0x017b92,
    "bluish purple":      0x703be7,
    "purplish blue":      0x601ef9,
    "blue purple":        0x5729ce,
    "purple blue":        0x632de9,
    "reddish purple":     0x910951,
    "purplish red":       0xb0054b,
    "red purple":         0x820747,
    "purple red":         0x990147,
    "reddish brown":      0x7f2b0a,
    "brownish red":       0x9e3623,
    "reddish orange":     0xf8481c,
    "orangish red":       0xf43605,
    "orange red":         0xfd411e,
    "orangered":          0xfe420f,
    "brownish orange":    0xcb7723,
    "orangish brown":     0xb25f03,
    "pinkish red":        0xf10c45,
    "reddish pink":       0xfe2c54,
    "pink red":           0xf5054f,
    "pinkish purple":     0xd648d7,
    "purplish pink":      0xce5dae,
    "wine":               0x80013f,
    "burgundy":           0x610023,
    "maroon":             0x650021,
    "dark maroon":        0x3c0008,

    # ── Misc ─────────────────────────────────────────────────────────────────
    "warm":               0xfbb972,
    "cool blue":          0x4984b8,
    "cornflower":         0x6a79f7,
    "steel":              0x738595,
    "dusk":               0x4e5481,
    "twilight":           0x4e518b,
    "midnight":           0x03012d,
    "cloudy blue":        0xacc2d9,
    "stormy blue":        0x507b9c,
    "ice":                0xd6fffa,
    "swamp green":        0x748500,
    "camo green":         0x526525,
    "military green":     0x667c3e,
    "algae green":        0x21c36f,
    "greenish":           0x40a368,
    "orangeish":          0xfd8d49,
    "pinkish":            0xd46a7e,
    "purplish":           0x94568c,
    "bluish":             0x2976bb,
    "reddish":            0xc44240,
    "yellowish":          0xfaee66,
    "brownish":           0x9c6d57,
    "greyish":            0xa8a495,
}


def xkcd_lookup(name):
    """Look up an XKCD colour name.  Returns (r, g, b) or None."""
    key = name.strip().lower()
    rgb = XKCD_COLORS.get(key)
    if rgb is None:
        return None
    return ((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF)


def parse_hex_rgb(text):
    """Parse 'RRGGBB' or '#RRGGBB'.  Returns (r, g, b) or None."""
    h = text.strip().lstrip("#")
    if len(h) != 6:
        return None
    try:
        val = int(h, 16)
    except ValueError:
        return None
    return ((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF)


def colour_brightness(r, g, b):
    """Perceived brightness (fast integer approximation of luminance)."""
    return (r * 77 + g * 150 + b * 29) >> 8


# =============================================================================
# Fixture Map (same as firmware fixture_map.h)
# =============================================================================

FIX_DIMMER = "dimmer"
FIX_COLORSOURCEPAR = "colorsourcepar"


class Fixture:
    __slots__ = ("dmx_start", "channels", "type")

    def __init__(self, dmx_start, channels, fix_type):
        self.dmx_start = dmx_start
        self.channels = channels
        self.type = fix_type


# 1-based head index — index 0 is a placeholder
FIXTURES = [
    Fixture(0, 0, FIX_DIMMER),          # [0] placeholder

    # Heads 1–12: Generic Dimmer (1 ch)
    Fixture(1,  1, FIX_DIMMER),         # Head  1
    Fixture(2,  1, FIX_DIMMER),         # Head  2
    Fixture(3,  1, FIX_DIMMER),         # Head  3
    Fixture(4,  1, FIX_DIMMER),         # Head  4
    Fixture(5,  1, FIX_DIMMER),         # Head  5
    Fixture(6,  1, FIX_DIMMER),         # Head  6
    Fixture(7,  1, FIX_DIMMER),         # Head  7
    Fixture(8,  1, FIX_DIMMER),         # Head  8
    Fixture(9,  1, FIX_DIMMER),         # Head  9
    Fixture(10, 1, FIX_DIMMER),         # Head 10
    Fixture(11, 1, FIX_DIMMER),         # Head 11
    Fixture(12, 1, FIX_DIMMER),         # Head 12

    # Heads 13–22: ETC ColorSourcePar 5ch
    Fixture(13, 5, FIX_COLORSOURCEPAR), # Head 13
    Fixture(18, 5, FIX_COLORSOURCEPAR), # Head 14
    Fixture(23, 5, FIX_COLORSOURCEPAR), # Head 15
    Fixture(29, 5, FIX_COLORSOURCEPAR), # Head 16  (gap at 28)
    Fixture(34, 5, FIX_COLORSOURCEPAR), # Head 17
    Fixture(39, 5, FIX_COLORSOURCEPAR), # Head 18
    Fixture(44, 5, FIX_COLORSOURCEPAR), # Head 19
    Fixture(49, 5, FIX_COLORSOURCEPAR), # Head 20
    Fixture(54, 5, FIX_COLORSOURCEPAR), # Head 21
    Fixture(60, 5, FIX_COLORSOURCEPAR), # Head 22  (gap at 59)
]

NUM_FIXTURES = 22  # heads 1–22

GROUPS = {
    "dimmer":         [13, 14, 15, 16, 17],
    "colorsourcepar": [18, 19, 20, 21, 22],
    "all":            list(range(1, 23)),
    "dimmers":        list(range(1, 13)),
    "pars":           list(range(13, 23)),
}


def get_fixture(head):
    """Return the Fixture for a 1-based head number, or None."""
    if head < 1 or head > NUM_FIXTURES:
        return None
    return FIXTURES[head]


def find_group(name):
    """Return a list of head numbers for a group name (case-insensitive)."""
    return GROUPS.get(name.strip().lower())


# =============================================================================
# DMX Engine — manages the 512-channel universe
# =============================================================================

class DmxEngine:
    """Thread-safe DMX universe buffer with blackout / restore."""

    def __init__(self, controller):
        self._values = bytearray(DMX_UNIVERSE_SIZE + 1)  # index 0 unused
        self._blackout = False
        self._lock = threading.Lock()
        self._controller = controller

    def set_channel(self, channel, value):
        """Set a 1-based DMX channel (1–512) to value (0–255)."""
        if channel < 1 or channel > DMX_UNIVERSE_SIZE:
            return
        value = max(0, min(255, int(value)))
        with self._lock:
            self._values[channel] = value

    def get_channel(self, channel):
        if channel < 1 or channel > DMX_UNIVERSE_SIZE:
            return 0
        return self._values[channel]

    def blackout_on(self):
        with self._lock:
            self._blackout = True

    def blackout_off(self):
        with self._lock:
            self._blackout = False

    def is_blackout(self):
        return self._blackout

    def transmit(self):
        """Send the current universe to the Enttec interface."""
        with self._lock:
            if self._blackout:
                for ch in range(1, DMX_UNIVERSE_SIZE + 1):
                    self._controller.set_channel(ch, 0)
            else:
                for ch in range(1, DMX_UNIVERSE_SIZE + 1):
                    self._controller.set_channel(ch, self._values[ch])
        self._controller.submit()


# =============================================================================
# OSC Handler — parses addresses and dispatches to DMX engine
# =============================================================================

def normalise_cmd(seg):
    """Strip underscores and lowercase — matches firmware normalise_cmd()."""
    return seg.replace("_", "").lower()


def apply_fixture_attr(dmx, head, attr, payload):
    """Apply an attribute command to a single fixture head."""
    f = get_fixture(head)
    if f is None:
        return

    cmd = normalise_cmd(attr)

    try:
        val = max(0, min(255, int(float(payload))))
    except (ValueError, TypeError):
        val = 0

    if cmd in ("dimmer", "dim", "intensity"):
        if f.type == FIX_COLORSOURCEPAR:
            dmx.set_channel(f.dmx_start, val)       # +0 = Dimmer
        else:
            dmx.set_channel(f.dmx_start, val)

    elif cmd in ("red", "r"):
        if f.type == FIX_COLORSOURCEPAR:
            dmx.set_channel(f.dmx_start + 1, val)   # +1 = Red

    elif cmd in ("green", "g"):
        if f.type == FIX_COLORSOURCEPAR:
            dmx.set_channel(f.dmx_start + 2, val)   # +2 = Green

    elif cmd in ("blue", "b"):
        if f.type == FIX_COLORSOURCEPAR:
            dmx.set_channel(f.dmx_start + 3, val)   # +3 = Blue

    elif cmd in ("shutter", "strobe"):
        if f.type == FIX_COLORSOURCEPAR:
            dmx.set_channel(f.dmx_start + 4, val)   # +4 = Shutter

    elif cmd in ("color", "colour"):
        rgb = xkcd_lookup(payload)
        if rgb is None:
            return
        r, g, b = rgb
        if f.type == FIX_COLORSOURCEPAR:
            dmx.set_channel(f.dmx_start,     255)    # Dimmer full
            dmx.set_channel(f.dmx_start + 1, r)
            dmx.set_channel(f.dmx_start + 2, g)
            dmx.set_channel(f.dmx_start + 3, b)
        else:
            dmx.set_channel(f.dmx_start, colour_brightness(r, g, b))

    elif cmd in ("rgb", "hex"):
        rgb = parse_hex_rgb(payload)
        if rgb is None:
            return
        r, g, b = rgb
        if f.type == FIX_COLORSOURCEPAR:
            dmx.set_channel(f.dmx_start,     255)
            dmx.set_channel(f.dmx_start + 1, r)
            dmx.set_channel(f.dmx_start + 2, g)
            dmx.set_channel(f.dmx_start + 3, b)
        else:
            dmx.set_channel(f.dmx_start, colour_brightness(r, g, b))


def make_osc_handler(dmx):
    """Return a default handler for all OSC messages."""

    def handler(address, *args):
        # Extract payload — first argument as string
        if args:
            payload = str(int(args[0])) if isinstance(args[0], float) else str(args[0])
        else:
            payload = ""

        print(f"[OSC] {address} {payload}")

        # Find /dmx in the address
        dmx_idx = address.find("/dmx")
        if dmx_idx < 0:
            return

        rest = address[dmx_idx + 4:]
        if rest.startswith("/"):
            rest = rest[1:]
        if not rest:
            return

        segments = [s for s in rest.split("/") if s]
        if not segments:
            return

        seg0 = normalise_cmd(segments[0])

        # /annieData/dmx/blackout
        if seg0 == "blackout":
            dmx.blackout_on()
            print("[DMX] Blackout ON")
            return

        # /annieData/dmx/restore
        if seg0 == "restore":
            dmx.blackout_off()
            print("[DMX] Blackout OFF (restored)")
            return

        # /annieData/dmx/{channel} — direct channel set
        try:
            ch = int(segments[0])
            if 1 <= ch <= DMX_UNIVERSE_SIZE:
                val = max(0, min(255, int(float(payload))))
                dmx.set_channel(ch, val)
                return
        except (ValueError, IndexError):
            pass

        # /annieData/dmx/fix/{head}/{attr}
        if seg0 in ("fix", "fixture"):
            if len(segments) < 3:
                return
            try:
                head = int(segments[1])
            except ValueError:
                return
            apply_fixture_attr(dmx, head, segments[2], payload)
            return

        # /annieData/dmx/group/{name}/{attr}
        if seg0 in ("group", "grp"):
            if len(segments) < 3:
                return
            heads = find_group(segments[1])
            if heads is None:
                return
            for head in heads:
                apply_fixture_attr(dmx, head, segments[2], payload)
            return

    return handler


# =============================================================================
# DMX Transmit Thread
# =============================================================================

def dmx_send_loop(dmx, stop_event):
    """Continuously send DMX frames at ~40 fps until stop_event is set."""
    period = 1.0 / DMX_FPS
    while not stop_event.is_set():
        dmx.transmit()
        stop_event.wait(period)


# =============================================================================
# Serial Port Helpers
# =============================================================================

def list_serial_ports():
    """Print available serial ports and exit."""
    try:
        from DMXEnttecPro.utils import get_port_by_product_id
    except ImportError:
        pass
    import serial.tools.list_ports
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("No serial ports found.")
    else:
        print("Available serial ports:")
        for p in ports:
            print(f"  {p.device}  —  {p.description}  [{p.hwid}]")


def find_enttec_port():
    """Try to auto-detect the Enttec DMX USB Pro serial port."""
    import serial.tools.list_ports
    for p in serial.tools.list_ports.comports():
        desc = (p.description or "").lower()
        mfg = (p.manufacturer or "").lower()
        if "enttec" in desc or "enttec" in mfg:
            return p.device
        # FTDI chip commonly used by Enttec (VID 0403, PID 6001)
        if p.vid == 0x0403 and p.pid == 0x6001:
            return p.device
    return None


# =============================================================================
# Main
# =============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="OSC-to-DMX bridge for Enttec DMX USB Pro (bc127 Python port)")
    parser.add_argument("--port", type=int, default=OSC_DEFAULT_PORT,
                        help=f"UDP port to listen on (default: {OSC_DEFAULT_PORT})")
    parser.add_argument("--device", type=str, default=None,
                        help="Serial port for Enttec DMX USB Pro "
                             "(auto-detected if omitted)")
    parser.add_argument("--list-ports", action="store_true",
                        help="List available serial ports and exit")
    args = parser.parse_args()

    if args.list_ports:
        list_serial_ports()
        sys.exit(0)

    # ── Resolve serial device ────────────────────────────────────────────────
    device = args.device
    if device is None:
        device = find_enttec_port()
        if device is None:
            print("Error: could not auto-detect Enttec DMX USB Pro.", file=sys.stderr)
            print("       Use --device /dev/ttyUSB0  (or --list-ports to see options)",
                  file=sys.stderr)
            sys.exit(1)
        print(f"Auto-detected Enttec DMX USB Pro on {device}")

    # ── Initialise DMX output ────────────────────────────────────────────────
    from DMXEnttecPro import Controller
    try:
        controller = Controller(device)
    except Exception as exc:
        print(f"Error opening DMX device {device}: {exc}", file=sys.stderr)
        sys.exit(1)

    dmx = DmxEngine(controller)
    print(f"DMX output ready on {device}")

    # ── Start DMX transmit thread ────────────────────────────────────────────
    stop_event = threading.Event()
    tx_thread = threading.Thread(target=dmx_send_loop, args=(dmx, stop_event),
                                 daemon=True, name="dmx_tx")
    tx_thread.start()
    print(f"DMX transmit thread running ({DMX_FPS} fps)")

    # ── Start OSC server ─────────────────────────────────────────────────────
    disp = dispatcher.Dispatcher()
    disp.set_default_handler(make_osc_handler(dmx))

    server = osc_server.ThreadingOSCUDPServer(("0.0.0.0", args.port), disp)
    print(f"Listening for OSC on 0.0.0.0:{args.port}")
    print("════════════════════════════════════════════════")
    print("  annieData DMX — bc127 Python bridge ready")
    print("════════════════════════════════════════════════")
    print("Press Ctrl+C to quit.\n")

    # ── Graceful shutdown ────────────────────────────────────────────────────
    def shutdown(signum, frame):
        print("\nShutting down...")
        stop_event.set()
        server.shutdown()

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    server.serve_forever()

    # Cleanup
    stop_event.set()
    tx_thread.join(timeout=2)
    controller.clear_channels()
    controller.submit()
    print("DMX output zeroed.  Bye!")


if __name__ == "__main__":
    main()
