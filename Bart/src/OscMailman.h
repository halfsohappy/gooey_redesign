#include "main.h"

#define MAX_OSC_VALUES_PER_DEST 16


//New hierarchy idea
//3 types of things: Mailmen, Packages, destinations
//Postmen are update frequencies, can be stopped  and started and possibly other configs (top adresses)
//Packages are values, as many as u like, scaled how u like
//destinations are made of a port, ip, and bottom adress
//could remove all packages going so


class OscDest {
public:
    IPAddress ip = IPAddress(0, 0, 0, 0);
    unsigned int send_port = 9999;
    String sub_osc_address = "";

    OscDest(IPAddress set_ip, unsigned int set_send_port, String set_sub_osc_address) {
        this->ip = set_ip;
        this->send_port = set_send_port;
        this->sub_osc_address = set_sub_osc_address;
    }

    OscDest(String in_string){
        this->ip.fromString(in_string.substring(0, in_string.indexOf(",")));
        this->send_port = in_string.substring(in_string.indexOf(",") + 1, in_string.lastIndexOf(",")).toInt();
        this->sub_osc_address = in_string.substring(in_string.lastIndexOf(",") + 1);
    }

    void ip_from_string(String ip_string){
        this->ip.fromString(ip_string);
    }
    
    void sub_from_string(String sub_osc_address_string){
        this->sub_osc_address = sub_osc_address_string;
    }

};

class OscPackage { // represents a "single" (might change that) scaled value shipped by a mailman to a destination
public:
    float out_bounds[2] = {0,1}; //min, max
    float in_bounds[2] = {0,1}; //min, max
    float last_value = 0.0f;
    float* value_ptr = nullptr; // pointer to the value that will be sent
    float threshold = 0.01f; // minimum change required to trigger a new OSC message
    String pack_name = "default";
    String pack_address = "";


    OscPackage(float* set_value_ptr, float out_min, float out_max) {
        this->value_ptr = set_value_ptr;
        this->out_bounds[0] = out_min;
        this->out_bounds[1] = out_max;
    }

    OscPackage() {
        this->value_ptr = nullptr;
    }

    void set_bounds(float in_min, float in_max, float out_min, float out_max) {
        this->in_bounds[0] = in_min;
        this->in_bounds[1] = in_max;
        this->out_bounds[0] = out_min;
        this->out_bounds[1] = out_max;
    }

    void set_scale(float out_min, float out_max) {
        this->out_bounds[0] = out_min;
        this->out_bounds[1] = out_max;
    }

    float map_value(float input) {
        return out_bounds[0] + (input - in_bounds[0]) * (out_bounds[1] - out_bounds[0]) / (in_bounds[1] - in_bounds[0]);
    }
    
    bool value_changed() {
        float new_value = *(this->value_ptr); // dereference the pointer to get the current value
        if (fabsf(map_value(new_value) - map_value(this->last_value)) > this->threshold) { // threshold to prevent sending too many messages
            this->last_value = new_value;
            return true;
        }
        return false;
    }
};


class OscMail {
public:
    OscPackage* package;
    OscDest* destination;

    OscMail(OscPackage* pkg, OscDest* dest) : package(pkg), destination(dest) {}

    bool operator==(const OscMail& other) const {
        return this->package == other.package && this->destination == other.destination;
    }
};





class OscMailman {
public:
    String mailman_name;
    String top_osc_address;
    TaskHandle_t send_task_handle;
    unsigned int poll_period_ms = 25; // default to 25ms polling
    unsigned int send_period_ms = 50; // default to 50ms between sends
    uint8_t num_mail = 0;
    OscMail* mail_list[32]; // max 32 packages per mailman

    OscMailman(String set_mailman_name, String set_top_osc_address, TaskHandle_t set_send_task_handle) {
        this->mailman_name = set_mailman_name;
        this->top_osc_address = set_top_osc_address;
        this->send_task_handle = set_send_task_handle;
    }

     OscMailman(String set_mailman_name, TaskHandle_t set_send_task_handle) {
        this->mailman_name = set_mailman_name;
        this->send_task_handle = set_send_task_handle;
    }
    
    bool add_mail(OscPackage* package, OscDest* destination) {
        if(num_mail >= 32) return false; // max 32 packages per mailman
        this->mail_list[num_mail] = new OscMail{package, destination};
        num_mail++;
        return true;
    }

    bool package_to_destinations(OscPackage* package, OscDest* destinations[], uint8_t num_new_destinations) {
        for (uint8_t i = 0; i < num_new_destinations; i++) {
            OscMail new_mail(package, destinations[i]);
            bool already_exists = false;
            for (uint8_t j = 0; j < num_mail; j++) {
                if (*mail_list[j] == new_mail) {
                    already_exists = true;
                    break;
                }
            }
            if (!already_exists && num_mail < 32) {
                this->mail_list[num_mail] = new OscMail(package, destinations[i]);
                num_mail++;
            }
        }
        return true;
    }


    

    void broadcast(){
        for(uint8_t i=0; i<num_mail; i++){
            if(mail_list[i]->package->value_changed()) {
                String full_address;
                if(mail_list[i]->package->pack_address == "") {
                    full_address = top_osc_address + mail_list[i]->destination->sub_osc_address;
                }
                else if(mail_list[i]->destination->sub_osc_address.startsWith("/")){
                    full_address = top_osc_address + mail_list[i]->destination->sub_osc_address + mail_list[i]->package->pack_address;}
                else{full_address = top_osc_address + mail_list[i]->destination->sub_osc_address + "/" + mail_list[i]->package->pack_name;}
                osc.sendFloat(full_address.c_str(), mail_list[i]->package->map_value(*(mail_list[i]->package->value_ptr)));
            }
        }
    }

    void begin_sending() {
        if(send_task_handle != nullptr){ //if a task is already running, delete it before starting a new one
            vTaskDelete(send_task_handle);
        }
        xTaskCreate(
            [](void* param) {
                auto* self = static_cast<OscMailman*>(param);
                while (true) {
                    self->broadcast();
                    vTaskDelay(pdMS_TO_TICKS(self->send_period_ms));
                }
            },
            "OscSendTask",
            4096,
            this,
            1,
            &this->send_task_handle
        );
    }
};

