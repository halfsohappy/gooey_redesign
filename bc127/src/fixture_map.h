#ifndef BC127_FIXTURE_MAP_H
#define BC127_FIXTURE_MAP_H

// =============================================================================
// fixture_map.h — Fixture definitions from bc127_patch.html
// =============================================================================
//
// Universe 1 layout (from ChamSys QuickQ show file):
//
//   Heads  1–12 : Generic Dimmer (1 ch each)  → DMX 1–12
//   Head  13    : ETC ColorSourcePar 5ch       → DMX 13–17
//   Head  14    : ETC ColorSourcePar 5ch       → DMX 18–22
//   Head  15    : ETC ColorSourcePar 5ch       → DMX 23–27
//   Head  16    : ETC ColorSourcePar 5ch       → DMX 29–33  (gap at 28)
//   Head  17    : ETC ColorSourcePar 5ch       → DMX 34–38
//   Head  18    : ETC ColorSourcePar 5ch       → DMX 39–43
//   Head  19    : ETC ColorSourcePar 5ch       → DMX 44–48
//   Head  20    : ETC ColorSourcePar 5ch       → DMX 49–53
//   Head  21    : ETC ColorSourcePar 5ch       → DMX 54–58
//   Head  22    : ETC ColorSourcePar 5ch       → DMX 60–64  (gap at 59)
//
// ColorSourcePar channel offsets (from DMX start address):
//   +0 = Dimmer, +1 = Red, +2 = Green, +3 = Blue, +4 = Shutter
//
// Fixture Groups (from show file):
//   "Dimmer"         → Heads 13, 14, 15, 16, 17
//   "ColorSourcePar" → Heads 18, 19, 20, 21, 22
//
// =============================================================================

#include <Arduino.h>

// ==== Fixture Type Enum =====================================================

enum FixtureType : uint8_t {
    FIX_DIMMER,          // 1-channel dimmer
    FIX_COLORSOURCEPAR   // 5-channel RGB par (Dimmer, R, G, B, Shutter)
};

// ==== Fixture Descriptor ====================================================

struct Fixture {
    uint16_t    dmx_start;   // 1-based DMX start address
    uint8_t     channels;    // number of DMX channels
    FixtureType type;
};

// ==== Fixture Table (indexed by head number, 1-based — index 0 unused) ======

static const Fixture FIXTURES[] = {
    // [0] placeholder (heads are 1-based)
    { 0,  0, FIX_DIMMER },

    // Heads 1–12: Generic Dimmer (1 ch)
    {  1, 1, FIX_DIMMER },   // Head  1
    {  2, 1, FIX_DIMMER },   // Head  2
    {  3, 1, FIX_DIMMER },   // Head  3
    {  4, 1, FIX_DIMMER },   // Head  4
    {  5, 1, FIX_DIMMER },   // Head  5
    {  6, 1, FIX_DIMMER },   // Head  6
    {  7, 1, FIX_DIMMER },   // Head  7
    {  8, 1, FIX_DIMMER },   // Head  8
    {  9, 1, FIX_DIMMER },   // Head  9
    { 10, 1, FIX_DIMMER },   // Head 10
    { 11, 1, FIX_DIMMER },   // Head 11
    { 12, 1, FIX_DIMMER },   // Head 12

    // Heads 13–22: ETC ColorSourcePar 5ch
    { 13, 5, FIX_COLORSOURCEPAR },   // Head 13
    { 18, 5, FIX_COLORSOURCEPAR },   // Head 14
    { 23, 5, FIX_COLORSOURCEPAR },   // Head 15
    { 29, 5, FIX_COLORSOURCEPAR },   // Head 16  (gap at 28)
    { 34, 5, FIX_COLORSOURCEPAR },   // Head 17
    { 39, 5, FIX_COLORSOURCEPAR },   // Head 18
    { 44, 5, FIX_COLORSOURCEPAR },   // Head 19
    { 49, 5, FIX_COLORSOURCEPAR },   // Head 20
    { 54, 5, FIX_COLORSOURCEPAR },   // Head 21
    { 60, 5, FIX_COLORSOURCEPAR },   // Head 22  (gap at 59)
};

static constexpr int NUM_FIXTURES = 22;   // heads 1–22

// ==== Fixture Groups ========================================================

struct FixtureGroup {
    const char* name;
    const uint8_t* heads;     // array of head numbers
    uint8_t       count;
};

// Group members (head numbers)
static const uint8_t GRP_DIMMER_HEADS[]         = { 13, 14, 15, 16, 17 };
static const uint8_t GRP_COLORSOURCEPAR_HEADS[] = { 18, 19, 20, 21, 22 };
static const uint8_t GRP_ALL_HEADS[]            = {  1,  2,  3,  4,  5,  6,
                                                      7,  8,  9, 10, 11, 12,
                                                     13, 14, 15, 16, 17,
                                                     18, 19, 20, 21, 22 };
static const uint8_t GRP_DIMMERS_HEADS[]        = {  1,  2,  3,  4,  5,  6,
                                                      7,  8,  9, 10, 11, 12 };
static const uint8_t GRP_PARS_HEADS[]           = { 13, 14, 15, 16, 17,
                                                     18, 19, 20, 21, 22 };

static const FixtureGroup GROUPS[] = {
    { "dimmer",         GRP_DIMMER_HEADS,         5  },
    { "colorsourcepar", GRP_COLORSOURCEPAR_HEADS, 5  },
    { "all",            GRP_ALL_HEADS,           22  },
    { "dimmers",        GRP_DIMMERS_HEADS,       12  },
    { "pars",           GRP_PARS_HEADS,          10  },
};

static constexpr int NUM_GROUPS = sizeof(GROUPS) / sizeof(GROUPS[0]);

// ==== Helper — find group by name (case-insensitive) ========================

inline const FixtureGroup* find_group(const String& name) {
    String lower = name;
    lower.toLowerCase();
    for (int i = 0; i < NUM_GROUPS; i++) {
        if (lower == GROUPS[i].name) return &GROUPS[i];
    }
    return nullptr;
}

// ==== Helper — get fixture by head number (1-based) =========================

inline const Fixture* get_fixture(int head) {
    if (head < 1 || head > NUM_FIXTURES) return nullptr;
    return &FIXTURES[head];
}

// ==== ColorSourcePar channel offset helpers =================================

// These return the 1-based DMX address for each attribute of a par fixture.
// Returns 0 if the fixture is not a ColorSourcePar.

inline uint16_t par_dimmer_addr(const Fixture& f) {
    return (f.type == FIX_COLORSOURCEPAR) ? f.dmx_start     : 0;
}
inline uint16_t par_red_addr(const Fixture& f) {
    return (f.type == FIX_COLORSOURCEPAR) ? f.dmx_start + 1 : 0;
}
inline uint16_t par_green_addr(const Fixture& f) {
    return (f.type == FIX_COLORSOURCEPAR) ? f.dmx_start + 2 : 0;
}
inline uint16_t par_blue_addr(const Fixture& f) {
    return (f.type == FIX_COLORSOURCEPAR) ? f.dmx_start + 3 : 0;
}
inline uint16_t par_shutter_addr(const Fixture& f) {
    return (f.type == FIX_COLORSOURCEPAR) ? f.dmx_start + 4 : 0;
}

#endif // BC127_FIXTURE_MAP_H
