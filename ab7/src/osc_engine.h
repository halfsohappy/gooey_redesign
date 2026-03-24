// =============================================================================
// osc_engine.h — FreeRTOS send tasks and the MicroOsc transport layer
// =============================================================================
//
// This file provides:
//   1. The WiFiUDP + MicroOscUdp globals used for all OSC I/O.
//   2. A send mutex that serialises access to the UDP socket across tasks.
//   3. The FreeRTOS task function that each OscPatch runs to send its
//      messages at the configured polling period.
//   4. Helpers: start_patch / stop_patch / blackout_all / restore_all.
//   5. The StatusReporter::send() implementation (needs the MicroOsc global).
//   6. begin_udp() — initialises WiFi and the UDP listener.
//
// SENDING FLOW (per patch task iteration):
//   1. Sleep for send_period_ms.
//   2. If the patch is disabled, go back to sleep.
//   3. Lock the registry mutex.
//   4. For each message index in the patch:
//      a. Skip if the message is disabled.
//      b. Resolve effective IP, port, and OSC address (message's own value,
//         or the patch's value if the patch overrides that field, or the
//         patch's value as a fallback if the message has no value set).
//      c. Read the live sensor value from data_streams[].
//      d. Lock the send mutex.
//      e. Set the MicroOsc destination and call sendFloat().
//      f. Unlock the send mutex.
//   5. Unlock the registry mutex and loop.
// =============================================================================

#ifndef OSC_ENGINE_H
#define OSC_ENGINE_H

#include <MicroOsc.h>
#include <MicroOscUdp.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "osc_registry.h"
#include "osc_status.h"
#include "ab7_hardware.h"
#include "ori_tracker.h"

// ---------------------------------------------------------------------------
// Global transport objects
// ---------------------------------------------------------------------------

WiFiUDP        Udp;                    // shared UDP socket
MicroOscUdp<1024> osc(&Udp);          // OSC codec (1024-byte receive buffer)

// ---------------------------------------------------------------------------
// Send mutex — serialises all outbound UDP writes across FreeRTOS tasks
// ---------------------------------------------------------------------------

static inline SemaphoreHandle_t& osc_send_mutex() {
    static SemaphoreHandle_t mtx = xSemaphoreCreateMutex();
    return mtx;
}

// ---------------------------------------------------------------------------
// Optional serial logging of every outbound message
// ---------------------------------------------------------------------------

static bool _send_logging_enabled = false;

static inline void set_send_logging_enabled(bool enabled) { _send_logging_enabled = enabled; }
static inline bool get_send_logging_enabled() { return _send_logging_enabled; }

// ---------------------------------------------------------------------------
// StatusReporter::send() implementation
// ---------------------------------------------------------------------------
//
// Defined here (not in osc_status.h) because it needs the osc global and
// the send mutex.

inline void StatusReporter::send(StatusLevel lvl, const String& category,
                                  const String& message) {
    // Build the payload: "[LEVEL] category: message"
    String payload = String("[") + status_level_label(lvl) + "] "
                   + category + ": " + message;

    // Always echo to serial if the level passes the serial filter.
    if (would_serial(lvl)) {
        Serial.println(payload);
    }

    // Send over OSC only if configured and level passes the OSC filter.
    if (!would_send(lvl)) return;
    if (dest_port == 0) return;

    xSemaphoreTake(osc_send_mutex(), portMAX_DELAY);
    osc.setDestination(dest_ip, dest_port);
    osc.sendString(dest_address.c_str(), payload.c_str());
    xSemaphoreGive(osc_send_mutex());
}

// ---------------------------------------------------------------------------
// Effective-value resolution helpers
// ---------------------------------------------------------------------------
//
// These decide what IP / port / address / bounds a message actually uses,
// taking into account the patch override flags and fallback logic:
//
//   1. If the patch overrides the field AND the patch has it set → patch wins.
//   2. Else if the message has it set → message wins.
//   3. Else if the patch has it set (no override, but no message value) → patch.
//   4. Else → empty / zero (not sendable).

static inline IPAddress resolve_ip(const OscMessage& m, const OscPatch& p) {
    if (p.overrides.ip && p.exist.ip) return p.ip;
    if (m.exist.ip)                   return m.ip;
    if (p.exist.ip)                   return p.ip;
    return IPAddress(0, 0, 0, 0);
}

static inline unsigned int resolve_port(const OscMessage& m, const OscPatch& p) {
    if (p.overrides.port && p.exist.port) return p.port;
    if (m.exist.port)                     return m.port;
    if (p.exist.port)                     return p.port;
    return 0;
}

/// Resolve the effective OSC address using the patch's address_mode:
///
///   ADR_FALLBACK — message's own address, patch as fallback if msg has none.
///   ADR_OVERRIDE — patch address replaces message address entirely.
///   ADR_PREPEND  — patch.adr + msg.adr  (e.g. "/mixer" + "/fader1" → "/mixer/fader1")
///   ADR_APPEND   — msg.adr + patch.adr  (e.g. "/fader1" + "/mixer" → "/fader1/mixer")
///
/// For PREPEND and APPEND, if either side is empty it gracefully degrades
/// to just the non-empty side.  A leading double-slash is avoided by
/// stripping trailing '/' from the first part.
static inline String resolve_address(const OscMessage& m, const OscPatch& p) {
    String m_adr = m.exist.adr ? m.osc_address : String("");
    String p_adr = p.exist.adr ? p.osc_address : String("");

    switch (p.address_mode) {
        case ADR_OVERRIDE:
            // Patch address replaces message address unconditionally.
            return p_adr.length() > 0 ? p_adr : m_adr;

        case ADR_PREPEND: {
            // patch.adr + msg.adr  (patch first, message appended).
            if (p_adr.length() == 0) return m_adr;
            if (m_adr.length() == 0) return p_adr;
            // Strip trailing '/' from patch to avoid "//".
            String prefix = p_adr;
            if (prefix.endsWith("/")) prefix.remove(prefix.length() - 1);
            // Ensure msg part starts with '/'.
            if (!m_adr.startsWith("/")) m_adr = "/" + m_adr;
            return prefix + m_adr;
        }

        case ADR_APPEND: {
            // msg.adr + patch.adr  (message first, patch appended).
            if (m_adr.length() == 0) return p_adr;
            if (p_adr.length() == 0) return m_adr;
            // Strip trailing '/' from message to avoid "//".
            String prefix = m_adr;
            if (prefix.endsWith("/")) prefix.remove(prefix.length() - 1);
            // Ensure patch part starts with '/'.
            if (!p_adr.startsWith("/")) p_adr = "/" + p_adr;
            return prefix + p_adr;
        }

        case ADR_FALLBACK:
        default:
            // Message's address wins if set; otherwise use patch as fallback.
            // The overrides.adr flag is handled here for backward compatibility:
            // if overrides.adr is true, the patch address is forced (like OVERRIDE).
            if (p.overrides.adr && p_adr.length() > 0) return p_adr;
            return m_adr.length() > 0 ? m_adr : p_adr;
    }
}

/// Resolve the effective low bound (bounds[0]).
/// Patch override → message's own → patch fallback → 0.0.
static inline float resolve_low(const OscMessage& m, const OscPatch& p) {
    if (p.overrides.low && p.exist.low)  return p.bounds[0];
    if (m.exist.low)                     return m.bounds[0];
    if (p.exist.low)                     return p.bounds[0];
    return 0.0f;
}

/// Resolve the effective high bound (bounds[1]).
/// Patch override → message's own → patch fallback → 1.0.
static inline float resolve_high(const OscMessage& m, const OscPatch& p) {
    if (p.overrides.high && p.exist.high) return p.bounds[1];
    if (m.exist.high)                     return m.bounds[1];
    if (p.exist.high)                     return p.bounds[1];
    return 1.0f;
}

// ---------------------------------------------------------------------------
// Patch send task — the FreeRTOS function each patch runs
// ---------------------------------------------------------------------------
//
// The task receives a pointer to its OscPatch (which lives in the registry's
// fixed array and never moves).  It loops forever, sending all assigned
// messages at the configured rate.

void patch_send_task(void* param) {
    OscPatch* patch = static_cast<OscPatch*>(param);
    OscRegistry& reg = osc_registry();

    for (;;) {
        // Sleep for the polling period.
        vTaskDelay(pdMS_TO_TICKS(clamp_patch_period_ms((long)patch->send_period_ms)));

        if (!patch->enabled) continue;

        reg.lock();

        for (uint8_t i = 0; i < patch->msg_count; i++) {
            int mi = patch->msg_indices[i];
            if (mi < 0 || mi >= (int)reg.msg_count) continue;

            OscMessage& msg = reg.messages[mi];
            if (!msg.enabled)    continue;
            if (!msg.value_ptr)  continue;

            // --- Ori-conditional check ---
            // If the message has an ori_only or ori_not condition, check it.
            {
                OriTracker& ot = ori_tracker();
                if (msg.ori_only.length() > 0 && !ot.is_active(msg.ori_only)) continue;
                if (msg.ori_not.length() > 0  &&  ot.is_active(msg.ori_not))  continue;
            }

            // Resolve effective destination.
            IPAddress    eff_ip   = resolve_ip(msg, *patch);
            unsigned int eff_port = resolve_port(msg, *patch);
            String       eff_adr  = resolve_address(msg, *patch);

            if (eff_ip == IPAddress(0, 0, 0, 0) || eff_port == 0 || eff_adr.length() == 0) {
                continue;  // not enough info to send
            }

            // Read the live sensor value (normalised to [0, 1]).
            float val = *(msg.value_ptr);

            // Resolve effective output bounds (patch may override message's).
            float eff_low  = resolve_low(msg, *patch);
            float eff_high = resolve_high(msg, *patch);

            // Map [0, 1] → [low, high].  If low == high the value passes
            // through as-is (degenerate range means "no scaling").
            if (eff_low != eff_high) {
                val = eff_low + val * (eff_high - eff_low);
            }

            // Send the float value over OSC.
            xSemaphoreTake(osc_send_mutex(), portMAX_DELAY);
            osc.setDestination(eff_ip, eff_port);
            osc.sendFloat(eff_adr.c_str(), val);
            if (get_send_logging_enabled()) {
                Serial.print(F("[SEND] "));
                Serial.print(eff_ip);
                Serial.print(F(":"));
                Serial.print(eff_port);
                Serial.print(F(" "));
                Serial.print(eff_adr);
                Serial.print(F(" = "));
                Serial.println(val, 6);
            }
            xSemaphoreGive(osc_send_mutex());
        }

        reg.unlock();
    }
}

// ---------------------------------------------------------------------------
// Patch lifecycle helpers
// ---------------------------------------------------------------------------

/// Create and start the FreeRTOS task for a patch.  No-op if already running.
static inline void start_patch(OscPatch* p) {
    if (!p) return;
    if (p->task_handle) return;  // already running

    p->enabled = true;

    // Task name: truncate to 15 chars (FreeRTOS limit).
    char tname[16];
    snprintf(tname, sizeof(tname), "p_%s", p->name.c_str());

    xTaskCreate(
        patch_send_task,   // task function
        tname,             // name (for debugging)
        4096,              // stack size (bytes)
        p,                 // parameter: pointer to the patch
        1,                 // priority
        &p->task_handle    // handle stored back in the patch
    );

    status_reporter().info("patch", "Started patch '" + p->name + "'");
}

/// Stop the FreeRTOS task for a patch.
static inline void stop_patch(OscPatch* p) {
    if (!p) return;
    p->enabled = false;
    if (p->task_handle) {
        vTaskDelete(p->task_handle);
        p->task_handle = nullptr;
    }
    status_reporter().info("patch", "Stopped patch '" + p->name + "'");
}

/// Stop all patch tasks (theater "blackout").
static inline void blackout_all() {
    OscRegistry& reg = osc_registry();
    reg.lock();
    for (uint16_t i = 0; i < reg.patch_count; i++) {
        stop_patch(&reg.patches[i]);
    }
    reg.unlock();
    status_reporter().info("engine", "BLACKOUT — all patches stopped");
}

/// Re-enable and start all patches that have at least one message.
static inline void restore_all() {
    OscRegistry& reg = osc_registry();
    reg.lock();
    for (uint16_t i = 0; i < reg.patch_count; i++) {
        if (reg.patches[i].msg_count > 0) {
            start_patch(&reg.patches[i]);
        }
    }
    reg.unlock();
    status_reporter().info("engine", "RESTORE — all patches restarted");
}

// ---------------------------------------------------------------------------
// UDP / WiFi initialisation
// ---------------------------------------------------------------------------

/// Connect to WiFi and start the UDP listener on `start_port`.
/// Called once during setup() after provisioning.
static inline void begin_udp(const String& start_ip, const String& start_ssid,
                              const String& start_pass, int start_port) {
    IPAddress static_ip;
    if (start_ip != "dhcp") {
        static_ip.fromString(start_ip);
        WiFi.config(static_ip);
    }

    WiFi.begin(start_ssid.c_str(), start_pass.c_str());
    Serial.print("Connecting to WiFi");
    const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000UL;
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if (millis() - startTime >= WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println();
            Serial.println(F("[BOOT] WiFi connection timed out. Clearing credentials and restarting..."));
            // Clear provisioned flag so the device re-enters provisioning mode.
            // Guard against begin() failure (e.g. corrupted NVS): if begin fails,
            // getBool("provisioned") will also fail and return the default (false),
            // so provisioning will start correctly on the next boot anyway.
            if (preferences.begin("device_config", false)) {
                preferences.putBool("provisioned", false);
                preferences.end();
            }
            ESP.restart();
        }
    }
    Serial.println();
    Serial.println("WiFi connected — IP: " + WiFi.localIP().toString());

    Udp.begin(start_port);
    Serial.println("UDP listening on port " + String(start_port));

    // Initialise the send mutex and registry mutex early.
    osc_send_mutex();
    osc_registry().ensure_mutex();

    status_reporter().info("network", "UDP ready on " + WiFi.localIP().toString()
                           + ":" + String(start_port));
}

#endif // OSC_ENGINE_H
