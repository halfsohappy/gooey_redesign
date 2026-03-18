#ifndef OSC_H
#define OSC_H

#include <MicroOsc.h>
#include <MicroOscUdp.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "bart_hardware.h"
#include "IPAddress.h"
#include "OscClass.h"

IPAddress static_ip;
WiFiUDP Udp;                                // A UDP instance to let us send and receive packets over UDP

MicroOscUdp<1024> osc(&Udp);

void begin_udp(String start_ip, String start_ssid, String start_pass, int start_port){
  if(start_ip == "dhcp"){}
  else{WiFi.config(static_ip.fromString(start_ip));}
  WiFi.begin(start_ssid.c_str(), start_pass.c_str());
  while (WiFi.status() != WL_CONNECTED) {delay(500); Serial.print(".");}

  Serial.println("WiFi connected, IP address:");
  Serial.println(WiFi.localIP());
  Serial.println("Starting UDP");
  Udp.begin(start_port);
  Serial.print("Local port: ");
  Serial.println(start_port);

  OscRegistry& reg = osc_registry();
}

//THE BIG HANDLER
void osc_recieved_msg( MicroOscMessage & osc_msg ){
    bool is_patch = false; bool is_msg = false;
    String address = osc_msg.getOscAddress();
    Serial.print("Received OSC message with address: ");
    Serial.println(address);
    if(address.startsWith("/annieData" + device_adr)){
        Serial.println("Address matches device name prefix, processing...");
        address.replace("/annieData" + device_adr, ""); 
        // remove the device name prefix to get the sub-address
    } else {
        Serial.println("Address does not match device name prefix, ignoring...");
        return;
    }
    //see if next field is "msg" or "patch"
    if(address.startsWith("/msg")){
        Serial.println("Message address detected");
        is_msg = true;
        address.replace("/msg", ""); // remove the "msg" prefix to get the sub-address
    } else if(address.startsWith("/patch")){
        Serial.println("Patch address detected");
        is_patch = true;
        address.replace("/patch", ""); // remove the "patch" prefix to get the sub-address
    } else {
        Serial.println("Unknown address type, ignoring...");
        return;
    }
    // Parse name and command from the remaining address.
    // Valid formats (leading slash already present after the /msg or /patch strip):
    //   /name/command  →  name="name", command="command"
    //   /name          →  name="name", command="assign"  (missing command = assign)
    //   /name/         →  same as /name (trailing slash ignored)
    int last_slash = address.lastIndexOf('/');
    String name_mp, command;
    if (last_slash <= 0) {
        // Only the leading slash exists (or none at all): whole thing is the name.
        name_mp = address.substring(1);
        command = "assign";
    } else if (last_slash == (int)address.length() - 1) {
        // Trailing slash — strip it, treat remainder as name, default command.
        name_mp = address.substring(1, last_slash);
        command = "assign";
    } else {
        name_mp = address.substring(1, last_slash);
        command = address.substring(last_slash + 1);
    }
    if (name_mp.length() == 0) {
        Serial.println("Invalid address: empty name, ignoring...");
        return;
    }
    Serial.print("Name: ");Serial.println(name_mp);Serial.print("Command: ");Serial.println(command);

    OscRegistry& reg = osc_registry();

    if (is_patch) {
        OscPatch* p = reg.get_or_create_patch(name_mp);
        if (!p) {
            Serial.println("Patch registry full, ignoring...");
            return;
        }
        if (command == "assign") {
            // Parse CSV config from OSC payload and merge into the stored patch
            OscMessage csv_params;
            String err;
            if (!csv_params.from_config_str(osc_msg.nextAsString(), &err)) {
                Serial.print("Failed to parse patch config: ");
                Serial.println(err);
                return;
            }
            if (csv_params.exist.ip)   { p->ip = csv_params.ip; p->exist.ip = true; }
            if (csv_params.exist.port) { p->port = csv_params.port; p->exist.port = true; }
            if (csv_params.exist.adr)  { p->osc_address = csv_params.osc_address; p->exist.adr = true; }
            Serial.print("Patch updated: "); Serial.println(name_mp);
        }
    } else if (is_msg) {
        OscMessage* m = reg.get_or_create_msg(name_mp);
        if (!m) {
            Serial.println("Message registry full, ignoring...");
            return;
        }
        //ASSIGN (applies new config values from OSC payload, keeping existing values where not specified)
        if (command == "assign") {
            // Parse CSV config and merge into the registered message
            OscMessage csv_params;
            String err;
            if (!csv_params.from_config_str(osc_msg.nextAsString(), &err)) {
                Serial.print("Failed to parse message config: ");
                Serial.println(err);
                return;
            }
            // Merge: new config values take priority, gaps filled from existing
            *m = csv_params * (*m);
            m->name = name_mp;  // ensure name stays as the registered name
            m->exist.name = true;
            Serial.print("Message updated: "); Serial.println(name_mp);
        }
        
    }
}










// void myOnOscMessageReceived( MicroOscMessage & oscMessage ) {





#endif

// bool accel_en;
// bool gyro_en;

// void myOnOscMessageReceived( MicroOscMessage & osc_msg ) {
//   if(osc_msg.checkOscAddress((name + String("/acceleration")).c_str(), "s")){
//     const char * boo = osc_msg.nextAsString();
//     if(strcmp(boo,"t")){accel_en = true;}
//     else if(strcmp(boo,"f")){accel_en = false;}
//   }
//   else if(osc_msg.checkOscAddress((name + String("/gyroscope")).c_str(), "s")){
//     const char * boo = osc_msg.nextAsString();
//     if(strcmp(boo,"t")){gyro_en = true;}
//     else if(strcmp(boo,"f")){gyro_en = false;}
//   }
//   else if(osc_msg.checkOscAddress((name + String("/punch")).c_str())){
//     punch.setRGB(osc_msg.nextAsInt(),osc_msg.nextAsInt(),osc_msg.nextAsInt());
//   }
// }
