#ifndef BC127_NETWORK_SETUP_H
#define BC127_NETWORK_SETUP_H

// =============================================================================
// network_setup.h — WiFi captive-portal provisioning for bc127 DMX controller
// =============================================================================

#include <WiFiProvisioner.h>
#include <Preferences.h>

extern Preferences preferences;

void network_config() {
    preferences.end();
    preferences.begin("device_config", false);
    preferences.clear();

    WiFiProvisioner::Config customCfg(
        "annieData DMX Setup",
        "annieData DMX Controller Setup",
        "#E4CBFF",
        R"rawliteral(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 60" width="8rem" height="4.8rem"><rect width="100" height="60" rx="12" fill="#E4CBFF"/><text x="50" y="28" text-anchor="middle" font-family="monospace" font-size="11" font-weight="bold" fill="#2A2F36">annieData</text><text x="50" y="42" text-anchor="middle" font-family="monospace" font-size="8" fill="#6C7A89">DMX512</text></svg>)rawliteral",
        "DMX Controller",
        "bc127",
        "Configure WiFi, Static IP, and OSC Port",
        "",
        "DMX controller configured!",
        "This will erase all stored settings.",
        "Static IP (or 'dhcp')",
        15, true,
        "OSC Port",
        5, true,
        "",
        0, false,
        false
    );

    WiFiProvisioner provisioner(customCfg);

    provisioner.onProvision([]() { Serial.println("Provisioning started."); })
        .onInputCheck([](const char *input1, const char *input2, const char *input3) -> bool {
            if (input1) {
                String ipStr(input1);
                if (ipStr != "dhcp") {
                    IPAddress ip;
                    if (!ip.fromString(ipStr)) {
                        Serial.println("Invalid IP address format.");
                        return false;
                    }
                }
            }
            if (input2) {
                int port = atoi(input2);
                if (port < 1 || port > 65535) {
                    Serial.println("Invalid port number.");
                    return false;
                }
            }
            return true;
        })
        .onSuccess([](const char *ssid, const char *password, const char *input1, const char *input2, const char *input3) {
            Serial.printf("Connected to SSID: %s\n", ssid);
            preferences.putString("ssid", ssid);
            if (password) {
                preferences.putString("net_pass", password);
            }
            if (input1) {
                if (strcmp(input1, "dhcp") == 0) {
                    preferences.putBool("use_dhcp", true);
                } else {
                    preferences.putString("static_ip", input1);
                    preferences.putBool("use_dhcp", false);
                }
            }
            if (input2) {
                preferences.putInt("port", atoi(input2));
            }
            preferences.putBool("provisioned", true);
            preferences.end();
            Serial.println("Provisioning completed!");
            delay(2000);
            ESP.restart();
        })
        .onFactoryReset([]() { Serial.println("Factory reset triggered!"); });

    provisioner.startProvisioning();
}

void reset_network_config() {
    preferences.begin("device_config", false);
    preferences.clear();
    preferences.end();
}

#endif // BC127_NETWORK_SETUP_H
