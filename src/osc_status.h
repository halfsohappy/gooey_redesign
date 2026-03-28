// =============================================================================
// osc_status.h — Status / error / progress reporting over OSC
// =============================================================================
//
// The StatusReporter sends human-readable status strings to a single
// configurable OSC destination.  Every action in the system can report
// a message with an importance level:
//
//   STATUS_ERROR    (0)  — something went wrong, likely user-visible
//   STATUS_WARNING  (1)  — potential problem, non-fatal
//   STATUS_INFO     (2)  — normal operational messages
//   STATUS_DEBUG    (3)  — verbose diagnostic output
//
// The reporter has a configurable minimum level.  Messages below that
// threshold are silently dropped.  For example, setting the level to
// STATUS_ERROR means only errors are forwarded to the monitoring device.
//
// USAGE:
//   status_reporter().configure(IPAddress(192,168,1,100), 9000, "/status");
//   status_reporter().set_level(STATUS_INFO);
//   status_reporter().send(STATUS_INFO, "scene", "Scene 'mixer1' started");
//   status_reporter().error("cmd", "Unknown command: /foo");
//
// The reporter uses the global MicroOscUdp instance and the send mutex
// defined in osc_engine.h, so it is safe to call from any FreeRTOS task.
// =============================================================================

#ifndef OSC_STATUS_H
#define OSC_STATUS_H

#include <Arduino.h>
#include <IPAddress.h>

// ---------------------------------------------------------------------------
// Status levels (lower number = higher importance)
// ---------------------------------------------------------------------------

enum StatusLevel : uint8_t {
    STATUS_ERROR   = 0,   // critical failures
    STATUS_WARNING = 1,   // non-fatal problems
    STATUS_INFO    = 2,   // normal progress / confirmations
    STATUS_DEBUG   = 3    // verbose diagnostics
};

/// Convert a StatusLevel to a short human-readable label.
static inline const char* status_level_label(StatusLevel lvl) {
    switch (lvl) {
        case STATUS_ERROR:   return "ERROR";
        case STATUS_WARNING: return "WARN";
        case STATUS_INFO:    return "INFO";
        case STATUS_DEBUG:   return "DEBUG";
        default:             return "???";
    }
}

/// Parse a level name (case-insensitive) to a StatusLevel.
/// Returns STATUS_INFO on unrecognised input.
static inline StatusLevel status_level_from_string(const String& s) {
    String l = s;
    l.trim();
    l.toLowerCase();
    if (l == "error"   || l == "err" || l == "0") return STATUS_ERROR;
    if (l == "warning" || l == "warn" || l == "1") return STATUS_WARNING;
    if (l == "info"    || l == "2")                return STATUS_INFO;
    if (l == "debug"   || l == "dbg"  || l == "3") return STATUS_DEBUG;
    return STATUS_INFO;
}

// ---------------------------------------------------------------------------
// StatusReporter
// ---------------------------------------------------------------------------

class StatusReporter {
public:
    IPAddress    dest_ip;
    unsigned int dest_port    = 0;
    String       dest_address = "/status";
    StatusLevel  min_level    = STATUS_INFO;
    StatusLevel  serial_level = STATUS_DEBUG;   // serial is verbose by default
    bool         configured   = false;

    StatusReporter() : dest_ip(0, 0, 0, 0) {}

    /// Set the destination for status messages.
    void configure(const IPAddress& ip, unsigned int port, const String& adr) {
        dest_ip      = ip;
        dest_port    = port;
        dest_address = adr;
        configured   = true;
    }

    /// Set the minimum importance level for OSC output.
    void set_level(StatusLevel lvl) { min_level = lvl; }

    /// Set the minimum importance level for Serial output.
    void set_serial_level(StatusLevel lvl) { serial_level = lvl; }

    /// Returns true if a message at `lvl` would be sent over OSC.
    bool would_send(StatusLevel lvl) const {
        return configured && (lvl <= min_level);
    }

    /// Returns true if a message at `lvl` would be printed to Serial.
    bool would_serial(StatusLevel lvl) const {
        return lvl <= serial_level;
    }

    // send() is implemented in osc_engine.h after the MicroOsc instance and
    // send mutex are available.  These inline wrappers are just convenience
    // shorthands that call send().

    void send(StatusLevel lvl, const String& category, const String& message);

    void error  (const String& cat, const String& msg) { send(STATUS_ERROR,   cat, msg); }
    void warning(const String& cat, const String& msg) { send(STATUS_WARNING, cat, msg); }
    void info   (const String& cat, const String& msg) { send(STATUS_INFO,    cat, msg); }
    void debug  (const String& cat, const String& msg) { send(STATUS_DEBUG,   cat, msg); }
};

/// Global status reporter accessor (Meyer's singleton).
static inline StatusReporter& status_reporter() {
    static StatusReporter instance;
    return instance;
}

#endif // OSC_STATUS_H
