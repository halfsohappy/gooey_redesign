#include <MicroOsc.h>
#include <MicroOscUdp.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#define MAX_OSC_VALUES_PER_DEST 16


class OscValue { // represents a single value that can be sent via OSC, along with its mapping and change detection logic
public:
    float out_bounds[2]; //min, max
    float in_bounds[2]; //min, max
    float last_value = 0.0f;
    float* value_ptr; // pointer to the value that will be sent
    String sub_address;

    OscValue(float* set_value_ptr, float in_min, float in_max, float out_min, float out_max, String set_sub_address) {
        this->value_ptr = set_value_ptr;
        this->in_bounds[0] = in_min;
        this->in_bounds[1] = in_max;
        this->out_bounds[0] = out_min;
        this->out_bounds[1] = out_max;
        this->sub_address = set_sub_address;
        last_value = 0.0f;
    }
    OscValue() {
        this->value_ptr = nullptr;
        this->sub_address = "";
    }

    bool is_empty() {

    }



    void set_bounds(float in_min, float in_max, float out_min, float out_max) {
        this->in_bounds[0] = in_min;
        this->in_bounds[1] = in_max;
        this->out_bounds[0] = out_min;
        this->out_bounds[1] = out_max;
    }
    float map_value(float input) {
        return out_bounds[0] + (input - in_bounds[0]) * (out_bounds[1] - out_bounds[0]) / (in_bounds[1] - in_bounds[0]);
    }
    bool value_changed() {
        float new_value = *(this->value_ptr); // dereference the pointer to get the current value
            if (fabsf(map_value(new_value) - map_value(this->last_value)) > 0.01f) { // simple threshold to prevent sending too many messages
                this->last_value = new_value;
                return true;
            }
                return false;
    }
};

class OscDest {
public:
    IPAddress ip = IPAddress(0, 0, 0, 0);
    unsigned int send_port = 9999;
    String top_osc_address;
    TaskHandle_t send_task_handle;
    unsigned int period_ms = 50; // default to 50ms polling
    OscValue values[MAX_OSC_VALUES_PER_DEST];
    uint8_t num_values = 0;
    //pointer to the value that will be sent
    

     OscDest(IPAddress set_ip, unsigned int set_send_port, float set_bound_min, float set_bound_max, String set_osc_address, TaskHandle_t set_send_task_handle, unsigned int set_period_ms) {
        this->ip = set_ip;
        this->send_port = set_send_port;
        this->send_task_handle = set_send_task_handle;
        this->top_osc_address = set_osc_address;
        this->period_ms = set_period_ms;
        num_values = 0;
    }

    OscDest(){
        this->ip = IPAddress(0, 0, 0, 0);
        this->send_port = 9999;
        this->send_task_handle = nullptr;
        this->top_osc_address = "/default/";
        num_values = 0;
        period_ms = 50;
    }

    void set_ip(int a, int b, int c, int d) {
        ip = IPAddress(a, b, c, d);
    }
    void ip_from_string(String ip_str) {
        ip.fromString(ip_str);
    }

    bool add_value(float* value_ptr, float in_min, float in_max, float out_min, float out_max, String sub_address) {
        if (num_values >= 16) return false; // max 16 values per destination
        values[num_values] = OscValue(value_ptr, in_min, in_max, out_min, out_max, sub_address);
        num_values++;
        return true;
    }

    void broadcast(){
        for(uint8_t i=0; i<num_values; i++){
            if(values[i].value_changed()) {
                String full_address = top_osc_address + values[i].sub_address;
                osc.sendFloat(full_address.c_str(), values[i].map_value(*(values[i].value_ptr)));
            }
        }
    }

    void begin_sending() {
        if(send_task_handle != nullptr){ //if a task is already running, delete it before starting a new one
            vTaskDelete(send_task_handle);
        }
        xTaskCreate(
            [](void* param) {
                auto* self = static_cast<OscDest*>(param);
                while (true) {
                    self->broadcast();
                    vTaskDelay(pdMS_TO_TICKS(self->period_ms)); //default to 50ms polling
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
