#include "main.h"

//New hierarchy idea
//3 types of things: Mailmen, Packages, destinations
//Postmen are update frequencies, can be stopped and started and possibly other configs (top adresses)
//Packages are values, as many as u like, scaled how u like
//destinations are made of a port, ip, and bottom adress
//could remove all packages going so


OscMailman* mailmen[16]; // max 16 mailmen (arbitrary limit for now, could be changed)
TaskHandle_t mail_tasks[16]; // keep track of mailman task handles for starting/stopping
uint8_t num_mailmen = 0;

OscDest* destinations[64]; // max 64 destinations (arbitrary limit for now, could be changed)
bool setup_destinations[64] = {false}; // keep track of which destination slots are filled
uint8_t num_destinations = 0;

OscPackage* packages[128]; // max 128 packages (arbitrary limit for now, could be changed)
uint8_t num_packages = 0;

//Search for mailman based on name. If it doesn't exist, create it with default values and add it to the list. Return a pointer to the mailman.
OscMailman* get_or_create_mailman(String mailman_name, String top_osc_address = "") {
    for(uint8_t i=0; i<num_mailmen; i++){
        if(mailmen[i]->mailman_name == mailman_name){
            return mailmen[i]; // mailman already exists, return it
        }
    }
    // mailman doesn't exist, create it
    if(num_mailmen >= 16) return nullptr; // max mailmen reached, return null
    mail_tasks[num_mailmen] = nullptr;
    mailmen[num_mailmen] = new OscMailman(mailman_name, top_osc_address, mail_tasks[num_mailmen]);
    num_mailmen++;
    return mailmen[num_mailmen - 1];
}






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

String remove_mail_signifier(String address){
    if(address.startsWith("mailman/")){address.replace("mailman/", ""); } //I AM WINNING WOKE OF THE DAY
    else if(address.startsWith("mailwoman/")){address.replace("mailwoman/", "");}
    else if(address.startsWith("mailperson/")){address.replace("mailperson/", "");}
    return address;
}

void mailman_config(OscMailman* mailman, String remaining_msg){

}

void destination_config(OscDest* destination, String remaining_msg){  

}

void package_config(OscPackage* package, String remaining_msg){

}

// FUNCTION THAT WILL BE CALLED WHEN AN OSC MESSAGE IS RECEIVED:
void my_osc_message_parser( MicroOscMessage& received_osc_message) {
    String address = received_osc_message.getOscAddress();
    Serial.print("Received OSC message with address: ");
    Serial.println(address);


    //check device name
    if(address.startsWith(device_name)){
        Serial.println("Address matches device name prefix, processing...");
        address.replace(device_name, ""); // remove the device name prefix to get the sub-address
    } else {
        Serial.println("Address does not match device name prefix, ignoring...");
        return;
    }

    //is it a mailman config?
    if(address.startsWith("mailman/") || address.startsWith("mailwoman/") || address.startsWith("mailperson/")){ // if the message is for configuring a mailman
        Serial.println("Message is for configuring a mailman");
        String mailman_name = remove_mail_signifier(address); // get the mailman name from the address
        OscMailman* mailman = get_or_create_mailman(mailman_name);
        if(mailman == nullptr){
            Serial.println("Failed to create mailman (max mailmen reached), ignoring message");
            return;    
        }
        String remaining_msg = address.substring(address.indexOf('/') + 1); // get the remaining message after the mailman name
        mailman_config(mailman, remaining_msg); // process the mailman configuration message
        return;
    }
    //is it a destination config?
    if(address.startsWith("destination/")){ // if the message is for configuring a destination
        Serial.println("Message is for configuring a destination");
        address.replace("destination/", ""); // remove the "destination/" prefix to get the index and parameter
        
   // DO MESSAGE ADDRESS CHECKING AND ARGUMENT GETTING HERE
}
