#include "main.h"

OscDest destinations[32]; // support up to 32 different OSC destinations, each with their own IP and port
TaskHandle_t destination_tasks[32]; // keep track of the FreeRTOS tasks for each destination's sending loop
uint8_t num_destinations = 0;
bool setup_destinations[32] = {false}; // keep track of which destination slots have been set up with tasks

void setup_destination_if_needed(uint8_t dest_index) {
    if(setup_destinations[dest_index] == false){
        Serial.println("Destination index is empty, adding new destination");
        setup_destinations[dest_index] = true;
        destinations[dest_index] = OscDest(IPAddress(0,0,0,0), 9999, 0, 1, device_name + "destination/" + String(dest_index) + "/value", destination_tasks[dest_index], 50);
        num_destinations++;
    }
}

void set_destination_ip(uint8_t dest_index, String ip_str) {
    destinations[dest_index].ip_from_string(ip_str);
    Serial.print("Set destination ");
    Serial.print(dest_index);
    Serial.print(" IP to: ");
    Serial.println(destinations[dest_index].ip.toString());
}

void set_destination_port(uint8_t dest_index, unsigned int port) {
    destinations[dest_index].send_port = port;
    Serial.print("Set destination ");
    Serial.print(dest_index);
    Serial.print(" port to: ");
    Serial.println(destinations[dest_index].send_port);
}

void set_destination_period(uint8_t dest_index, unsigned long period_ms) {
    destinations[dest_index].period_ms = period_ms;
    Serial.print("Set destination ");
    Serial.print(dest_index);
    Serial.print(" period to: ");
    Serial.print(destinations[dest_index].period_ms);
    Serial.println(" ms");
}

void set_destination_osc_address(uint8_t dest_index, String osc_address) {
    destinations[dest_index].top_osc_address = osc_address;
    Serial.print("Set destination ");
    Serial.print(dest_index);
    Serial.print(" top OSC address to: ");
    Serial.println(destinations[dest_index].top_osc_address);
}

uint8_t value_name_parser(String val_name){
    if(val_name == "accelX"){ return ACCELX; }
    else if(val_name == "accelY"){ return ACCELY; }
    else if(val_name == "accelZ"){ return ACCELZ; }
    else if(val_name == "accelLength"){ return ACCELLENGTH; }
    else if(val_name == "gyroX"){ return GYROX; }
    else if(val_name == "gyroY"){ return GYROY; }
    else if(val_name == "gyroZ"){ return GYROZ; }
    else if(val_name == "gyroLength"){ return GYROLENGTH; }
    else if(val_name == "baro"){ return BARO; }
    else if(val_name == "eulerX"){ return EULERX; }
    else if(val_name == "eulerY"){ return EULERY; }
    else if(val_name == "eulerZ"){ return EULERZ; }
    else { Serial.println("Unknown value name in configuration, defaulting to accelX"); return ACCELX;}
}



void add_destination_value(uint8_t dest_index, String value_config) {
    // expected format for value_config: "valueName,inMin,inMax,outMin,outMax"
    uint16_t commas[4]; // we expect 4 commas in the string
    commas[0] = value_config.indexOf(',');
    commas[1] = value_config.indexOf(',', commas[0] + 1);
    commas[2] = value_config.indexOf(',', commas[1] + 1);
    commas[3] = value_config.indexOf(',', commas[2] + 1);
    if(commas[0] == -1 || commas[1] == -1 || commas[2] == -1 || commas[3] == -1){
        Serial.println("Invalid value configuration format, ignoring message");
        return;
    }
    String value_name = value_config.substring(0, commas[0]);
    float in_min = value_config.substring(commas[0] + 1, commas[1]).toFloat();
    float in_max = value_config.substring(commas[1] + 1, commas[2]).toFloat();
    float out_min = value_config.substring(commas[2] + 1, commas[3]).toFloat();
    float out_max = value_config.substring(commas[3] + 1).toFloat();
    // for this example, we'll just point all values to data_streams[0] for testing
    if(destinations[dest_index].add_value(&data_streams[value_name_parser(value_name)], in_min, in_max, out_min, out_max, "/" + value_name)){
        Serial.print("Added value to destination ");
        Serial.print(dest_index);
        Serial.print(": ");
        Serial.println(value_name);
    } else {
        Serial.println("Failed to add value (max values reached), ignoring message");
    }
}

// FUNCTION THAT WILL BE CALLED WHEN AN OSC MESSAGE IS RECEIVED:
void my_osc_message_parser( MicroOscMessage& received_osc_message) {
    String address = received_osc_message.getOscAddress();
    Serial.print("Received OSC message with address: ");
    Serial.println(address);
    if(address.startsWith(device_name)){
        Serial.println("Address matches device name prefix, processing...");
        address.replace(device_name, ""); // remove the device name prefix to get the sub-address
    } else {
        Serial.println("Address does not match device name prefix, ignoring...");
        return;
    }
    if(address.startsWith("destination/")){ // if the message is for configuring a destination
        Serial.println("Message is for configuring a destination");
        address.replace("destination/", ""); // remove the "destination/" prefix to get the index and parameter
        uint8_t dest_index;
        if(address.charAt(1) == '/'){
            dest_index = address.charAt(0) - '0'; // convert char to int
        }
        else if(address.charAt(2) == '/'){
            dest_index = (address.substring(0, 2)).toInt(); // convert first two chars to int
        }
        else {
            Serial.println("Invalid destination index in address");
            return;
        }
        String parameter = address.substring(address.indexOf('/') + 1); // get the parameter name
        Serial.print("Destination index: ");
        Serial.println(dest_index);
        Serial.print("Parameter: ");
        Serial.println(parameter);
        if(dest_index >= 32){Serial.println("Invalid destination index, ignoring message");return;}
        
        setup_destination_if_needed(dest_index);

        if(parameter == "ip"){
            if(received_osc_message.getTypeTags()[0] == 's'){
                String ip_str = received_osc_message.nextAsString();
                set_destination_ip(dest_index, ip_str);
            } else {
                Serial.println("Invalid data type for IP parameter, ignoring message");
            }
        }
        else if(parameter == "port"){
            if(received_osc_message.getTypeTags()[0] == 'i'){
                unsigned int port = received_osc_message.nextAsInt();
                set_destination_port(dest_index, port);
            } else {
                Serial.println("Invalid data type for port parameter, ignoring message");
            }
        }
        else if(parameter == "period"){
            if(received_osc_message.getTypeTags()[0] == 'i'){
                unsigned long period = received_osc_message.nextAsInt();
                set_destination_period(dest_index, period);
            } else {
                Serial.println("Invalid data type for period parameter, ignoring message");
            }
        }
        else if(parameter == "oscAddress"){
            if(received_osc_message.getTypeTags()[0] == 's'){
                String osc_addr = received_osc_message.nextAsString();
                set_destination_osc_address(dest_index, osc_addr);
            } else {
                Serial.println("Invalid data type for OSC address parameter, ignoring message");
            }
        }
        else if(parameter == "addValue"){
            if(received_osc_message.getTypeTags()[0] == 's'){
                String config = received_osc_message.nextAsString();
                add_destination_value(dest_index, config);
            } else {
                Serial.println("Invalid data type for addValue parameter, ignoring message");
            }
        }
        else { Serial.println("Unknown parameter in address, ignoring message"); }
    }
   // DO MESSAGE ADDRESS CHECKING AND ARGUMENT GETTING HERE
}
