// =============================================================================
// osc_registry.h — Central owner of all OscScene and OscMessage objects
// =============================================================================
//
// The OscRegistry is a Meyer's singleton that owns every OscScene and
// OscMessage in fixed-size arrays.  All pointers returned by the registry
// point into these arrays and are stable for the device's lifetime.
//
// MEMORY MODEL (ESP32):
//   No heap allocation — capacity is fixed at compile time.  This avoids
//   fragmentation and gives deterministic memory usage.  Increase
//   MAX_OSC_SCENES / MAX_OSC_MESSAGES and recompile if you need more slots.
//
// THREAD SAFETY:
//   A FreeRTOS mutex (reg_mutex) protects all registry modifications.
//   Send tasks that only *read* messages do so under brief mutex holds.
//   The main loop (which processes incoming OSC commands) also takes the
//   mutex before modifying the registry.
//
// USAGE:
//   OscRegistry& reg = osc_registry();
//   OscMessage*  m   = reg.get_or_create_msg("accelX_out");
//   OscScene*    p   = reg.get_or_create_scene("mixer1");
//   p->add_msg(reg.msg_index(m));
// =============================================================================

#ifndef OSC_REGISTRY_H
#define OSC_REGISTRY_H

#include "osc_scene.h"
#include "osc_pattern.h"
#include "ori_tracker.h"
#include "string_pool.h"

class OscRegistry {
public:
    // --- Storage arrays (fixed, never reallocated) --------------------------
    OscScene   scenes[MAX_OSC_SCENES];
    uint16_t   scene_count = 0;

    OscMessage messages[MAX_OSC_MESSAGES];
    uint16_t   msg_count = 0;

    // Mutex for thread-safe access.  Initialised lazily on first use.
    SemaphoreHandle_t reg_mutex = nullptr;

    void ensure_mutex() {
        if (!reg_mutex) reg_mutex = xSemaphoreCreateMutex();
    }

    void lock()   { ensure_mutex(); xSemaphoreTake(reg_mutex, portMAX_DELAY); }
    void unlock() { xSemaphoreGive(reg_mutex); }

    // --- Lookup by name (case-insensitive) ----------------------------------

    OscScene* find_scene(const String& n) {
        String key = osc_lower_copy(osc_trim_copy(n));
        for (uint16_t i = 0; i < scene_count; i++) {
            if (osc_lower_copy(osc_trim_copy(scenes[i].name)) == key)
                return &scenes[i];
        }
        return nullptr;
    }

    OscMessage* find_msg(const String& n) {
        String key = osc_lower_copy(osc_trim_copy(n));
        for (uint16_t i = 0; i < msg_count; i++) {
            if (osc_lower_copy(osc_trim_copy(messages[i].name)) == key)
                return &messages[i];
        }
        return nullptr;
    }

    // --- Pattern-matching lookup (OSC 1.0 wildcards) -------------------------

    uint16_t find_msgs_matching(const char* pattern, OscMessage** out, uint16_t max_out) {
        uint16_t count = 0;
        for (uint16_t i = 0; i < msg_count && count < max_out; i++) {
            if (osc_pattern_match(pattern, messages[i].name.c_str()))
                out[count++] = &messages[i];
        }
        return count;
    }

    uint16_t find_scenes_matching(const char* pattern, OscScene** out, uint16_t max_out) {
        uint16_t count = 0;
        for (uint16_t i = 0; i < scene_count && count < max_out; i++) {
            if (osc_pattern_match(pattern, scenes[i].name.c_str()))
                out[count++] = &scenes[i];
        }
        return count;
    }

    // --- Index helpers (pointers ↔ indices) ----------------------------------

    int scene_index(const OscScene* p) const {
        if (!p) return -1;
        ptrdiff_t off = p - scenes;
        return (off >= 0 && off < scene_count) ? (int)off : -1;
    }

    int msg_index(const OscMessage* m) const {
        if (!m) return -1;
        ptrdiff_t off = m - messages;
        return (off >= 0 && off < msg_count) ? (int)off : -1;
    }

    // --- Create or find -----------------------------------------------------

    OscScene* get_or_create_scene(const String& n) {
        OscScene* found = find_scene(n);
        if (found) return found;
        if (scene_count >= MAX_OSC_SCENES) return nullptr;
        scenes[scene_count] = OscScene(n);
        return &scenes[scene_count++];
    }

    OscMessage* get_or_create_msg(const String& n) {
        OscMessage* found = find_msg(n);
        if (found) return found;
        if (msg_count >= MAX_OSC_MESSAGES) return nullptr;
        messages[msg_count] = OscMessage(n);
        return &messages[msg_count++];
    }

    // --- Merge (update existing or create, copying only "exist" fields) ------

    OscScene* update_scene(const OscScene& src) {
        OscScene* p = get_or_create_scene(src.name);
        if (!p) return nullptr;
        if (src.exist.ip)   { p->ip = src.ip;                   p->exist.ip   = true; }
        if (src.exist.port) { p->port = src.port;               p->exist.port = true; }
        if (src.exist.adr)  { p->osc_address = src.osc_address; p->exist.adr  = true; }
        if (src.exist.low)  { p->bounds[0] = src.bounds[0];     p->exist.low  = true; }
        if (src.exist.high) { p->bounds[1] = src.bounds[1];     p->exist.high = true; }
        return p;
    }

    OscMessage* update_msg(const OscMessage& src) {
        OscMessage* m = get_or_create_msg(src.name);
        if (!m) return nullptr;
        if (src.exist.ip)    { m->ip = src.ip;                   m->exist.ip    = true; }
        if (src.exist.port)  { m->port = src.port;               m->exist.port  = true; }
        if (src.exist.adr)   { m->osc_address = src.osc_address; m->exist.adr   = true; }
        for (uint8_t i = 0; i < src.scene_count; i++) m->add_scene(src.scenes[i]);
        if (src.exist.val)   { m->value_ptr = src.value_ptr;     m->exist.val   = true; }
        if (src.exist.low)   { m->bounds[0] = src.bounds[0];     m->exist.low   = true; }
        if (src.exist.high)  { m->bounds[1] = src.bounds[1];     m->exist.high  = true; }
        if (src.gate_mode != GATE_NONE) {
            m->gate_source = src.gate_source;
            m->gate_mode   = src.gate_mode;
            m->gate_lo     = src.gate_lo;
            m->gate_hi     = src.gate_hi;
        }
        return m;
    }

    // --- Delete (swap-and-shrink) -------------------------------------------

    /// Delete a scene by name.  Any messages referencing this scene have their
    /// scene pointer cleared.  Returns true if found and deleted.
    bool delete_scene(const String& n) {
        OscScene* p = find_scene(n);
        if (!p) return false;

        // Stop the task if running.
        if (p->task_handle) {
            vTaskDelete(p->task_handle);
            p->task_handle = nullptr;
        }

        int idx = scene_index(p);

        // Remove this scene from all messages that reference it.
        for (uint16_t i = 0; i < msg_count; i++) {
            messages[i].remove_scene(p);
        }

        // Swap with last and shrink.
        if (idx < (int)(scene_count - 1)) {
            scenes[idx] = scenes[scene_count - 1];

            // Fix scene pointers in all messages referencing the moved scene.
            OscScene* moved = &scenes[idx];
            OscScene* old_ptr = &scenes[scene_count - 1];
            for (uint16_t i = 0; i < msg_count; i++) {
                for (uint8_t si = 0; si < messages[i].scene_count; si++) {
                    if (messages[i].scenes[si] == old_ptr) {
                        messages[i].scenes[si] = moved;
                    }
                }
            }
            // Fix msg_indices inside other scenes that pointed at old_idx.
            for (uint16_t pi = 0; pi < scene_count - 1; pi++) {
                for (uint8_t mi = 0; mi < scenes[pi].msg_count; mi++) {
                    // No msg_indices reference scene indices, they reference message indices.
                    // So nothing to fix here for scenes.
                }
            }
        }
        scene_count--;
        return true;
    }

    /// Delete a message by name.  Removes it from any scene that references it.
    /// Returns true if found and deleted.
    bool delete_msg(const String& n) {
        OscMessage* m = find_msg(n);
        if (!m) return false;

        int idx = msg_index(m);

        // Remove from any scenes that list this message index.
        for (uint16_t pi = 0; pi < scene_count; pi++) {
            scenes[pi].remove_msg(idx);
        }

        // Swap with last and shrink.
        int last = msg_count - 1;
        if (idx < last) {
            messages[idx] = messages[last];

            // Update all scene msg_indices that pointed at `last` to point at `idx`.
            for (uint16_t pi = 0; pi < scene_count; pi++) {
                for (uint8_t mi = 0; mi < scenes[pi].msg_count; mi++) {
                    if (scenes[pi].msg_indices[mi] == last) {
                        scenes[pi].msg_indices[mi] = idx;
                    }
                }
            }

            // Update the scene pointer if the moved message references a scene.
            // (Scene pointers are stable since scenes don't move here.)
        }
        msg_count--;
        return true;
    }

    // --- Listing helpers ----------------------------------------------------

    uint16_t count_scenes() const { return scene_count; }
    uint16_t count_msgs()    const { return msg_count; }
};

// ---------------------------------------------------------------------------
// Global registry accessor (Meyer's singleton, created on first call).
// ---------------------------------------------------------------------------

static inline OscRegistry& osc_registry() {
    static OscRegistry instance;
    return instance;
}

// =============================================================================
// OscMessage method implementations (need OscScene and OscRegistry complete)
// =============================================================================

/// True if there is enough information to actually transmit an OSC packet:
/// a value to send, and a resolvable destination (ip, port, address).
inline bool OscMessage::sendable() const {
    bool has_val = (value_ptr != nullptr) || (gate_mode == GATE_TOGGLE);
    bool has_ip  = exist.ip;
    bool has_port = exist.port;
    bool has_adr = exist.adr;
    // Check if any parent scene can provide missing fields.
    for (uint8_t i = 0; i < scene_count && !(has_ip && has_port && has_adr); i++) {
        if (scenes[i]) {
            if (!has_ip   && scenes[i]->exist.ip)   has_ip   = true;
            if (!has_port && scenes[i]->exist.port)  has_port = true;
            if (!has_adr  && scenes[i]->exist.adr)   has_adr  = true;
        }
    }
    return has_val && has_ip && has_port && has_adr;
}

/// Parse a CSV config string and populate this message's fields.
///
/// Format: "key:value, key:value, ..."
///   Direct keys:  name, ip, port, adr/addr/address, scene, value, low/min/lo,
///                 high/max/hi, enabled
///                 period — accepted but ignored (scene-level field, handled by caller)
///   Reference keys (use '<' separator):  ip<refName, port<refName, etc.
///   Special:  default<refName / all<refName  copies all set fields from a
///             registered scene or message as fallback values.
///
/// Returns false with *error filled in on parse failure.
inline bool OscMessage::from_config_str(const String& config, String* error) {
    // Reset parseable fields.
    exist              = ExistFlags{};
    ip                 = IPAddress(0, 0, 0, 0);
    port               = 0;
    osc_address        = "";
    clear_scenes();
    value_ptr          = nullptr;
    string_value_ptr   = nullptr;
    bounds[0]          = 0.0f;
    bounds[1]  = 1.0f;
    gate_source = "";
    gate_mode   = GATE_NONE;
    gate_lo     = NAN;
    gate_hi     = NAN;

    String input = config;
    input.trim();
    if (input.length() == 0) {
        if (error) *error = "Empty config string";
        return false;
    }

    size_t start = 0;
    while (start < input.length()) {
        // Find the next comma-separated token.
        int comma = input.indexOf(',', start);
        String token = (comma < 0) ? input.substring(start) : input.substring(start, comma);
        token = osc_trim_copy(token);
        start = (comma < 0) ? input.length() : (size_t)(comma + 1);

        if (token.length() == 0) continue;

        // Determine separator: ':' = direct value, '<' = registry reference.
        int colon = token.indexOf(':');
        int angle = token.indexOf('<');

        bool is_ref = false;
        int  sep    = -1;
        if (colon >= 0 && (angle < 0 || colon <= angle)) {
            sep    = colon;
            is_ref = false;
        } else if (angle >= 0) {
            sep    = angle;
            is_ref = true;
        } else {
            if (error) *error = "Missing ':' or '<' in token: " + token;
            return false;
        }

        String key   = osc_lower_copy(osc_trim_copy(token.substring(0, sep)));
        String value = osc_trim_copy(token.substring(sep + 1));

        // ----- Reference mode: value is a registered name to look up --------
        if (is_ref) {
            OscRegistry& reg = osc_registry();
            OscScene*   ref_p = reg.find_scene(value);
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
            } else if (key == "scene") {
                OscScene* sp = ref_p ? ref_p : reg.get_or_create_scene(value);
                if (sp) add_scene(sp);
            } else if (key == "value") {
                if (ref_m && ref_m->exist.val) { value_ptr = ref_m->value_ptr; exist.val = true; }
                else { if (error) *error = "Ref '" + value + "' has no value"; return false; }
            } else if (key == "low" || key == "min" || key == "lo") {
                if (ref_m && ref_m->exist.low) { bounds[0] = ref_m->bounds[0]; exist.low = true; }
                else { if (error) *error = "Ref '" + value + "' has no low"; return false; }
            } else if (key == "high" || key == "max" || key == "hi") {
                if (ref_m && ref_m->exist.high) { bounds[1] = ref_m->bounds[1]; exist.high = true; }
                else { if (error) *error = "Ref '" + value + "' has no high"; return false; }
            } else if (key == "orionly" || key == "ori_only") {
                // Backward compat: ori_only → gate(ori:X, ONLY)
                if (ref_m && ref_m->gate_mode != GATE_NONE) {
                    gate_source = ref_m->gate_source; gate_mode = ref_m->gate_mode;
                    gate_lo = ref_m->gate_lo; gate_hi = ref_m->gate_hi;
                } else { if (error) *error = "Ref '" + value + "' has no gate"; return false; }
            } else if (key == "orinot" || key == "ori_not") {
                if (ref_m && ref_m->gate_mode != GATE_NONE) {
                    gate_source = ref_m->gate_source; gate_mode = ref_m->gate_mode;
                    gate_lo = ref_m->gate_lo; gate_hi = ref_m->gate_hi;
                } else { if (error) *error = "Ref '" + value + "' has no gate"; return false; }
            } else if (key == "ternori") {
                if (ref_m && ref_m->gate_mode != GATE_NONE) {
                    gate_source = ref_m->gate_source; gate_mode = ref_m->gate_mode;
                    gate_lo = ref_m->gate_lo; gate_hi = ref_m->gate_hi;
                } else { if (error) *error = "Ref '" + value + "' has no gate"; return false; }
            } else if (key == "gate_src" || key == "gate_source") {
                if (ref_m && ref_m->gate_source.length() > 0) { gate_source = ref_m->gate_source; }
                else { if (error) *error = "Ref '" + value + "' has no gate_src"; return false; }
            } else if (key == "gate_mode") {
                if (ref_m && ref_m->gate_mode != GATE_NONE) { gate_mode = ref_m->gate_mode; }
                else { if (error) *error = "Ref '" + value + "' has no gate_mode"; return false; }
            } else if (key == "default" || key == "all") {
                if (!ref_p && !ref_m) {
                    if (error) *error = "default/all: no object named '" + value + "'";
                    return false;
                }
                if (ref_p) {
                    if (ref_p->exist.ip   && !exist.ip)   { ip = ref_p->ip;                   exist.ip   = true; }
                    if (ref_p->exist.port && !exist.port)  { port = ref_p->port;               exist.port = true; }
                    if (ref_p->exist.adr  && !exist.adr)   { osc_address = ref_p->osc_address; exist.adr  = true; }
                }
                if (ref_m) {
                    if (ref_m->exist.ip    && !exist.ip)    { ip = ref_m->ip;                   exist.ip    = true; }
                    if (ref_m->exist.port  && !exist.port)  { port = ref_m->port;               exist.port  = true; }
                    if (ref_m->exist.adr   && !exist.adr)   { osc_address = ref_m->osc_address; exist.adr   = true; }
                    for (uint8_t si = 0; si < ref_m->scene_count; si++) add_scene(ref_m->scenes[si]);
                    if (ref_m->exist.val   && !exist.val)   { value_ptr = ref_m->value_ptr;     exist.val   = true; }
                    if (ref_m->exist.low   && !exist.low)   { bounds[0] = ref_m->bounds[0];     exist.low   = true; }
                    if (ref_m->exist.high  && !exist.high)  { bounds[1] = ref_m->bounds[1];     exist.high  = true; }
                    if (ref_m->gate_mode != GATE_NONE && gate_mode == GATE_NONE) {
                        gate_source = ref_m->gate_source; gate_mode = ref_m->gate_mode;
                        gate_lo = ref_m->gate_lo; gate_hi = ref_m->gate_hi;
                    }
                }
            } else {
                if (error) *error = "Unknown key: " + key;
                return false;
            }
            continue;
        }

        // ----- Direct mode: value is a literal ------------------------------
        if (key == "name") {
            name = value;
            exist.name = true;
        } else if (key == "ip") {
            IPAddress parsed;
            if (!parsed.fromString(value)) {
                if (error) *error = "Invalid IP: " + value;
                return false;
            }
            ip = parsed;
            exist.ip = true;
        } else if (key == "port") {
            long p = value.toInt();
            if (p < 1 || p > 65535) {
                if (error) *error = "Invalid port: " + value;
                return false;
            }
            port = (unsigned int)p;
            exist.port = true;
        } else if (key == "adr" || key == "addr" || key == "address") {
            osc_address = value;
            exist.adr = true;
        } else if (key == "scene") {
            // Support multiple scenes separated by '+': "scene:mixer1+mixer2"
            OscRegistry& reg = osc_registry();
            int s2 = 0;
            while (s2 < (int)value.length()) {
                int plus = value.indexOf('+', s2);
                String sn = (plus < 0) ? value.substring(s2) : value.substring(s2, plus);
                sn = osc_trim_copy(sn);
                s2 = (plus < 0) ? value.length() : plus + 1;
                if (sn.length() == 0) continue;
                OscScene* sp = reg.find_scene(sn);
                if (!sp) sp = reg.get_or_create_scene(sn);
                if (sp) add_scene(sp);
            }
        } else if (key == "value" || key == "val") {
            // Check for string pool reference first (str1, str2, ...)
            String lv = value; lv.trim(); lv.toLowerCase();
            if (lv.startsWith("str") && lv.length() > 3 && isDigit(lv[3])) {
                String* sp = string_pool().ptr_from_name(value);
                if (sp) {
                    string_value_ptr = sp;
                    value_ptr = nullptr;
                    exist.val = true;
                } else {
                    if (error) *error = "String not found: " + value;
                    return false;
                }
            } else {
                int vi = data_stream_index_from_name(value);
                if (vi < 0 || vi >= NUM_DATA_STREAMS) {
                    if (error) *error = "Unknown value: " + value;
                    return false;
                }
                value_ptr = &data_streams[vi];
                string_value_ptr = nullptr;
                exist.val = true;
            }
        } else if (key == "low" || key == "min" || key == "lo") {
            bounds[0] = value.toFloat();
            exist.low = true;
        } else if (key == "high" || key == "max" || key == "hi") {
            bounds[1] = value.toFloat();
            exist.high = true;
        } else if (key == "enabled") {
            String v = osc_lower_copy(value);
            enabled = (v == "true" || v == "1" || v == "yes" || v == "on");
        } else if (key == "orionly" || key == "ori_only") {
            // Backward compat: ori_only:X → gate(ori:X, ONLY)
            gate_source = "ori:" + value;
            gate_mode   = GATE_ONLY;
            { OriTracker& ot = ori_tracker(); if (ot.find(value) < 0) ot.register_ori(value, 255, 255, 255); }
        } else if (key == "orinot" || key == "ori_not") {
            gate_source = "ori:" + value;
            gate_mode   = GATE_NOT;
            { OriTracker& ot = ori_tracker(); if (ot.find(value) < 0) ot.register_ori(value, 255, 255, 255); }
        } else if (key == "ternori") {
            gate_source = "ori:" + value;
            gate_mode   = GATE_TOGGLE;
            { OriTracker& ot = ori_tracker(); if (ot.find(value) < 0) ot.register_ori(value, 255, 255, 255); }
        } else if (key == "gate_src" || key == "gate_source") {
            gate_source = value;
            // Auto-register ori names
            if (value.startsWith("ori:")) {
                String ori_name = value.substring(4);
                OriTracker& ot = ori_tracker();
                if (ot.find(ori_name) < 0) ot.register_ori(ori_name, 255, 255, 255);
            }
        } else if (key == "gate_mode") {
            String v = osc_lower_copy(value);
            if      (v == "only"   || v == "1") gate_mode = GATE_ONLY;
            else if (v == "not"    || v == "2") gate_mode = GATE_NOT;
            else if (v == "toggle" || v == "3") gate_mode = GATE_TOGGLE;
            else if (v == "none"   || v == "0") gate_mode = GATE_NONE;
            else { if (error) *error = "Unknown gate_mode: " + value; return false; }
        } else if (key == "gate_lo") {
            gate_lo = value.toFloat();
        } else if (key == "gate_hi") {
            gate_hi = value.toFloat();
        } else if (key == "period") {
            // Recognised but handled outside from_config_str (scene-level field).
        } else {
            if (error) *error = "Unknown key: " + key;
            return false;
        }
    }

    return true;
}

/// Build a human-readable info string describing this message.
inline String OscMessage::to_info_string(bool verbose) const {
    String s = name;
    if (!verbose) return s;

    s += " [";
    s += enabled ? "ON" : "OFF";
    s += "]";

    if (exist.ip)   { s += " ip:" + ip.toString(); }
    if (exist.port) { s += " port:" + String(port); }
    if (exist.adr)  { s += " adr:" + osc_address; }
    if (exist.val) {
        if (string_value_ptr) {
            // Find pool index for display.
            for (uint8_t _si = 0; _si < string_pool().count; _si++) {
                if (&string_pool().values[_si] == string_value_ptr) {
                    s += " val:" + StringPool::name_for_index(_si);
                    break;
                }
            }
        } else {
            int idx = data_stream_index_from_ptr(value_ptr);
            s += " val:" + (idx >= 0 ? data_stream_name(idx) : String("custom"));
        }
    }
    if (exist.low)  { s += " low:" + String(bounds[0], 2); }
    if (exist.high) { s += " high:" + String(bounds[1], 2); }
    if (scene_count > 0) {
        s += " scene:";
        for (uint8_t i = 0; i < scene_count; i++) {
            if (i > 0) s += "+";
            s += scenes[i] ? scenes[i]->name : "?";
        }
    }
    if (gate_mode != GATE_NONE) {
        s += " gate_src:" + gate_source;
        s += " gate_mode:";
        s += (gate_mode == GATE_ONLY) ? "only" : (gate_mode == GATE_NOT) ? "not" : "toggle";
        if (!isnan(gate_lo)) { s += " gate_lo:" + String(gate_lo, 4); }
        if (!isnan(gate_hi)) { s += " gate_hi:" + String(gate_hi, 4); }
    }

    return s;
}

/// Build a human-readable info string for this scene.
inline String OscScene::to_info_string(bool verbose) const {
    String s = name;
    if (!verbose) return s;

    s += " [";
    s += enabled ? "RUNNING" : "STOPPED";
    s += ", " + String(send_period_ms) + "ms";
    s += ", " + String(msg_count) + " msgs";
    s += "]";

    if (exist.ip)   { s += " ip:" + ip.toString(); }
    if (exist.port) { s += " port:" + String(port); }
    if (exist.adr)  { s += " adr:" + osc_address; }
    if (exist.low)  { s += " low:" + String(bounds[0], 2); }
    if (exist.high) { s += " high:" + String(bounds[1], 2); }

    s += " adr_mode:" + String(address_mode_label(address_mode));

    if (overrides.ip || overrides.port || overrides.adr
        || overrides.low || overrides.high) {
        s += " override:";
        if (overrides.ip)   s += "ip,";
        if (overrides.port) s += "port,";
        if (overrides.adr)  s += "adr,";
        if (overrides.low)  s += "low,";
        if (overrides.high) s += "high,";
        s.remove(s.length() - 1); // trailing comma
    }

    return s;
}

#endif // OSC_REGISTRY_H
