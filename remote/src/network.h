#ifndef NETWORK_H
#define NETWORK_H

// ============================================================================
//  TheaterGWD Setup Remote — WiFi Connection & NVS Credential Storage
// ============================================================================
//
//  Stores WiFi SSID / password and target-device details in NVS so they
//  survive power cycles.  Call net_load() once at boot, then net_connect()
//  to join the network.
//
// ============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "config.h"

// ── Persisted settings ──────────────────────────────────────────────────────

static char    net_ssid[33]       = "";
static char    net_pass[65]       = "";
static char    target_name[MAX_INPUT_LEN] = "bart";
static uint8_t target_ip[4]      = {192, 168, 1, 100};
static uint16_t target_port      = DEFAULT_TARGET_PORT;
static uint16_t listen_port      = DEFAULT_LISTEN_PORT;

// ── Helpers ─────────────────────────────────────────────────────────────────

static IPAddress target_ip_addr() {
    return IPAddress(target_ip[0], target_ip[1], target_ip[2], target_ip[3]);
}

static bool net_has_credentials() {
    return net_ssid[0] != 0;
}

// ── NVS load / save ─────────────────────────────────────────────────────────

static void net_load() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);  // read-only
    String s;
    s = prefs.getString("ssid", "");
    s.toCharArray(net_ssid, sizeof(net_ssid));
    s = prefs.getString("pass", "");
    s.toCharArray(net_pass, sizeof(net_pass));
    s = prefs.getString("tgt_name", "bart");
    s.toCharArray(target_name, sizeof(target_name));
    target_ip[0] = prefs.getUChar("tgt_ip0", 192);
    target_ip[1] = prefs.getUChar("tgt_ip1", 168);
    target_ip[2] = prefs.getUChar("tgt_ip2", 1);
    target_ip[3] = prefs.getUChar("tgt_ip3", 100);
    target_port  = prefs.getUShort("tgt_port", DEFAULT_TARGET_PORT);
    listen_port  = prefs.getUShort("lst_port", DEFAULT_LISTEN_PORT);
    prefs.end();
}

static void net_save() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false); // read-write
    prefs.putString("ssid",     net_ssid);
    prefs.putString("pass",     net_pass);
    prefs.putString("tgt_name", target_name);
    prefs.putUChar("tgt_ip0",   target_ip[0]);
    prefs.putUChar("tgt_ip1",   target_ip[1]);
    prefs.putUChar("tgt_ip2",   target_ip[2]);
    prefs.putUChar("tgt_ip3",   target_ip[3]);
    prefs.putUShort("tgt_port", target_port);
    prefs.putUShort("lst_port", listen_port);
    prefs.end();
}

// ── WiFi connection ─────────────────────────────────────────────────────────

enum NetState { NET_IDLE, NET_CONNECTING, NET_CONNECTED, NET_FAILED };
static NetState _net_state = NET_IDLE;
static unsigned long _net_start = 0;

static NetState net_state() { return _net_state; }

// Begin an async WiFi connection attempt.
static void net_connect() {
    if (!net_has_credentials()) { _net_state = NET_FAILED; return; }
    WiFi.mode(WIFI_STA);
    wl_status_t rc = WiFi.begin(net_ssid, net_pass);
    if (rc == WL_CONNECT_FAILED) { _net_state = NET_FAILED; return; }
    _net_state = NET_CONNECTING;
    _net_start = millis();
}

// Call from loop().  Returns current state.
static NetState net_update() {
    if (_net_state == NET_CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            _net_state = NET_CONNECTED;
        } else if (millis() - _net_start > 15000) {
            WiFi.disconnect();
            _net_state = NET_FAILED;
        }
    } else if (_net_state == NET_CONNECTED) {
        if (WiFi.status() != WL_CONNECTED) {
            _net_state = NET_FAILED;
        }
    }
    return _net_state;
}

// Disconnect and reset state.
static void net_disconnect() {
    WiFi.disconnect();
    _net_state = NET_IDLE;
}

#endif // NETWORK_H
