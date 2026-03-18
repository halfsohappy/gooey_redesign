#include "main.h"



void setup() {
  Serial.begin(115200);
  begin_pins(0,0,0,0);
  SPI.begin(SCK_PIN, SDO_PIN, SDI_PIN, CS_IMU);
  Serial.println("IM ALIVE!");
  begin_baro(CS_BAR);
  begin_imu(CS_IMU, CS_MAG);

  preferences.begin("device_config", true);
  if(preferences.getBool("provisioned", false)){
    begin_udp(preferences.getString("ip"),preferences.getString("ssid"),preferences.getString("network_password"),preferences.getInt("port"));

    device_adr = preferences.getString("device_adr");
    //device name must always begin with a slash but not end with a slash
    if (!device_adr.startsWith("/")){
      // ensure device name starts with a slash for consistent OSC address formatting
      device_adr = "/" + device_adr;
    }
    if(device_adr.endsWith("/")){
      // remove trailing slash if present to avoid double slashes in OSC addresses
      device_adr.remove(device_adr.length() - 1);
    }
  }
  else {
    Serial.println("Device not provisioned, starting provisioning process...");
    network_config();
  }
  preferences.end();

  //create new task for sensor reading and processing, so that it can run independently of the main loop and not be blocked by any long-running operations in the main loop
  xTaskCreate([](void*){
    while(true){
      //read and process IMU data
      IMU.getAccel(&accel_data);
      IMU.getGyro(&gyro_data);
      mmc.getMeasurementXYZ(&mag_data[0], &mag_data[1], &mag_data[2]);
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }, "sensor_task", 4096, nullptr, 1, nullptr);

  //
}

void loop() {
  osc.onOscMessageReceived(osc_recieved_msg);
}

