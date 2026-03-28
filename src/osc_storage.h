// =============================================================================
// osc_storage.h — Non-volatile storage (NVS) for OscMessages and OscScenes
// =============================================================================
//
// This file provides functions to save and load the entire OSC registry
// (messages and scenes) to/from the ESP32's NVS flash using the Preferences
// library.  This lets configurations survive power cycles.
//
// STORAGE FORMAT:
//   Each message and scene is serialised to a self-describing CSV string
//   (the same format used by from_config_str) and stored under a short key.
//
//   Namespace: "osc_store"
//   Keys:
//     "m_count"   — uint16_t, number of saved messages
//     "m_0" .. "m_255"  — String, serialised message
//     "p_count"   — uint16_t, number of saved scenes
//     "p_0" .. "p_63"   — String, serialised scene
//
// SAVE ORDER:
//   Scenes are saved first, then messages.  This does not matter for save,
//   but on load, scenes must be created before messages so that messages
//   can resolve their scene references.
//
// COMMANDS (handled in osc_commands.h):
//   /annieData{dev}/save           — save all scenes and messages
//   /annieData{dev}/load           — load all scenes and messages
//   /annieData{dev}/save/all       — same as /save
//   /annieData{dev}/load/all       — same as /load
//   /annieData{dev}/save/msg       — save one message (payload: name)
//   /annieData{dev}/save/scene     — save one scene (payload: name)
//   /annieData{dev}/load/msg       — load one message (payload: name)
//   /annieData{dev}/load/scene     — load one scene (payload: name)
//   /annieData{dev}/nvs/clear      — erase all saved OSC data
// =============================================================================

#ifndef OSC_STORAGE_H
#define OSC_STORAGE_H

#include <Preferences.h>
#include "osc_registry.h"
#include "ori_tracker.h"

// ---------------------------------------------------------------------------
// Serialisation helpers — convert objects to/from storable strings
// ---------------------------------------------------------------------------

/// Serialise an OscMessage to a string that can be stored in NVS and later
/// parsed back via from_config_str() plus some extra fields.
///
/// Format: "name:xxx, ip:x.x.x.x, port:N, adr:/xxx, value:sensorName,
///          low:N.NN, high:N.NN, enabled:true, scene:sceneName"
/// Only fields with exist flags set are included.
static inline String msg_to_save_string(const OscMessage& m) {
    String s;

    if (m.exist.name) {
        s += "name:" + m.name;
    }
    if (m.exist.ip) {
        if (s.length() > 0) s += ", ";
        s += "ip:" + m.ip.toString();
    }
    if (m.exist.port) {
        if (s.length() > 0) s += ", ";
        s += "port:" + String(m.port);
    }
    if (m.exist.adr) {
        if (s.length() > 0) s += ", ";
        s += "adr:" + m.osc_address;
    }
    if (m.exist.val) {
        int idx = data_stream_index_from_ptr(m.value_ptr);
        if (idx >= 0) {
            if (s.length() > 0) s += ", ";
            s += "value:" + data_stream_name(idx);
        }
    }
    if (m.exist.low) {
        if (s.length() > 0) s += ", ";
        s += "low:" + String(m.bounds[0], 4);
    }
    if (m.exist.high) {
        if (s.length() > 0) s += ", ";
        s += "high:" + String(m.bounds[1], 4);
    }
    // enabled (always store it so we restore the exact state)
    if (s.length() > 0) s += ", ";
    s += String("enabled:") + (m.enabled ? "true" : "false");

    if (m.exist.scene && m.scene) {
        if (s.length() > 0) s += ", ";
        s += "scene:" + m.scene->name;
    }

    // Ori-conditional fields (ab7 only).
    if (m.ori_only.length() > 0) {
        if (s.length() > 0) s += ", ";
        s += "ori_only:" + m.ori_only;
    }
    if (m.ori_not.length() > 0) {
        if (s.length() > 0) s += ", ";
        s += "ori_not:" + m.ori_not;
    }
    if (m.ternori.length() > 0) {
        if (s.length() > 0) s += ", ";
        s += "ternori:" + m.ternori;
    }

    return s;
}

/// Serialise an OscScene to a string for NVS storage.
///
/// Format: "name:xxx, ip:x.x.x.x, port:N, adr:/xxx, low:N, high:N,
///          period:N, adrmode:prepend, override:ip+port+adr+low+high,
///          msgs:msg1+msg2+msg3"
///
/// The "override" and "msgs" fields use '+' as an internal separator since
/// ',' and ':' are already used by the CSV format.
static inline String scene_to_save_string(const OscScene& p) {
    OscRegistry& reg = osc_registry();
    String s;

    if (p.exist.name) {
        s += "name:" + p.name;
    }
    if (p.exist.ip) {
        if (s.length() > 0) s += ", ";
        s += "ip:" + p.ip.toString();
    }
    if (p.exist.port) {
        if (s.length() > 0) s += ", ";
        s += "port:" + String(p.port);
    }
    if (p.exist.adr) {
        if (s.length() > 0) s += ", ";
        s += "adr:" + p.osc_address;
    }
    if (p.exist.low) {
        if (s.length() > 0) s += ", ";
        s += "low:" + String(p.bounds[0], 4);
    }
    if (p.exist.high) {
        if (s.length() > 0) s += ", ";
        s += "high:" + String(p.bounds[1], 4);
    }

    // Period (always store)
    if (s.length() > 0) s += ", ";
    s += "period:" + String(p.send_period_ms);

    // Address mode
    if (s.length() > 0) s += ", ";
    s += "adrmode:";
    s += address_mode_label(p.address_mode);

    // Override flags — stored as "override:ip+port+adr+low+high" (only set flags)
    {
        String ov;
        if (p.overrides.ip)   { if (ov.length() > 0) ov += "+"; ov += "ip"; }
        if (p.overrides.port) { if (ov.length() > 0) ov += "+"; ov += "port"; }
        if (p.overrides.adr)  { if (ov.length() > 0) ov += "+"; ov += "adr"; }
        if (p.overrides.low)  { if (ov.length() > 0) ov += "+"; ov += "low"; }
        if (p.overrides.high) { if (ov.length() > 0) ov += "+"; ov += "high"; }
        if (s.length() > 0) s += ", ";
        s += "override:" + (ov.length() > 0 ? ov : String("none"));
    }

    // Message names — stored as "msgs:name1+name2+name3"
    {
        String ml;
        for (uint8_t i = 0; i < p.msg_count; i++) {
            int mi = p.msg_indices[i];
            if (mi >= 0 && mi < (int)reg.msg_count) {
                if (ml.length() > 0) ml += "+";
                ml += reg.messages[mi].name;
            }
        }
        if (ml.length() > 0) {
            if (s.length() > 0) s += ", ";
            s += "msgs:" + ml;
        }
    }

    return s;
}

/// Restore an OscScene from a saved string.  This handles the extra fields
/// (period, adrmode, override, msgs) that from_config_str() does not know
/// about.  It first uses from_config_str() for the standard fields
/// (ip, port, adr, low, high), then manually parses the rest.
///
/// Assumes the scene has already been created in the registry by name.
static inline void scene_from_save_string(OscScene* p, const String& saved) {
    if (!p) return;
    OscRegistry& reg = osc_registry();

    // Use from_config_str on a temporary OscMessage to extract standard fields.
    OscMessage tmp;
    String err;
    tmp.from_config_str(saved, &err);
    // Copy the standard fields into the scene.
    if (tmp.exist.ip)   { p->ip = tmp.ip;                   p->exist.ip   = true; }
    if (tmp.exist.port) { p->port = tmp.port;               p->exist.port = true; }
    if (tmp.exist.adr)  { p->osc_address = tmp.osc_address; p->exist.adr  = true; }
    if (tmp.exist.low)  { p->bounds[0] = tmp.bounds[0];     p->exist.low  = true; }
    if (tmp.exist.high) { p->bounds[1] = tmp.bounds[1];     p->exist.high = true; }

    // Now manually parse the scene-specific fields from the raw CSV.
    String input = saved;
    input.trim();
    size_t start = 0;
    while (start < input.length()) {
        int comma = input.indexOf(',', start);
        String token = (comma < 0) ? input.substring(start)
                                   : input.substring(start, comma);
        token.trim();
        start = (comma < 0) ? input.length() : (size_t)(comma + 1);
        if (token.length() == 0) continue;

        int colon = token.indexOf(':');
        if (colon < 0) continue;

        String key   = token.substring(0, colon);
        key.trim(); key.toLowerCase();
        String value = token.substring(colon + 1);
        value.trim();

        if (key == "period") {
            int ms = value.toInt();
            if (ms > 0) p->send_period_ms = clamp_scene_period_ms(ms);
        } else if (key == "adrmode") {
            p->address_mode = address_mode_from_string(value);
        } else if (key == "override") {
            // Reset all overrides, then set from "ip+port+adr+low+high".
            p->overrides.ip = p->overrides.port = p->overrides.adr = false;
            p->overrides.low = p->overrides.high = false;
            if (value != "none") {
                int s2 = 0;
                while (s2 < (int)value.length()) {
                    int plus = value.indexOf('+', s2);
                    String f = (plus < 0) ? value.substring(s2)
                                          : value.substring(s2, plus);
                    f.trim(); f.toLowerCase();
                    s2 = (plus < 0) ? value.length() : plus + 1;
                    if (f == "ip")   p->overrides.ip   = true;
                    if (f == "port") p->overrides.port = true;
                    if (f == "adr")  p->overrides.adr  = true;
                    if (f == "low")  p->overrides.low  = true;
                    if (f == "high") p->overrides.high = true;
                }
            }
        } else if (key == "msgs") {
            // Parse "name1+name2+name3" and add to scene.
            int s2 = 0;
            while (s2 < (int)value.length()) {
                int plus = value.indexOf('+', s2);
                String mname = (plus < 0) ? value.substring(s2)
                                          : value.substring(s2, plus);
                mname.trim();
                s2 = (plus < 0) ? value.length() : plus + 1;
                if (mname.length() == 0) continue;
                OscMessage* m = reg.find_msg(mname);
                if (m) {
                    int mi = reg.msg_index(m);
                    p->add_msg(mi);
                    m->scene = p;
                    m->exist.scene = true;
                }
                // If the message doesn't exist yet, it will be loaded later
                // and linked in a second pass.
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Save / load all — writes/reads the entire registry to/from NVS
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Ori NVS persistence
// ---------------------------------------------------------------------------
// Namespace: "ori_store"
// Keys: "o_count", "o_0" .. "o_31"
// Format per ori:
//   "name:xxx,sc:N,r:R,g:G,b:B,qi:F,qj:F,qk:F,qr:F,hw:F"
//   sc (sample_count) == 0 means pre-registered, not yet sampled.

/// Serialise one SavedOri to a storable string.
static inline String ori_to_save_string(const SavedOri& o) {
    String s;
    s  = "name:" + o.name;
    s += ",sc:"  + String(o.sample_count);
    s += ",r:"   + String(o.color_r);
    s += ",g:"   + String(o.color_g);
    s += ",b:"   + String(o.color_b);
    s += ",qi:"  + String(o.qi, 6);
    s += ",qj:"  + String(o.qj, 6);
    s += ",qk:"  + String(o.qk, 6);
    s += ",qr:"  + String(o.qr, 6);
    s += ",hw:"  + String(o.angular_half_width, 6);
    return s;
}

/// Parse an ori from a save string back into the tracker.
static inline void ori_from_save_string(OriTracker& ot, const String& s) {
    if (s.length() == 0) return;

    auto extract = [&](const char* key) -> String {
        String k = String(key) + ":";
        int start = s.indexOf(k);
        if (start < 0) return "";
        start += k.length();
        int end = s.indexOf(',', start);
        return (end < 0) ? s.substring(start) : s.substring(start, end);
    };

    String name = extract("name");
    if (name.length() == 0) return;

    uint8_t sc = (uint8_t)extract("sc").toInt();
    uint8_t r  = (uint8_t)extract("r").toInt();
    uint8_t g  = (uint8_t)extract("g").toInt();
    uint8_t b  = (uint8_t)extract("b").toInt();

    if (sc == 0) {
        // Pre-registered slot — restore name + color only.
        ot.register_ori(name, r, g, b);
        return;
    }

    float qi = extract("qi").toFloat();
    float qj = extract("qj").toFloat();
    float qk = extract("qk").toFloat();
    float qr = extract("qr").toFloat();
    float hw = extract("hw").toFloat();

    int idx = ot.save(name, qi, qj, qk, qr);
    if (idx < 0) return;

    // Restore exact sample count and half-width (save() incremented sc to 1).
    ot.oris[idx].sample_count       = sc;
    ot.oris[idx].angular_half_width = hw;
    ot.oris[idx].color_r = r;
    ot.oris[idx].color_g = g;
    ot.oris[idx].color_b = b;
}

/// Save the ori tracker to NVS.  Returns number of oris saved.
static inline int nvs_save_oris() {
    OriTracker& ot = ori_tracker();
    Preferences prefs;
    prefs.begin("ori_store", false);
    prefs.clear();

    uint8_t saved = 0;
    for (uint8_t i = 0; i < ot.ori_count; i++) {
        if (!ot.oris[i].used) continue;
        String key = "o_" + String(saved);
        prefs.putString(key.c_str(), ori_to_save_string(ot.oris[i]));
        saved++;
    }
    prefs.putUChar("o_count", saved);
    // Also persist global settings.
    prefs.putFloat("o_thr",  ot.motion_threshold);
    prefs.putFloat("o_tol",  ot.ori_tolerance);
    prefs.putBool ("o_str",  ot.strict_matching);
    prefs.end();
    return saved;
}

/// Load oris from NVS into the tracker.  Clears existing oris first.
/// Returns number of oris loaded.
static inline int nvs_load_oris() {
    OriTracker& ot = ori_tracker();
    Preferences prefs;
    prefs.begin("ori_store", true);  // read-only

    uint8_t count = prefs.getUChar("o_count", 0);
    if (count == 0) { prefs.end(); return 0; }

    ot.clear();

    int loaded = 0;
    for (uint8_t i = 0; i < count && i < MAX_ORIS; i++) {
        String key = "o_" + String(i);
        String saved = prefs.getString(key.c_str(), "");
        if (saved.length() == 0) continue;
        ori_from_save_string(ot, saved);
        loaded++;
    }
    // Restore global settings.
    ot.motion_threshold = prefs.getFloat("o_thr", 1.5f);
    ot.ori_tolerance    = prefs.getFloat("o_tol", 10.0f);
    ot.strict_matching  = prefs.getBool ("o_str", false);
    prefs.end();
    return loaded;
}

/// Clear all ori data from NVS.
static inline void nvs_clear_oris() {
    Preferences prefs;
    prefs.begin("ori_store", false);
    prefs.clear();
    prefs.end();
}

/// Save all scenes and messages to NVS.  Returns the number of objects saved.
static inline int nvs_save_all() {
    OscRegistry& reg = osc_registry();
    Preferences prefs;
    prefs.begin("osc_store", false);  // read-write
    prefs.clear();  // wipe previous data

    // Save scenes.
    prefs.putUShort("p_count", reg.scene_count);
    for (uint16_t i = 0; i < reg.scene_count; i++) {
        String key = "p_" + String(i);
        String val = scene_to_save_string(reg.scenes[i]);
        prefs.putString(key.c_str(), val);
    }

    // Save messages.
    prefs.putUShort("m_count", reg.msg_count);
    for (uint16_t i = 0; i < reg.msg_count; i++) {
        String key = "m_" + String(i);
        String val = msg_to_save_string(reg.messages[i]);
        prefs.putString(key.c_str(), val);
    }

    prefs.end();
    // Also save oris.
    nvs_save_oris();
    return (int)(reg.scene_count + reg.msg_count);
}

/// Load all scenes and messages from NVS.  Clears the current registry
/// before loading.  Returns the number of objects loaded.
static inline int nvs_load_all() {
    OscRegistry& reg = osc_registry();
    Preferences prefs;
    prefs.begin("osc_store", true);  // read-only

    uint16_t p_count = prefs.getUShort("p_count", 0);
    uint16_t m_count = prefs.getUShort("m_count", 0);

    if (p_count == 0 && m_count == 0) {
        prefs.end();
        return 0;
    }

    // Stop all running scene tasks before clearing.
    for (uint16_t i = 0; i < reg.scene_count; i++) {
        if (reg.scenes[i].task_handle) {
            vTaskDelete(reg.scenes[i].task_handle);
            reg.scenes[i].task_handle = nullptr;
        }
    }

    // Reset the registry.
    reg.scene_count = 0;
    reg.msg_count   = 0;

    // --- First pass: create scenes with basic fields -----------------------
    // We create them first so messages can reference them by name.
    for (uint16_t i = 0; i < p_count && i < MAX_OSC_SCENES; i++) {
        String key = "p_" + String(i);
        String saved = prefs.getString(key.c_str(), "");
        if (saved.length() == 0) continue;

        // Extract the name from the saved string.
        String pname;
        int ni = saved.indexOf("name:");
        if (ni >= 0) {
            int end = saved.indexOf(',', ni);
            pname = (end < 0) ? saved.substring(ni + 5)
                              : saved.substring(ni + 5, end);
            pname.trim();
        }
        if (pname.length() == 0) {
            pname = "scene_" + String(i);  // fallback name
        }

        OscScene* p = reg.get_or_create_scene(pname);
        if (p) {
            scene_from_save_string(p, saved);
        }
    }

    // --- Second pass: create messages ---------------------------------------
    for (uint16_t i = 0; i < m_count && i < MAX_OSC_MESSAGES; i++) {
        String key = "m_" + String(i);
        String saved = prefs.getString(key.c_str(), "");
        if (saved.length() == 0) continue;

        // Extract the name from the saved string.
        String mname;
        int ni = saved.indexOf("name:");
        if (ni >= 0) {
            int end = saved.indexOf(',', ni);
            mname = (end < 0) ? saved.substring(ni + 5)
                              : saved.substring(ni + 5, end);
            mname.trim();
        }
        if (mname.length() == 0) {
            mname = "msg_" + String(i);  // fallback name
        }

        OscMessage* m = reg.get_or_create_msg(mname);
        if (!m) continue;

        // Parse the config string to populate the message.
        OscMessage parsed;
        String err;
        if (parsed.from_config_str(saved, &err)) {
            *m = parsed * (*m);  // merge: parsed takes priority
            m->name = mname;
            m->exist.name = true;
        }

        // Restore the enabled flag manually since from_config_str doesn't
        // set it in the exist flags.
        int en_idx = saved.indexOf("enabled:");
        if (en_idx >= 0) {
            int end = saved.indexOf(',', en_idx);
            String en_val = (end < 0) ? saved.substring(en_idx + 8)
                                      : saved.substring(en_idx + 8, end);
            en_val.trim();
            en_val.toLowerCase();
            m->enabled = (en_val == "true" || en_val == "1" || en_val == "yes");
        }

        // If the message references a scene, add it to the scene's msg list.
        if (m->exist.scene && m->scene) {
            int mi = reg.msg_index(m);
            m->scene->add_msg(mi);
        }
    }

    // --- Third pass: re-link scene message lists ----------------------------
    // Some scenes may have had "msgs:..." that referenced messages not yet
    // loaded during the first pass.  Re-parse the scene strings to fix.
    for (uint16_t i = 0; i < p_count && i < MAX_OSC_SCENES; i++) {
        String key = "p_" + String(i);
        String saved = prefs.getString(key.c_str(), "");
        if (saved.length() == 0) continue;

        // Find the msgs field.
        int msgs_idx = saved.indexOf("msgs:");
        if (msgs_idx < 0) continue;

        // Extract name.
        String pname;
        int ni = saved.indexOf("name:");
        if (ni >= 0) {
            int end = saved.indexOf(',', ni);
            pname = (end < 0) ? saved.substring(ni + 5)
                              : saved.substring(ni + 5, end);
            pname.trim();
        }

        OscScene* p = reg.find_scene(pname);
        if (!p) continue;

        // Parse msgs field.
        int end = saved.indexOf(',', msgs_idx);
        String msgs_val = (end < 0) ? saved.substring(msgs_idx + 5)
                                    : saved.substring(msgs_idx + 5, end);
        msgs_val.trim();

        int s2 = 0;
        while (s2 < (int)msgs_val.length()) {
            int plus = msgs_val.indexOf('+', s2);
            String mname = (plus < 0) ? msgs_val.substring(s2)
                                      : msgs_val.substring(s2, plus);
            mname.trim();
            s2 = (plus < 0) ? msgs_val.length() : plus + 1;
            if (mname.length() == 0) continue;
            OscMessage* m = reg.find_msg(mname);
            if (m) {
                int mi = reg.msg_index(m);
                p->add_msg(mi);
                m->scene = p;
                m->exist.scene = true;
            }
        }
    }

    prefs.end();
    // Also load oris.
    nvs_load_oris();
    return (int)(p_count + m_count);
}

/// Save a single message to NVS by appending/updating it in the stored list.
static inline bool nvs_save_msg(const String& name) {
    OscRegistry& reg = osc_registry();
    OscMessage* m = reg.find_msg(name);
    if (!m) return false;

    Preferences prefs;
    prefs.begin("osc_store", false);

    uint16_t count = prefs.getUShort("m_count", 0);

    // Check if this message name is already stored; if so, overwrite in place.
    int slot = -1;
    for (uint16_t i = 0; i < count; i++) {
        String key = "m_" + String(i);
        String saved = prefs.getString(key.c_str(), "");
        int ni = saved.indexOf("name:");
        if (ni >= 0) {
            int end = saved.indexOf(',', ni);
            String stored_name = (end < 0) ? saved.substring(ni + 5)
                                           : saved.substring(ni + 5, end);
            stored_name.trim();
            if (osc_lower_copy(stored_name) == osc_lower_copy(name)) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) {
        slot = count;
        prefs.putUShort("m_count", count + 1);
    }
    String key = "m_" + String(slot);
    prefs.putString(key.c_str(), msg_to_save_string(*m));

    prefs.end();
    return true;
}

/// Save a single scene to NVS by appending/updating it in the stored list.
static inline bool nvs_save_scene(const String& name) {
    OscRegistry& reg = osc_registry();
    OscScene* p = reg.find_scene(name);
    if (!p) return false;

    Preferences prefs;
    prefs.begin("osc_store", false);

    uint16_t count = prefs.getUShort("p_count", 0);

    int slot = -1;
    for (uint16_t i = 0; i < count; i++) {
        String key = "p_" + String(i);
        String saved = prefs.getString(key.c_str(), "");
        int ni = saved.indexOf("name:");
        if (ni >= 0) {
            int end = saved.indexOf(',', ni);
            String stored_name = (end < 0) ? saved.substring(ni + 5)
                                           : saved.substring(ni + 5, end);
            stored_name.trim();
            if (osc_lower_copy(stored_name) == osc_lower_copy(name)) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) {
        slot = count;
        prefs.putUShort("p_count", count + 1);
    }
    String key = "p_" + String(slot);
    prefs.putString(key.c_str(), scene_to_save_string(*p));

    prefs.end();
    return true;
}

/// Clear all saved OSC data from NVS.
static inline void nvs_clear_osc_data() {
    Preferences prefs;
    prefs.begin("osc_store", false);
    prefs.clear();
    prefs.end();
    nvs_clear_oris();
}

#endif // OSC_STORAGE_H
