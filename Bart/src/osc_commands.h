// =============================================================================
// osc_commands.h — Incoming OSC command dispatcher
// =============================================================================
//
// This file contains the main handler that is called every time an OSC
// message arrives.  It parses the address, identifies the target (message,
// patch, or system command), and dispatches to the appropriate action.
//
// ADDRESS FORMAT:
//   /annieData{device_adr}/{category}/{name}/{command}
//
//   {device_adr}  — the device's provisioned name (e.g. "/bart")
//   {category}    — "msg", "patch", "list", "clone", "rename", or a
//                    top-level command like "blackout", "restore", "status"
//   {name}        — the name of the target message or patch
//   {command}     — the action to perform (defaults to "assign" if omitted)
//
// COMMAND REFERENCE  (see user_guide.md for full documentation):
// ─────────────────────────────────────────────────────────────────────────────
//
// ── MESSAGE COMMANDS (/annieData{dev}/msg/{name}/...) ──────────────────────
//   (no command / assign)  — create or update a message from config string
//   delete                 — remove message from registry
//   enable                 — enable message for sending
//   disable / mute         — disable message (skip during send)
//   info                   — reply with message details
//
// ── PATCH COMMANDS (/annieData{dev}/patch/{name}/...) ──────────────────────
//   (no command / assign)  — create or update a patch from config string
//   delete                 — remove patch and its task
//   start                  — create FreeRTOS task and begin sending
//   stop                   — stop the send task
//   enable                 — re-enable a stopped patch (same as start)
//   disable / mute         — stop sending without deleting the task
//   addmsg                 — add message(s) to this patch (comma-separated)
//   removemsg              — remove a message from this patch
//   period                 — set the send period in milliseconds
//   override               — set which fields the patch forces on its messages
//   adrmode                — set address composition mode (fallback/override/prepend/append)
//   setall                 — set a property on all messages in this patch
//   solo                   — enable one message, disable all others in patch
//   unsolo                 — re-enable all messages in this patch
//   info                   — reply with patch details
//
// ── CLONE COMMANDS (/annieData{dev}/clone/...) ─────────────────────────────
//   msg      [src, dest]   — duplicate a message under a new name
//   patch    [src, dest]   — duplicate a patch (and optionally its messages)
//
// ── RENAME COMMANDS (/annieData{dev}/rename/...) ───────────────────────────
//   msg      [old, new]    — rename a message
//   patch    [old, new]    — rename a patch
//
// ── MOVE COMMAND (/annieData{dev}/move) ────────────────────────────────────
//   (string args: msgName, patchName) — move a message into a different patch
//
// ── LIST COMMANDS (/annieData{dev}/list/...) ───────────────────────────────
//   msgs     [verbose?]    — reply with all message names (+ params if verbose)
//   patches  [verbose?]    — reply with all patch names (+ params if verbose)
//   all      [verbose?]    — reply with everything
//
// ── GLOBAL COMMANDS (/annieData{dev}/...) ──────────────────────────────────
//   blackout               — stop ALL patch tasks immediately
//   restore                — restart all patches that have messages
//
// ── STATUS COMMANDS (/annieData{dev}/status/...) ───────────────────────────
//   config   [config_str]  — set status destination (ip, port, address)
//   level    [level_str]   — set minimum importance level
//
// =============================================================================

#ifndef OSC_COMMANDS_H
#define OSC_COMMANDS_H

#include "osc_engine.h"

// Forward-declare the device address (defined in main.h / main.cpp).
extern String device_adr;

// ---------------------------------------------------------------------------
// Reply helper — sends a string back to whoever sent the command
// ---------------------------------------------------------------------------
//
// We send replies to the source IP on the same port, under an address that
// mirrors the incoming address with "/reply" prepended.  This is a pragmatic
// convention for OSC — the receiver can filter on /reply/*.

static inline void osc_reply(const IPAddress& dest_ip, unsigned int dest_port,
                              const String& reply_address, const String& text) {
    xSemaphoreTake(osc_send_mutex(), portMAX_DELAY);
    osc.setDestination(dest_ip, dest_port);
    osc.sendString(reply_address.c_str(), text.c_str());
    xSemaphoreGive(osc_send_mutex());
}

// ---------------------------------------------------------------------------
// Main command handler — called from loop() via osc.onOscMessageReceived()
// ---------------------------------------------------------------------------

void osc_handle_message(MicroOscMessage& osc_msg) {
    String address = osc_msg.getOscAddress();
    Serial.println("[OSC] " + address);

    // ── Check device prefix ────────────────────────────────────────────────
    String prefix = "/annieData" + device_adr;
    if (!address.startsWith(prefix)) {
        Serial.println("  → not for this device, ignoring");
        return;
    }
    address = address.substring(prefix.length());  // strip prefix

    // Source address for replies (remote IP + port the message came from).
    IPAddress sender_ip = Udp.remoteIP();
    unsigned int sender_port = Udp.remotePort();
    String reply_adr = "/reply" + device_adr;

    OscRegistry& reg = osc_registry();

    // ── TOP-LEVEL COMMANDS (no name required) ──────────────────────────────

    if (address == "/blackout") {
        blackout_all();
        osc_reply(sender_ip, sender_port, reply_adr, "BLACKOUT");
        return;
    }

    if (address == "/restore") {
        restore_all();
        osc_reply(sender_ip, sender_port, reply_adr, "RESTORE");
        return;
    }

    // ── STATUS COMMANDS ────────────────────────────────────────────────────

    if (address.startsWith("/status")) {
        String sub = address.substring(7);  // after "/status"
        if (sub == "/config" || sub == "/configure") {
            // Payload: "ip:x.x.x.x, port:NNNN, adr:/some/address"
            OscMessage cfg;
            String err;
            if (!cfg.from_config_str(osc_msg.nextAsString(), &err)) {
                status_reporter().error("status", "Bad config: " + err);
                return;
            }
            IPAddress    sip  = cfg.exist.ip   ? cfg.ip   : sender_ip;
            unsigned int sp   = cfg.exist.port ? cfg.port : sender_port;
            String       sadr = cfg.exist.adr  ? cfg.osc_address : "/status";
            status_reporter().configure(sip, sp, sadr);
            status_reporter().info("status", "Status reporter configured");
            return;
        }
        if (sub == "/level") {
            StatusLevel lvl = status_level_from_string(osc_msg.nextAsString());
            status_reporter().set_level(lvl);
            status_reporter().info("status", String("Level set to ") + status_level_label(lvl));
            return;
        }
        status_reporter().warning("cmd", "Unknown status command: " + sub);
        return;
    }

    // ── LIST COMMANDS ──────────────────────────────────────────────────────

    if (address.startsWith("/list")) {
        String sub = address.substring(5);  // after "/list"
        // Optional "verbose" flag: any string argument → verbose.
        bool verbose = false;
        // Check if there is a string argument.
        const char* arg = nullptr;
        // MicroOsc: try to read a string argument (safe even if none present).
        arg = osc_msg.nextAsString();
        if (arg && (String(arg) == "verbose" || String(arg) == "v"
                    || String(arg) == "1" || String(arg) == "true")) {
            verbose = true;
        }

        reg.lock();

        if (sub == "/msgs" || sub == "/messages") {
            String result = "Messages (" + String(reg.msg_count) + "):\n";
            for (uint16_t i = 0; i < reg.msg_count; i++) {
                result += "  " + reg.messages[i].to_info_string(verbose) + "\n";
            }
            osc_reply(sender_ip, sender_port, reply_adr + "/list/msgs", result);
        } else if (sub == "/patches") {
            String result = "Patches (" + String(reg.patch_count) + "):\n";
            for (uint16_t i = 0; i < reg.patch_count; i++) {
                result += "  " + reg.patches[i].to_info_string(verbose) + "\n";
            }
            osc_reply(sender_ip, sender_port, reply_adr + "/list/patches", result);
        } else if (sub == "/all" || sub == "") {
            String result = "Patches (" + String(reg.patch_count) + "):\n";
            for (uint16_t i = 0; i < reg.patch_count; i++) {
                result += "  " + reg.patches[i].to_info_string(verbose) + "\n";
            }
            result += "Messages (" + String(reg.msg_count) + "):\n";
            for (uint16_t i = 0; i < reg.msg_count; i++) {
                result += "  " + reg.messages[i].to_info_string(verbose) + "\n";
            }
            osc_reply(sender_ip, sender_port, reply_adr + "/list/all", result);
        } else {
            status_reporter().warning("cmd", "Unknown list target: " + sub);
        }

        reg.unlock();
        return;
    }

    // ── CLONE COMMANDS ─────────────────────────────────────────────────────

    if (address.startsWith("/clone")) {
        String sub = address.substring(6);  // after "/clone"
        const char* src_name  = osc_msg.nextAsString();
        const char* dest_name = osc_msg.nextAsString();
        if (!src_name || !dest_name) {
            status_reporter().error("cmd", "clone requires two string args: source, destination");
            return;
        }

        reg.lock();

        if (sub == "/msg" || sub == "/message") {
            OscMessage* src = reg.find_msg(src_name);
            if (!src) {
                status_reporter().error("cmd", String("clone/msg: source '") + src_name + "' not found");
                reg.unlock();
                return;
            }
            OscMessage* dest = reg.get_or_create_msg(dest_name);
            if (!dest) {
                status_reporter().error("cmd", "clone/msg: registry full");
                reg.unlock();
                return;
            }
            // Copy all fields except name.
            ExistFlags saved_exist = dest->exist;
            String saved_name = dest->name;
            *dest = *src;
            dest->name = String(dest_name);
            dest->exist.name = true;
            status_reporter().info("cmd", String("Cloned msg '") + src_name + "' → '" + dest_name + "'");
        } else if (sub == "/patch") {
            OscPatch* src = reg.find_patch(src_name);
            if (!src) {
                status_reporter().error("cmd", String("clone/patch: source '") + src_name + "' not found");
                reg.unlock();
                return;
            }
            OscPatch* dest = reg.get_or_create_patch(dest_name);
            if (!dest) {
                status_reporter().error("cmd", "clone/patch: registry full");
                reg.unlock();
                return;
            }
            // Copy config but not task state.
            dest->ip             = src->ip;
            dest->port           = src->port;
            dest->osc_address    = src->osc_address;
            dest->bounds[0]      = src->bounds[0];
            dest->bounds[1]      = src->bounds[1];
            dest->send_period_ms = src->send_period_ms;
            dest->address_mode   = src->address_mode;
            dest->overrides      = src->overrides;
            dest->exist          = src->exist;
            dest->exist.name     = true;
            dest->name           = String(dest_name);
            // Copy message list.
            dest->msg_count = src->msg_count;
            memcpy(dest->msg_indices, src->msg_indices,
                   src->msg_count * sizeof(int));
            status_reporter().info("cmd", String("Cloned patch '") + src_name + "' → '" + dest_name + "'");
        } else {
            status_reporter().warning("cmd", "Unknown clone target: " + sub);
        }

        reg.unlock();
        return;
    }

    // ── RENAME COMMANDS ────────────────────────────────────────────────────

    if (address.startsWith("/rename")) {
        String sub = address.substring(7);  // after "/rename"
        const char* old_name = osc_msg.nextAsString();
        const char* new_name = osc_msg.nextAsString();
        if (!old_name || !new_name) {
            status_reporter().error("cmd", "rename requires two string args: old, new");
            return;
        }

        reg.lock();

        if (sub == "/msg" || sub == "/message") {
            OscMessage* m = reg.find_msg(old_name);
            if (!m) {
                status_reporter().error("cmd", String("rename/msg: '") + old_name + "' not found");
            } else {
                m->name = String(new_name);
                status_reporter().info("cmd", String("Renamed msg '") + old_name + "' → '" + new_name + "'");
            }
        } else if (sub == "/patch") {
            OscPatch* p = reg.find_patch(old_name);
            if (!p) {
                status_reporter().error("cmd", String("rename/patch: '") + old_name + "' not found");
            } else {
                p->name = String(new_name);
                status_reporter().info("cmd", String("Renamed patch '") + old_name + "' → '" + new_name + "'");
            }
        } else {
            status_reporter().warning("cmd", "Unknown rename target: " + sub);
        }

        reg.unlock();
        return;
    }

    // ── MOVE COMMAND ───────────────────────────────────────────────────────

    if (address == "/move") {
        const char* msg_name   = osc_msg.nextAsString();
        const char* patch_name = osc_msg.nextAsString();
        if (!msg_name || !patch_name) {
            status_reporter().error("cmd", "move requires two args: msgName, patchName");
            return;
        }

        reg.lock();

        OscMessage* m = reg.find_msg(msg_name);
        OscPatch*   p = reg.find_patch(patch_name);
        if (!m) {
            status_reporter().error("cmd", String("move: msg '") + msg_name + "' not found");
            reg.unlock();
            return;
        }
        if (!p) {
            status_reporter().error("cmd", String("move: patch '") + patch_name + "' not found");
            reg.unlock();
            return;
        }

        int mi = reg.msg_index(m);

        // Remove from current patch (if any).
        if (m->patch) {
            m->patch->remove_msg(mi);
        }

        // Add to new patch.
        p->add_msg(mi);
        m->patch = p;
        m->exist.patch = true;

        status_reporter().info("cmd", String("Moved msg '") + msg_name
                               + "' → patch '" + patch_name + "'");
        reg.unlock();
        return;
    }

    // ── CATEGORY DISPATCH: /msg or /patch ──────────────────────────────────

    bool is_msg   = address.startsWith("/msg");
    bool is_patch = address.startsWith("/patch");

    if (!is_msg && !is_patch) {
        status_reporter().warning("cmd", "Unknown category in: " + address);
        return;
    }

    // Strip category prefix to get /{name}/{command}.
    if (is_msg)   address = address.substring(4);   // strip "/msg"
    if (is_patch) address = address.substring(6);    // strip "/patch"

    // Parse name and command from the remaining /{name}/{command}.
    //   /name/command  →  name, command
    //   /name          →  name, "assign"
    //   /name/         →  name, "assign"
    int last_slash = address.lastIndexOf('/');
    String name_mp, command;

    if (last_slash <= 0) {
        name_mp = address.substring(1);
        command = "assign";
    } else if (last_slash == (int)address.length() - 1) {
        name_mp = address.substring(1, last_slash);
        command = "assign";
    } else {
        name_mp = address.substring(1, last_slash);
        command = address.substring(last_slash + 1);
    }

    if (name_mp.length() == 0) {
        status_reporter().warning("cmd", "Empty name in address");
        return;
    }

    command.toLowerCase();
    Serial.println("  name=" + name_mp + "  cmd=" + command);

    // ════════════════════════════════════════════════════════════════════════
    // PATCH COMMANDS
    // ════════════════════════════════════════════════════════════════════════

    if (is_patch) {
        reg.lock();

        // ── assign (create / update) ───────────────────────────────────────
        if (command == "assign") {
            OscPatch* p = reg.get_or_create_patch(name_mp);
            if (!p) {
                status_reporter().error("patch", "Registry full");
                reg.unlock();
                return;
            }
            const char* arg = osc_msg.nextAsString();
            if (arg && strlen(arg) > 0) {
                OscMessage csv;
                String err;
                if (!csv.from_config_str(arg, &err)) {
                    status_reporter().warning("patch", "Parse warning: " + err);
                }
                if (csv.exist.ip)   { p->ip = csv.ip;                   p->exist.ip   = true; }
                if (csv.exist.port) { p->port = csv.port;               p->exist.port = true; }
                if (csv.exist.adr)  { p->osc_address = csv.osc_address; p->exist.adr  = true; }
                if (csv.exist.low)  { p->bounds[0] = csv.bounds[0];     p->exist.low  = true; }
                if (csv.exist.high) { p->bounds[1] = csv.bounds[1];     p->exist.high = true; }
            }
            status_reporter().info("patch", "Patch '" + name_mp + "' updated");
        }

        // ── delete ─────────────────────────────────────────────────────────
        else if (command == "delete" || command == "remove") {
            if (reg.delete_patch(name_mp)) {
                status_reporter().info("patch", "Deleted patch '" + name_mp + "'");
            } else {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
            }
        }

        // ── start ──────────────────────────────────────────────────────────
        else if (command == "start" || command == "enable" || command == "go") {
            OscPatch* p = reg.find_patch(name_mp);
            if (!p) {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
            } else {
                start_patch(p);
            }
        }

        // ── stop ───────────────────────────────────────────────────────────
        else if (command == "stop" || command == "disable" || command == "mute") {
            OscPatch* p = reg.find_patch(name_mp);
            if (!p) {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
            } else {
                stop_patch(p);
            }
        }

        // ── addmsg ─────────────────────────────────────────────────────────
        //    Payload: comma-separated message names.
        else if (command == "addmsg" || command == "add") {
            OscPatch* p = reg.find_patch(name_mp);
            if (!p) {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
                reg.unlock();
                return;
            }
            String arg = osc_msg.nextAsString();
            // Split on commas.
            int start_pos = 0;
            while (start_pos < (int)arg.length()) {
                int comma = arg.indexOf(',', start_pos);
                String mname = (comma < 0) ? arg.substring(start_pos)
                                           : arg.substring(start_pos, comma);
                mname = osc_trim_copy(mname);
                start_pos = (comma < 0) ? arg.length() : comma + 1;
                if (mname.length() == 0) continue;

                OscMessage* m = reg.find_msg(mname);
                if (!m) {
                    status_reporter().warning("patch", "addmsg: msg '" + mname + "' not found");
                    continue;
                }
                int mi = reg.msg_index(m);
                p->add_msg(mi);
                m->patch = p;
                m->exist.patch = true;
                status_reporter().debug("patch", "Added msg '" + mname + "' to patch '" + name_mp + "'");
            }
            status_reporter().info("patch", "addmsg complete for '" + name_mp + "'");
        }

        // ── removemsg ──────────────────────────────────────────────────────
        else if (command == "removemsg" || command == "rmmsg") {
            OscPatch* p = reg.find_patch(name_mp);
            if (!p) {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
                reg.unlock();
                return;
            }
            String mname = osc_msg.nextAsString();
            mname = osc_trim_copy(mname);
            OscMessage* m = reg.find_msg(mname);
            if (!m) {
                status_reporter().warning("patch", "removemsg: msg '" + mname + "' not found");
            } else {
                int mi = reg.msg_index(m);
                p->remove_msg(mi);
                if (m->patch == p) {
                    m->patch = nullptr;
                    m->exist.patch = false;
                }
                status_reporter().info("patch", "Removed msg '" + mname
                                       + "' from patch '" + name_mp + "'");
            }
        }

        // ── period ─────────────────────────────────────────────────────────
        else if (command == "period" || command == "rate") {
            OscPatch* p = reg.find_patch(name_mp);
            if (!p) {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
            } else {
                int ms = (int)osc_msg.nextAsInt();
                if (ms < 1) ms = 1;
                if (ms > 60000) ms = 60000;
                p->send_period_ms = (unsigned int)ms;
                status_reporter().info("patch", "Period for '" + name_mp
                                       + "' set to " + String(ms) + " ms");
            }
        }

        // ── override ───────────────────────────────────────────────────────
        //    Payload: comma-separated field names to toggle override on.
        //    Prefix with '-' to turn off.
        //    Examples:  "ip, port"        →  override ip and port
        //               "-ip"            →  stop overriding ip
        //               "low, high"      →  override output bounds (scale)
        //               "all"            →  override everything
        //               "none"           →  override nothing
        else if (command == "override") {
            OscPatch* p = reg.find_patch(name_mp);
            if (!p) {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
                reg.unlock();
                return;
            }
            String arg = osc_msg.nextAsString();
            arg.toLowerCase();
            arg.trim();

            if (arg == "all") {
                p->overrides.ip  = p->overrides.port = p->overrides.adr  = true;
                p->overrides.low = p->overrides.high = true;
            } else if (arg == "none" || arg == "clear") {
                p->overrides.ip  = p->overrides.port = p->overrides.adr  = false;
                p->overrides.low = p->overrides.high = false;
            } else {
                // Parse individual fields.
                int s = 0;
                while (s < (int)arg.length()) {
                    int c = arg.indexOf(',', s);
                    String f = (c < 0) ? arg.substring(s) : arg.substring(s, c);
                    f = osc_trim_copy(f);
                    s = (c < 0) ? arg.length() : c + 1;
                    if (f.length() == 0) continue;

                    bool enable = true;
                    if (f.startsWith("-")) { enable = false; f = f.substring(1); }
                    if (f.startsWith("+")) { f = f.substring(1); }

                    if (f == "ip")                                 p->overrides.ip   = enable;
                    else if (f == "port")                          p->overrides.port = enable;
                    else if (f == "adr" || f == "addr" || f == "address")
                                                                   p->overrides.adr  = enable;
                    else if (f == "low" || f == "min" || f == "lo")
                                                                   p->overrides.low  = enable;
                    else if (f == "high" || f == "max" || f == "hi")
                                                                   p->overrides.high = enable;
                    else if (f == "scale" || f == "bounds") {
                        p->overrides.low = enable;
                        p->overrides.high = enable;
                    }
                    else status_reporter().warning("patch", "Unknown override field: " + f);
                }
            }
            status_reporter().info("patch", "Override for '" + name_mp + "': ip="
                                   + String(p->overrides.ip ? "ON" : "OFF")
                                   + " port=" + String(p->overrides.port ? "ON" : "OFF")
                                   + " adr=" + String(p->overrides.adr ? "ON" : "OFF")
                                   + " low=" + String(p->overrides.low ? "ON" : "OFF")
                                   + " high=" + String(p->overrides.high ? "ON" : "OFF"));
        }

        // ── adrmode ────────────────────────────────────────────────────────
        //    Set how patch and message OSC addresses are combined:
        //      "fallback"  — message's address, patch as fallback (default)
        //      "override"  — patch address replaces message address
        //      "prepend"   — patch.adr + msg.adr  (e.g. /mixer + /fader1)
        //      "append"    — msg.adr + patch.adr  (e.g. /fader1 + /mixer)
        else if (command == "adrmode" || command == "addressmode") {
            OscPatch* p = reg.find_patch(name_mp);
            if (!p) {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
            } else {
                String mode_str = osc_msg.nextAsString();
                p->address_mode = address_mode_from_string(mode_str);
                status_reporter().info("patch", "Address mode for '" + name_mp
                                       + "' set to: " + address_mode_label(p->address_mode));
            }
        }

        // ── setall ─────────────────────────────────────────────────────────
        //    Apply a config string to every message in this patch.
        //    Example: setall "ip:192.168.1.100, port:9000"
        else if (command == "setall") {
            OscPatch* p = reg.find_patch(name_mp);
            if (!p) {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
                reg.unlock();
                return;
            }
            String cfg_str = osc_msg.nextAsString();
            int applied = 0;
            for (uint8_t i = 0; i < p->msg_count; i++) {
                int mi = p->msg_indices[i];
                if (mi < 0 || mi >= (int)reg.msg_count) continue;
                OscMessage& m = reg.messages[mi];
                OscMessage csv;
                String err;
                if (csv.from_config_str(cfg_str, &err)) {
                    // Merge: new config takes priority.
                    OscMessage merged = csv * m;
                    merged.name = m.name;
                    merged.exist.name = true;
                    m = merged;
                    applied++;
                }
            }
            status_reporter().info("patch", "setall on '" + name_mp + "': applied to "
                                   + String(applied) + " messages");
        }

        // ── solo ───────────────────────────────────────────────────────────
        //    Enable one message, disable all others in this patch.
        else if (command == "solo") {
            OscPatch* p = reg.find_patch(name_mp);
            if (!p) {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
                reg.unlock();
                return;
            }
            String solo_name = osc_msg.nextAsString();
            solo_name = osc_trim_copy(solo_name);
            OscMessage* solo_m = reg.find_msg(solo_name);
            if (!solo_m) {
                status_reporter().warning("patch", "solo: msg '" + solo_name + "' not found");
                reg.unlock();
                return;
            }
            int solo_idx = reg.msg_index(solo_m);
            for (uint8_t i = 0; i < p->msg_count; i++) {
                int mi = p->msg_indices[i];
                if (mi >= 0 && mi < (int)reg.msg_count) {
                    reg.messages[mi].enabled = (mi == solo_idx);
                }
            }
            status_reporter().info("patch", "Solo '" + solo_name + "' in patch '" + name_mp + "'");
        }

        // ── unsolo ─────────────────────────────────────────────────────────
        //    Re-enable all messages in this patch.
        else if (command == "unsolo" || command == "unmute") {
            OscPatch* p = reg.find_patch(name_mp);
            if (!p) {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
                reg.unlock();
                return;
            }
            for (uint8_t i = 0; i < p->msg_count; i++) {
                int mi = p->msg_indices[i];
                if (mi >= 0 && mi < (int)reg.msg_count) {
                    reg.messages[mi].enabled = true;
                }
            }
            status_reporter().info("patch", "Unsolo: all messages in '" + name_mp + "' enabled");
        }

        // ── info ───────────────────────────────────────────────────────────
        else if (command == "info") {
            OscPatch* p = reg.find_patch(name_mp);
            if (!p) {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
            } else {
                String info = p->to_info_string(true) + "\n  Messages:";
                for (uint8_t i = 0; i < p->msg_count; i++) {
                    int mi = p->msg_indices[i];
                    if (mi >= 0 && mi < (int)reg.msg_count) {
                        info += "\n    " + reg.messages[mi].to_info_string(true);
                    }
                }
                osc_reply(sender_ip, sender_port,
                          reply_adr + "/patch/" + name_mp + "/info", info);
            }
        }

        else {
            status_reporter().warning("cmd", "Unknown patch command: " + command);
        }

        reg.unlock();
        return;
    }

    // ════════════════════════════════════════════════════════════════════════
    // MESSAGE COMMANDS
    // ════════════════════════════════════════════════════════════════════════

    if (is_msg) {
        reg.lock();

        // ── assign (create / update) ───────────────────────────────────────
        if (command == "assign") {
            OscMessage* m = reg.get_or_create_msg(name_mp);
            if (!m) {
                status_reporter().error("msg", "Registry full");
                reg.unlock();
                return;
            }
            const char* arg = osc_msg.nextAsString();
            if (arg && strlen(arg) > 0) {
                OscMessage csv;
                String err;
                if (!csv.from_config_str(arg, &err)) {
                    status_reporter().warning("msg", "Parse warning: " + err);
                }
                // Merge: new config values take priority.
                *m = csv * (*m);
                m->name = name_mp;
                m->exist.name = true;

                // If a patch was specified, auto-add to that patch.
                if (m->exist.patch && m->patch) {
                    int mi = reg.msg_index(m);
                    m->patch->add_msg(mi);
                }
            }
            status_reporter().info("msg", "Message '" + name_mp + "' updated");
        }

        // ── delete ─────────────────────────────────────────────────────────
        else if (command == "delete" || command == "remove") {
            if (reg.delete_msg(name_mp)) {
                status_reporter().info("msg", "Deleted msg '" + name_mp + "'");
            } else {
                status_reporter().warning("msg", "Msg '" + name_mp + "' not found");
            }
        }

        // ── enable ─────────────────────────────────────────────────────────
        else if (command == "enable" || command == "unmute") {
            OscMessage* m = reg.find_msg(name_mp);
            if (!m) {
                status_reporter().warning("msg", "Msg '" + name_mp + "' not found");
            } else {
                m->enabled = true;
                status_reporter().info("msg", "Enabled msg '" + name_mp + "'");
            }
        }

        // ── disable / mute ─────────────────────────────────────────────────
        else if (command == "disable" || command == "mute") {
            OscMessage* m = reg.find_msg(name_mp);
            if (!m) {
                status_reporter().warning("msg", "Msg '" + name_mp + "' not found");
            } else {
                m->enabled = false;
                status_reporter().info("msg", "Disabled msg '" + name_mp + "'");
            }
        }

        // ── info ───────────────────────────────────────────────────────────
        else if (command == "info") {
            OscMessage* m = reg.find_msg(name_mp);
            if (!m) {
                status_reporter().warning("msg", "Msg '" + name_mp + "' not found");
            } else {
                osc_reply(sender_ip, sender_port,
                          reply_adr + "/msg/" + name_mp + "/info",
                          m->to_info_string(true));
            }
        }

        else {
            status_reporter().warning("cmd", "Unknown msg command: " + command);
        }

        reg.unlock();
        return;
    }
}

#endif // OSC_COMMANDS_H
