#pragma once
#include "main.h"

#define MAX_OSC_PATCHES 64
#define MAX_OSC_MESSAGES 256
class OscPatch;
class OscRegistry;

static inline String osc_trim_copy(String s) {
    s.trim(); // remove leading and trailing whitespace
    return s;
}

static inline String osc_lower_copy(String s) {
    s.toLowerCase(); // convert to lowercase for case-insensitive comparison
    return s;
}

static inline int osc_value_index_from_name(const String& value_name) {
    String key = osc_lower_copy(osc_trim_copy(value_name));
    if (key == "accelx") return ACCELX;
    if (key == "accely") return ACCELY;
    if (key == "accelz") return ACCELZ;
    if (key == "accellength" || key == "accellen" || key == "accelen" || key == "alen") return ACCELLENGTH;
    if (key == "gyrox") return GYROX;
    if (key == "gyroy") return GYROY;
    if (key == "gyroz") return GYROZ;
    if (key == "gyrolength" || key == "gyrolen" || key == "glen") return GYROLENGTH;
    if (key == "baro") return BARO;
    if (key == "eulerx") return EULERX;
    if (key == "eulery") return EULERY;
    if (key == "eulerz") return EULERZ;
    return -1;
}

struct existing {
    bool name = false;

    bool ip = false;
    bool port = false;
    bool adr = false;

    bool patch = false;

    bool val = false;
    bool low = false;
    bool high = false;
};


class OscMessage{
    public:
    existing exist;  
    String name;

    IPAddress ip;
    unsigned int port;
    String osc_address;

    OscPatch* patch;

    float* value_ptr; // pointer to the live value
    float bounds[2] = {0.0,1.0}; //min, max

    OscMessage() {
        ip = IPAddress(0,0,0,0);
        port = 0;
        osc_address = "";
        patch = nullptr;
        value_ptr = nullptr;
    }

    OscMessage(String set_name) {
        name = set_name;
        exist.name = true;
        ip = IPAddress(0,0,0,0);
        port = 0;
        osc_address = "";
        patch = nullptr;
        value_ptr = nullptr;
    }

    // define copy constructor and assignment operator to copy all fields, including exist
    OscMessage(const OscMessage& other) {
        exist = other.exist;
        name = other.name;
        ip = other.ip;
        port = other.port;
        osc_address = other.osc_address;
        patch = other.patch;
        value_ptr = other.value_ptr;
        bounds[0] = other.bounds[0];
        bounds[1] = other.bounds[1];
    }
    // assignment operator
    OscMessage& operator=(const OscMessage& other) {
        if (this != &other) {
            exist = other.exist;
            name = other.name;
            ip = other.ip;
            port = other.port;
            osc_address = other.osc_address;
            patch = other.patch;
            value_ptr = other.value_ptr;
            bounds[0] = other.bounds[0];
            bounds[1] = other.bounds[1];
        }
        return *this;
    }
    
    // redefine * operator, so oscMessage * oscMessage returns 
    // a new OscMessage with all exist fields set to true if they are true in either operand, 
    // and values taken from the first operand if it exists, otherwise the second operand
    OscMessage operator*(const OscMessage& other) const {
        OscMessage result;
        result.exist.name = exist.name || other.exist.name;
        result.name = exist.name ? name : other.name;

        result.exist.ip = exist.ip || other.exist.ip;
        result.ip = exist.ip ? ip : other.ip;

        result.exist.port = exist.port || other.exist.port;
        result.port = exist.port ? port : other.port;

        result.exist.adr = exist.adr || other.exist.adr;
        result.osc_address = exist.adr ? osc_address : other.osc_address;

        result.exist.patch = exist.patch || other.exist.patch;
        result.patch = exist.patch ? patch : other.patch;

        result.exist.val = exist.val || other.exist.val;
        result.value_ptr = exist.val ? value_ptr : other.value_ptr;

        result.exist.low = exist.low || other.exist.low;
        result.bounds[0] = exist.low ? bounds[0] : other.bounds[0];

        result.exist.high = exist.high || other.exist.high;
        result.bounds[1] = exist.high ? bounds[1] : other.bounds[1];

        return result;
    }



 

    bool sendable() const;
    bool from_config_str(const String& config, String* error = nullptr);



};




class OscPatch {
    public:    
    existing exist;
    TaskHandle_t send_task_handle;
    String name;
    String osc_address;
    unsigned int port;
    IPAddress ip;
    unsigned int send_period_ms = 50; // default to 50ms between sends

    OscPatch() {
        ip = IPAddress(0,0,0,0);
        port = 0;
        osc_address = "";
    }
    OscPatch(String set_name) {
        name = set_name;
        exist.name = true;
        ip = IPAddress(0,0,0,0);
        port = 0;
        osc_address = "";
    }
    OscPatch(const OscMessage& msg){
        name = msg.name;
        exist.name = msg.exist.name;

        ip = msg.ip;
        exist.ip = msg.exist.ip;

        port = msg.port;
        exist.port = msg.exist.port;

        osc_address = msg.osc_address;
        exist.adr = msg.exist.adr;
    }


};

// ============================================================================
// OscRegistry — Central owner of all OscPatch and OscMessage objects
// ============================================================================
//
// WHY THIS EXISTS:
//   You need a single place that OWNS patches and messages so they persist
//   in memory and can be found by name from anywhere in the program.
//   Without this, locally-created OscMessage/OscPatch objects die when the
//   function returns, leaving dangling pointers.
//
// MEMORY MODEL (important for embedded / ESP32):
//
//   The registry stores actual objects in fixed-size arrays — NOT heap-
//   allocated pointers. This means:
//
//   - Objects live inside the registry arrays for the entire program
//     lifetime. They are never freed, which is correct for an embedded
//     system that runs until power-off.
//
//   - Pointers returned by find_patch(), get_or_create_msg(), etc. point
//     directly into the arrays. These pointers are STABLE — the arrays
//     never move or reallocate. It is safe to store them long-term
//     (e.g., OscMessage::patch can hold a pointer to a registry patch).
//
//   - No heap allocation (no new/malloc). ESP32 has ~300KB of heap shared
//     with WiFi/BLE stacks. Dynamic allocation (new/malloc) risks:
//       * Fragmentation: repeated alloc/free creates gaps too small to reuse
//       * Exhaustion: no virtual memory — when RAM is gone, you crash
//       * Unpredictability: malloc can fail at any time under load
//     Fixed arrays use BSS/data memory with a size known at compile time:
//       sizeof(OscPatch)*MAX_OSC_PATCHES + sizeof(OscMessage)*MAX_OSC_MESSAGES
//
//   - Trade-off: capacity is fixed at compile time. Increase the #defines
//     at the top of this file and recompile if you need more.
//
// HOW TO USE:
//
//   OscRegistry& reg = osc_registry();
//
//   // Create or find a patch — returns stable pointer to stored object:
//   OscPatch* p = reg.get_or_create_patch("myPatch");
//   p->ip = IPAddress(192,168,1,100);
//   p->port = 9000;
//
//   // Create or find a message:
//   OscMessage* m = reg.get_or_create_msg("accelX_out");
//   m->patch = p;  // safe: both pointers are stable registry references
//   m->value_ptr = &data_streams[ACCELX];
//
//   // Look up later from anywhere:
//   OscPatch* found = reg.find_patch("myPatch");   // returns p, or nullptr
//   OscMessage* fm = reg.find_msg("accelX_out");   // returns m, or nullptr
//
//   // Merge parsed config into a registered message:
//   OscMessage parsed;
//   parsed.from_config_str("ip:192.168.1.50, port:9000, value:accelx");
//   OscMessage* stored = reg.update_msg(parsed);   // creates or updates by name
//
// THREAD SAFETY:
//   Not thread-safe. If multiple FreeRTOS tasks need to read/write the
//   registry concurrently, protect calls with a mutex (xSemaphoreCreateMutex).
//   Currently only the main loop task modifies it.
//
// ============================================================================

class OscRegistry {
public:
    OscPatch patches[MAX_OSC_PATCHES];
    uint8_t patch_count = 0;

    OscMessage messages[MAX_OSC_MESSAGES];
    uint8_t msg_count = 0;

    // Find a patch by name (case-insensitive). Returns nullptr if not found.
    OscPatch* find_patch(const String& name) {
        String key = osc_lower_copy(osc_trim_copy(name));
        for (uint8_t i = 0; i < patch_count; i++) {
            if (osc_lower_copy(osc_trim_copy(patches[i].name)) == key) {
                return &patches[i];
            }
        }
        return nullptr;
    }

    // Find a message by name (case-insensitive). Returns nullptr if not found.
    OscMessage* find_msg(const String& name) {
        String key = osc_lower_copy(osc_trim_copy(name));
        for (uint8_t i = 0; i < msg_count; i++) {
            if (osc_lower_copy(osc_trim_copy(messages[i].name)) == key) {
                return &messages[i];
            }
        }
        return nullptr;
    }

    // Get existing patch by name, or create a new one if it doesn't exist.
    // Returns nullptr only if the registry is full (patch_count == MAX_OSC_PATCHES).
    OscPatch* get_or_create_patch(const String& name) {
        OscPatch* found = find_patch(name);
        if (found) return found;
        if (patch_count >= MAX_OSC_PATCHES) return nullptr;
        patches[patch_count] = OscPatch(name);
        return &patches[patch_count++];
    }

    // Get existing message by name, or create a new one if it doesn't exist.
    // Returns nullptr only if the registry is full (msg_count == MAX_OSC_MESSAGES).
    OscMessage* get_or_create_msg(const String& name) {
        OscMessage* found = find_msg(name);
        if (found) return found;
        if (msg_count >= MAX_OSC_MESSAGES) return nullptr;
        messages[msg_count] = OscMessage(name);
        return &messages[msg_count++];
    }

    // Merge a patch's set fields into the registry. Finds by name (or creates),
    // then copies only the fields where src.exist flags are true.
    // Returns pointer to the stored patch, or nullptr if full.
    OscPatch* update_patch(const OscPatch& src) {
        OscPatch* p = get_or_create_patch(src.name);
        if (!p) return nullptr;
        if (src.exist.ip)   { p->ip = src.ip; p->exist.ip = true; }
        if (src.exist.port) { p->port = src.port; p->exist.port = true; }
        if (src.exist.adr)  { p->osc_address = src.osc_address; p->exist.adr = true; }
        return p;
    }

    // Merge a message's set fields into the registry. Finds by name (or creates),
    // then copies only the fields where src.exist flags are true.
    // Returns pointer to the stored message, or nullptr if full.
    OscMessage* update_msg(const OscMessage& src) {
        OscMessage* m = get_or_create_msg(src.name);
        if (!m) return nullptr;
        if (src.exist.ip)    { m->ip = src.ip; m->exist.ip = true; }
        if (src.exist.port)  { m->port = src.port; m->exist.port = true; }
        if (src.exist.adr)   { m->osc_address = src.osc_address; m->exist.adr = true; }
        if (src.exist.patch) { m->patch = src.patch; m->exist.patch = true; }
        if (src.exist.val)   { m->value_ptr = src.value_ptr; m->exist.val = true; }
        if (src.exist.low)   { m->bounds[0] = src.bounds[0]; m->exist.low = true; }
        if (src.exist.high)  { m->bounds[1] = src.bounds[1]; m->exist.high = true; }
        return m;
    }

    uint8_t count_patches() const { return patch_count; }
    uint8_t count_msgs() const { return msg_count; }
};

// Global registry accessor (Meyer's singleton).
// Created on first call, lives forever. Thread-safe init in C++11+.
static inline OscRegistry& osc_registry() {
    static OscRegistry instance;
    return instance;
}

//THIS IS THE STRING PARSER, MAKES NEW MSG OUT OF CONFIG STR
inline bool OscMessage::from_config_str(const String& config, String* error) { // parse config string of format "key1:value1, key2:value2, ..." and set fields accordingly, return false if parsing fails with error message in *error
    // reset parseable fields
    exist = existing{};
    ip = IPAddress(0,0,0,0);
    port = 0;
    osc_address = "";
    patch = nullptr;
    value_ptr = nullptr;
    bounds[0] = 0.0f;
    bounds[1] = 1.0f;

    String input = config;
    input.trim();
    if (input.length() == 0) {
        if (error) *error = "Empty OSC config string";
        return false;
    }

    size_t start = 0;
    while (start < input.length()) {
        size_t comma = input.indexOf(',', start);
        String token = (comma == (size_t)-1) ? input.substring(start) : input.substring(start, comma);
        token = osc_trim_copy(token);
        start = (comma == (size_t)-1) ? input.length() : comma + 1;

        if (token.length() == 0) {
            continue;
        }

        // Determine separator: ':' means direct value, '-' means "inherit this
        // field from the named message or patch in the registry".
        // Whichever separator appears first wins, so a direct value like
        // "adr:/foo/bar-baz" is handled correctly (colon is first).
        int colon = token.indexOf(':');
        int dash  = token.indexOf('-');

        bool is_ref = false; // true when the value is a registry name to look up
        int sep = -1;
        if (colon >= 0 && (dash < 0 || colon <= dash)) {
            sep = colon;
            is_ref = false;
        } else if (dash >= 0) {
            sep = dash;
            is_ref = true;
        } else {
            if (error) *error = "Missing ':' or '-' in token: " + token;
            return false;
        }

        String key   = osc_lower_copy(osc_trim_copy(token.substring(0, sep)));
        String value = osc_trim_copy(token.substring(sep + 1));

        // --- reference mode: value is the name of a registered msg/patch ---
        // We look in the patch registry first, then the message registry, so
        // that shared fields (ip, port, adr) resolve to either type.
        if (is_ref) {
            OscRegistry& reg = osc_registry();
            OscPatch*   ref_p = reg.find_patch(value);
            OscMessage* ref_m = reg.find_msg(value);

            if (key == "ip") {
                if      (ref_p && ref_p->exist.ip) { ip = ref_p->ip; exist.ip = true; }
                else if (ref_m && ref_m->exist.ip) { ip = ref_m->ip; exist.ip = true; }
                else { if (error) *error = "Ref '" + value + "' has no ip"; return false; }
            } else if (key == "port") {
                if      (ref_p && ref_p->exist.port) { port = ref_p->port; exist.port = true; }
                else if (ref_m && ref_m->exist.port) { port = ref_m->port; exist.port = true; }
                else { if (error) *error = "Ref '" + value + "' has no port"; return false; }
            } else if (key == "adr" || key == "addr" || key == "address") {
                if      (ref_p && ref_p->exist.adr) { osc_address = ref_p->osc_address; exist.adr = true; }
                else if (ref_m && ref_m->exist.adr) { osc_address = ref_m->osc_address; exist.adr = true; }
                else { if (error) *error = "Ref '" + value + "' has no adr"; return false; }
            } else if (key == "patch") {
                // "patch-name" means the same as "patch:name" — look up a patch
                patch = ref_p ? ref_p : reg.get_or_create_patch(value);
                exist.patch = true;
            } else if (key == "value") {
                if (ref_m && ref_m->exist.val) { value_ptr = ref_m->value_ptr; exist.val = true; }
                else { if (error) *error = "Ref '" + value + "' has no value"; return false; }
            } else if (key == "low" || key == "min" || key == "lo") {
                if (ref_m && ref_m->exist.low) { bounds[0] = ref_m->bounds[0]; exist.low = true; }
                else { if (error) *error = "Ref '" + value + "' has no low"; return false; }
            } else if (key == "high" || key == "max" || key == "hi") {
                if (ref_m && ref_m->exist.high) { bounds[1] = ref_m->bounds[1]; exist.high = true; }
                else { if (error) *error = "Ref '" + value + "' has no high"; return false; }
            } else if (key == "default" || key == "all") {
                // Copy every set field from the named patch/msg, but ONLY into
                // fields that are not already set on this message. This means
                // any other explicit key in the same config string takes priority
                // regardless of whether it appears before or after this token.
                if (!ref_p && !ref_m) {
                    if (error) *error = "default/all: no patch or message named '" + value + "'";
                    return false;
                }
                // Patch fields (ip, port, adr are the only ones a patch carries)
                if (ref_p) {
                    if (ref_p->exist.ip   && !exist.ip)   { ip = ref_p->ip;                    exist.ip   = true; }
                    if (ref_p->exist.port && !exist.port)  { port = ref_p->port;                exist.port = true; }
                    if (ref_p->exist.adr  && !exist.adr)   { osc_address = ref_p->osc_address;  exist.adr  = true; }
                }
                // Message fields (check ref_m; also check ref_p again for shared fields
                // if ref_m didn't provide them)
                if (ref_m) {
                    if (ref_m->exist.ip    && !exist.ip)    { ip = ref_m->ip;                   exist.ip    = true; }
                    if (ref_m->exist.port  && !exist.port)  { port = ref_m->port;               exist.port  = true; }
                    if (ref_m->exist.adr   && !exist.adr)   { osc_address = ref_m->osc_address; exist.adr   = true; }
                    if (ref_m->exist.patch && !exist.patch) { patch = ref_m->patch;             exist.patch = true; }
                    if (ref_m->exist.val   && !exist.val)   { value_ptr = ref_m->value_ptr;     exist.val   = true; }
                    if (ref_m->exist.low   && !exist.low)   { bounds[0] = ref_m->bounds[0];     exist.low   = true; }
                    if (ref_m->exist.high  && !exist.high)  { bounds[1] = ref_m->bounds[1];     exist.high  = true; }
                }
            } else {
                if (error) *error = "Unknown config key: " + key;
                return false;
            }
            continue; // done with this token
        }

        // --- direct mode: value is a literal ---
        if (key == "name") {
            name = value;
            exist.name = true;
        } else if (key == "ip") {
            IPAddress parsed;
            if (!parsed.fromString(value)) {
                if (error) *error = "Invalid IP address: " + value;
                return false;
            }
            ip = parsed;
            exist.ip = true;
        } else if (key == "port") {
            long parsed = value.toInt();
            if (parsed < 1 || parsed > 65535) {
                if (error) *error = "Invalid port: " + value;
                return false;
            }
            port = (unsigned int)parsed;
            exist.port = true;
        } else if (key == "adr" || key == "addr" || key == "address") {
            osc_address = value;
            exist.adr = true;
        } else if (key == "patch") {
            patch = osc_registry().find_patch(value);
            if (!patch) {
                if (error) *error = "had to create patch:" + value;
                patch = osc_registry().get_or_create_patch(value);
            }
            exist.patch = true;
        } else if (key == "value") {
            int value_index = osc_value_index_from_name(value);
            if (value_index < 0 || value_index >= NUM_DATA_STREAMS) {
                if (error) *error = "Unknown value name: " + value;
                return false;
            }
            value_ptr = &data_streams[value_index];
            exist.val = true;
        } else if (key == "low" || key == "min" || key == "lo") {
            bounds[0] = value.toFloat();
            exist.low = true;
        } else if (key == "high" || key == "max" || key == "hi") {
            bounds[1] = value.toFloat();
            exist.high = true;
        } else {
            if (error) *error = "Unknown config key: " + key;
            return false;
        }
    }


    return true;
}

// define member after OscPatch is known
bool OscMessage::sendable() const {
    return (
        value_ptr != nullptr &&
        (osc_address != "" || (patch && patch->osc_address != "")) &&
        (port != 0 || (patch && patch->port != 0)) &&
        (ip != IPAddress(0,0,0,0) || (patch && patch->ip != IPAddress(0,0,0,0)))
    );
}
