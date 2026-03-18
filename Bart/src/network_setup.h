#ifndef DEVICE_SETUP_H
#define DEVICE_SETUP_H

#include <WiFiProvisioner.h>
#include <cstring>
#include "bart_hardware.h"

void network_config(){
  preferences.end(); // end read-only preferences to switch to read-write
  preferences.begin("device_config", false);
  preferences.clear(); // clear existing preferences

  // Define a custom configuration
  WiFiProvisioner::Config customCfg(
      "annieData Setup",   // Access Point Name
      "annieData Device Setup", // HTML Page Title
      "#E4CBFF",                     // Theme Color
      R"rawliteral(<svg id="Layer_2" data-name="Layer 2" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1159.63 695.78" width="12rem" height="7.2rem"><defs><style>.cls-1 {fill: #e4cbff;}.cls-2 {fill: #fff;}</style></defs><g id="Layer_1-2" data-name="Layer 1"><g><rect class="cls-1" width="1159.63" height="695.78" rx="200" ry="200"/><path class="cls-2" d="M73.13,441.46c.68-56.63,12.13-110.87,37.99-161.45,28.66-56.04,68.33-102.82,121.05-137.84,28.17-18.71,58.53-32.41,92.17-38.08,17.89-3.01,35.77-3.44,53.69-.36.98.17,1.95.43,2.91.7,9.62,2.7,9.77,4.67,3.93,12.36-6.89,9.07-15.56,12.41-26.57,14.14-40.38,6.33-76.65,23.24-110.31,45.93-37.14,25.03-68.45,56.02-94.58,92.45-23.41,32.64-36.74,69.26-44.44,108.24-4.69,23.75-7.31,47.77-6.9,71.96.45,26.24,5.35,51.67,17.67,75.19,7.58,14.48,18.02,25.6,35.13,28.38,11.43,1.86,22.19-.35,32.54-5.18,15.35-7.16,29.27-16.62,42.51-27.05,24.09-18.97,48.17-37.95,69.92-59.69,3.06-3.06,4.02-5.77,3.02-10.08-5.67-24.31-8.02-49.02-6.29-73.91,2.04-29.34,9.78-57.38,21.38-84.36,8.82-20.53,17.69-41.11,32.03-58.57,11.88-14.47,25.98-25.75,45.18-29.17,27.38-4.89,51.17,14.59,51.8,42.44.56,24.45-5.93,47.59-13.78,70.24-17.51,50.56-43.55,96.46-78.26,137.29-2.49,2.93-3.11,5.38-1.83,8.95,6.31,17.55,11.46,35.53,19.77,52.32,6.8,13.73,14.78,17.42,29.7,13.62,15.61-3.98,29.03-12.42,42.23-21.2,6.52-4.33,12.73-9.13,19.19-13.55,2.72-1.86,3.35-3.82,2.65-7.15-8.41-40.03.4-76.08,26.3-107.6,11.73-14.28,26.75-23.6,46.02-23.73,23.83-.16,43.63,17.22,46.94,40.85,2.97,21.19-3.68,40.16-13.8,58.2-9.22,16.43-21.39,30.5-35.42,43.04-4.72,4.22-4.6,4.88.79,8.15,9.82,5.95,20.69,8.21,31.97,8.66,32.14,1.27,62.34-6.94,91.69-19.13,3.23-1.34,6.31-3.07,9.6-4.23,4.34-1.52,5.13-3.68,3.62-8.21-9.13-27.28-9.45-55.01-2.42-82.73,8.07-31.81,27.2-55.99,53.57-74.77,14.01-9.97,29.07-17.53,46.53-19.35,29.1-3.03,56.14,15.98,52.12,51.51-4.09,36.14-21.39,66.54-42.62,95.06-10.7,14.37-23.37,26.89-37.1,38.38-1.21,1.01-3.27,1.82-2.85,3.73.4,1.87,2.59,1.88,4.1,2.38,7.95,2.64,16.13,4.06,24.49,4.43,37.59,1.65,73.9-6.03,109.86-15.61,71.45-19.04,142.81-38.45,214.2-57.7.64-.17,1.27-.4,1.92-.55,5.35-1.28,8.79-.02,9.86,3.91,1.3,4.78-1.7,7.04-5.54,8.64-28.6,11.92-57.16,23.96-85.82,35.73-48.4,19.87-97.05,39.08-146.68,55.72-31.51,10.56-63.73,16.9-97.15,15.38-24.76-1.13-48.2-6.52-68.12-22.37-2.64-2.1-4.48-.94-6.72.35-21.56,12.41-43.73,23.45-67.69,30.62-27.7,8.28-55.69,11.68-84.3,5.51-18.31-3.94-34.56-12.08-48.31-24.82-3.01-2.79-4.88-2.6-7.97-.29-19.01,14.16-38.55,27.49-60.8,36.2-16.14,6.32-32.68,9.1-49.91,5.38-25.3-5.47-40.64-22.1-50.44-44.96-4.34-10.13-7.72-20.58-10.69-31.18-1.55-5.54-1.64-5.56-5.84-1.29-19.63,19.99-39.75,39.45-61.88,56.69-14.09,10.98-28.89,20.91-45.39,28.04-45.89,19.83-90.57,2.44-110.76-43.13-10.65-24.04-15.56-49.43-17.84-75.48-.76-8.65-.88-17.29-1.04-25.98ZM709.96,448.28c.35,2.8.77,7.78,1.62,12.69.86,4.99,1.4,5.08,5.46,1.83,17.33-13.88,31.94-30.21,43.67-49.04,8.58-13.77,15.91-28.18,19.14-44.25.82-4.1,2.5-9.7-.85-12.3-3.03-2.36-7.91.5-11.7,2.12-34.06,14.52-57.45,49.95-57.35,88.96ZM341.74,407.24c1.23-1.42,1.72-1.86,2.06-2.4,18.37-28.65,35.09-58.17,47.38-90.01,6.91-17.91,10.71-36.37,9.34-55.69-.36-5.04-.81-5.3-5.17-2.62-7.29,4.48-12.69,10.76-16.87,18.08-14.71,25.74-26.18,52.76-32.14,81.93-3.33,16.33-5.28,32.79-4.6,50.71ZM514.42,453.37c7.32-6.43,13.27-13.73,18.15-21.88,3.86-6.44,7.17-13.12,8.54-20.58.25-1.39.83-3.15-.75-4.11-1.56-.95-2.87.3-4.02,1.18-14.87,11.47-20.13,27.62-21.92,45.4Z"/></g></g></svg>)rawliteral", // SVG Logo
      "Device Setup", // Project Title
      "hooray!",       // Project Sub-title
      "Configure Network, Static IP, Port, and Device Name",                             // Project Information
      "Father died a year ago today :'(", // Footer
                                                      // Text
      "The device is now configured!", // Success Message
      "This action will erase all stored settings, including "
      "API key.", // Reset Confirmation Text
      "Static IP (or 'dhcp')",  // Input Field Text
      15,          // Input Field Length
      true,
      "Port",  // Second Input Text
      5,          // Second Input Length
      true,
      "Device Name (for OSC address)",  // Third Input Text
      32,         // Third Input Length
      true,       // Show Input Field
      false        // Show Reset Field
  );

  // Create the WiFiProvisioner instance with the custom configuration
  WiFiProvisioner provisioner(customCfg);

  // Set up callbacks
  provisioner.onProvision([]() { Serial.println("Provisioning started."); })
      .onInputCheck([](const char *input1, const char *input2, const char *input3) -> bool {
        //test if ip is valid format or is "dhcp"
        if (input1) {
          String ipStr(input1);
          if (ipStr != "dhcp") {
            IPAddress ip;
            if (!ip.fromString(ipStr)) {
              Serial.println("Invalid IP address format.");
              return false;
            }
          }
          else {
            Serial.println("Using DHCP for IP configuration.");
          }
        }
        //test if port is valid number between 1 and 65535
        if (input2) {     
          int port = atoi(input2);
          if (port < 1 || port > 65535) {
            Serial.println("Invalid port number.");
            return false;
          }
        }
        // simple validations for demonstration
        // if (input1 && strlen(input1) != 0) return true;
        // if (input2 && strlen(input2) != 6) return false;
        // if (input3 && strlen(input3) != 12) return false;
        return true;
      })
      .onSuccess([](const char *ssid, const char *password, const char *input1, const char *input2, const char *input3) {
        Serial.printf("Connected to SSID: %s\n", ssid);
        preferences.putString("ssid", ssid);
        if (password) {
          Serial.printf("Password: %s\n", password);
          preferences.putString("network_password", password);
        }
        if (input1) {
          Serial.printf("IP: %s\n", input1);
          if(strcmp(input1, "dhcp") == 0) {
            //preferences.putString("static_ip", "dhcp");
            preferences.putBool("use_dhcp", true);
          }
          else {
            preferences.putString("static_ip", input1);
            preferences.putBool("use_dhcp", false);
          }
        }
        if (input2) {
          Serial.printf("Input2: %s\n", input2);
          preferences.putInt("port", atoi(input2));
        }
        if (input3) {
          Serial.printf("Input3: %s\n", input3);
          preferences.putString("device_adr", input3);
        }
        preferences.putBool("provisioned", true);
        preferences.end(); // commit all NVS writes to flash before restart
        Serial.println("Provisioning completed successfully!");
        delay(2000); // Optional: delay to allow user to read the success message
        ESP.restart(); // Restart the device to apply new settings
      })
      .onFactoryReset([]() { Serial.println("Factory reset triggered!"); });

  // Start provisioning
  provisioner.startProvisioning();


}

void reset_network_config(){
  preferences.begin("device_config", false);
  preferences.clear();
  preferences.end();
}

#endif



