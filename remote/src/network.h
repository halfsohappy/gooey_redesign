#ifndef NETWORK_H
#define NETWORK_H

// ============================================================================
//  TheaterGWD Setup Remote — WiFi Connection, NVS Storage & Provisioning
// ============================================================================
//
//  On first boot (no credentials) the device enters AP provisioning mode
//  via the WiFiProvisioner library — a captive portal lets the user enter
//  WiFi SSID / password plus target-device details from any browser.
//
//  On subsequent boots the saved credentials are loaded from NVS and the
//  device connects directly to the configured network.
//
// ============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <WiFiProvisioner.h>
#include "config.h"

// ── Persisted settings ──────────────────────────────────────────────────────

static char    net_ssid[33]       = "";
static char    net_pass[65]       = "";
static char    target_name[MAX_INPUT_LEN] = "bart";
static uint8_t target_ip[4]      = {192, 168, 1, 100};
static uint16_t target_port      = DEFAULT_TARGET_PORT;
static uint16_t listen_port      = DEFAULT_LISTEN_PORT;
static bool    _provisioned      = false;

// ── Helpers ─────────────────────────────────────────────────────────────────

static IPAddress target_ip_addr() {
    return IPAddress(target_ip[0], target_ip[1], target_ip[2], target_ip[3]);
}

static bool net_has_credentials() {
    return net_ssid[0] != 0;
}

static bool net_is_provisioned() {
    return _provisioned;
}

// ── NVS load / save ─────────────────────────────────────────────────────────

static Preferences _net_prefs;

static void net_load() {
    _net_prefs.begin(NVS_NAMESPACE, true);  // read-only
    _provisioned = _net_prefs.getBool("provisioned", false);
    String s;
    s = _net_prefs.getString("ssid", "");
    s.toCharArray(net_ssid, sizeof(net_ssid));
    s = _net_prefs.getString("pass", "");
    s.toCharArray(net_pass, sizeof(net_pass));
    s = _net_prefs.getString("tgt_name", "bart");
    s.toCharArray(target_name, sizeof(target_name));
    target_ip[0] = _net_prefs.getUChar("tgt_ip0", 192);
    target_ip[1] = _net_prefs.getUChar("tgt_ip1", 168);
    target_ip[2] = _net_prefs.getUChar("tgt_ip2", 1);
    target_ip[3] = _net_prefs.getUChar("tgt_ip3", 100);
    target_port  = _net_prefs.getUShort("tgt_port", DEFAULT_TARGET_PORT);
    listen_port  = _net_prefs.getUShort("lst_port", DEFAULT_LISTEN_PORT);
    _net_prefs.end();
}

static void net_save() {
    _net_prefs.begin(NVS_NAMESPACE, false); // read-write
    _net_prefs.putBool("provisioned", true);
    _net_prefs.putString("ssid",     net_ssid);
    _net_prefs.putString("pass",     net_pass);
    _net_prefs.putString("tgt_name", target_name);
    _net_prefs.putUChar("tgt_ip0",   target_ip[0]);
    _net_prefs.putUChar("tgt_ip1",   target_ip[1]);
    _net_prefs.putUChar("tgt_ip2",   target_ip[2]);
    _net_prefs.putUChar("tgt_ip3",   target_ip[3]);
    _net_prefs.putUShort("tgt_port", target_port);
    _net_prefs.putUShort("lst_port", listen_port);
    _net_prefs.end();
}

// ── WiFi provisioning (captive portal) ──────────────────────────────────────

// Blocks until the user completes provisioning via the captive portal,
// then saves credentials and restarts.
static void net_provision() {
    _net_prefs.begin(NVS_NAMESPACE, false);
    _net_prefs.clear();

    WiFiProvisioner::Config cfg(
        "annieData Remote",              // AP name
        "Remote Setup",                  // page title
        "#E4CBFF",                       // theme colour
        "",                              // logo (none for remote)
        "Setup Remote",                  // project title
        "",                              // subtitle
        "Enter WiFi credentials and target device info", // info
        "",                              // footer
        "The remote is now configured!", // success message
        "",                              // reset text
        "Target IP",                     // input 1 label
        15,                              // input 1 max length
        true,                            // show input 1
        "Target Port",                   // input 2 label
        5,                               // input 2 max length
        true,                            // show input 2
        "Device Name (for OSC address)", // input 3 label
        32,                              // input 3 max length
        true,                            // show input 3
        false                            // show reset button
    );

    WiFiProvisioner provisioner(cfg);

    provisioner
        .onProvision([]() {
            Serial.println("Provisioning started.");
        })
        .onInputCheck([](const char* ip_str, const char* port_str,
                         const char* /*name*/) -> bool {
            if (ip_str) {
                IPAddress ip;
                if (!ip.fromString(ip_str)) {
                    Serial.println("Invalid IP address.");
                    return false;
                }
            }
            if (port_str) {
                int p = atoi(port_str);
                if (p < 1 || p > 65535) {
                    Serial.println("Invalid port.");
                    return false;
                }
            }
            return true;
        })
        .onSuccess([](const char* ssid, const char* password,
                      const char* ip_str, const char* port_str,
                      const char* name) {
            Serial.printf("Provisioned — SSID: %s\n", ssid);
            _net_prefs.putString("ssid", ssid);
            if (password) _net_prefs.putString("pass", password);
            if (ip_str) {
                IPAddress ip;
                if (ip.fromString(ip_str)) {
                    _net_prefs.putUChar("tgt_ip0", ip[0]);
                    _net_prefs.putUChar("tgt_ip1", ip[1]);
                    _net_prefs.putUChar("tgt_ip2", ip[2]);
                    _net_prefs.putUChar("tgt_ip3", ip[3]);
                }
            }
            if (port_str) _net_prefs.putUShort("tgt_port", atoi(port_str));
            if (name)     _net_prefs.putString("tgt_name", name);
            _net_prefs.putBool("provisioned", true);
            _net_prefs.end();
            Serial.println("Provisioning complete — restarting...");
            delay(2000);
            ESP.restart();
        });

    provisioner.startProvisioning();  // blocks until complete
}

// Clear all saved credentials and reboot into provisioning mode.
static void net_clear_provision() {
    _net_prefs.begin(NVS_NAMESPACE, false);
    _net_prefs.clear();
    _net_prefs.end();
    ESP.restart();
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
