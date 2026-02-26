// #include "main.h"

// // Structs for X,Y,Z data
// // sfe_ism_data_t accel_data; 
// // sfe_ism_data_t gyro_data; 
// // double mag_data[3];


// void setup() {
//   Serial.begin(115200);
//   begin_pins(0,0,0,0);
//   SPI.begin(SCK, SDO, SDI, CS_IMU);
//   Serial.println("IM ALIVE!");
//   begin_baro(CS_BAR);
//   begin_imu(CS_IMU, CS_MAG);
//  // begin_udp();


//   start_point = 100 + bmp.readAltitude(SEALEVELPRESSURE_HPA);
//   max_yet = start_point + .4;

// }

// void loop() {
//   EVERY_N_MILLISECONDS(20) {
//     mag_data[0] = M_PI * ((mmc.getMeasurementX() - 131072.0) / 131072.0);
//     mag_data[1] = M_PI * ((mmc.getMeasurementY() - 131072.0) / 131072.0);
//     mag_data[2] = M_PI * ((mmc.getMeasurementZ() - 131072.0) / 131072.0);


//     bmp.performReading();

//     float decimal;

//     Serial.print("Baro: "); Serial.println(bmp.readAltitude(SEALEVELPRESSURE_HPA));
//     float hold = 100 + bmp.readAltitude(SEALEVELPRESSURE_HPA);
//     if(max_yet < hold){max_yet = hold;}
//     if(hold < start_point){decimal = 0;}
//     else{decimal = map(hold*100.0, start_point*100.0, max_yet*100.0, 0, 1000) / 1000.0;}
//     printf("Lowest %0.1f, Highest %0.1f, Point %0.1f\n", start_point, max_yet, decimal);
//     //osc.sendFloat("/annieData/test/elevation", decimal);
//     Serial.println(""); 
//     Serial.println(decimal); 
//     IMU.getAccel(&accel_data);
//     Serial.println(accel_data.xData);
//     //normalize_in_place(&accel_data, ACCEL_NORM);
//     IMU.getGyro(&gyro_data);
//     normalize_in_place(&gyro_data, GYRO_NORM);
//     // process_imu_data(&gyro_data, &my_g_data, GYRO_NORM, TRUE);
//     process_imu_data(&accel_data, &my_a_data, ACCEL_NORM, TRUE);
//     // osc.sendMessage((name + String("acceleration")).c_str(), "ffff", my_a_data.xData, my_a_data.yData, my_a_data.zData, my_a_data.length);
//     // osc.sendMessage((name + String("gyroscope")).c_str(), "ffff", my_g_data.xData, my_g_data.yData, my_g_data.zData, my_g_data.length);
//     //osc.sendMessage("/annieData/test/acceleration", "ffff", accel_data.xData / 2, accel_data.yData / 2, accel_data.zData / 2, sqrt((accel_data.xData / 2)*(accel_data.xData / 2) + (accel_data.yData / 2)*(accel_data.yData / 2) + (accel_data.zData / 2)*(accel_data.zData / 2)) - .5);
//    // osc.sendMessage("/annieData/test/gyroscope", "fff", gyro_data.xData, gyro_data.yData, gyro_data.zData);

//   }
// }


// void accel_osc(float aX, float aY, float aZ){
//   aX = aX / 2.0;
//   aY = aY / 2.0;
//   aZ = aZ / 2.0;
//   float norm = sqrt(aX*aX + aY*aY + aZ*aZ);
//   osc.sendMessage((device_name + String("acceleration")).c_str(), "ffff", aX, aY, aZ, norm);
// }

// void gyro_osc(float gX, float gY, float gZ){
//   gX = gX / 2.0;
//   gY = gY / 2.0;
//   gZ = gZ / 2.0;
//   osc.sendMessage((device_name + String("gyroscope")).c_str(), "fff", gX, gY, gZ);
// }







// /*Confirming Value ranges: 
// 1-axis:
// gross (f 0. to 1.)
// elevation (f 0. to 1.)

// 3-axes:
// acceleration (f -1. to 1.)
// gyroscope (i -180 to 180) 
// magnetometer (f 0. to 1.)
// orientation (i -180 to 180)
// quaternion (i -180 to 180)*/


#include <Arduino.h>
#include <WiFiProvisioner.h>

void setup() {
  Serial.begin(9600);

  // Create the WiFiProvisioner instance
  WiFiProvisioner provisioner;

  // Configure to hide additional fields
  provisioner.getConfig().SHOW_INPUT_FIELD = true; // No additional input field
  provisioner.getConfig().SHOW_RESET_FIELD = false; // No reset field

  // Set the success callback
  provisioner.onSuccess(
      [](const char *ssid, const char *password, const char *input1, const char *input2, const char *input3) {
        Serial.printf("Provisioning successful! Connected to SSID: %s\n", ssid);
        if (password) {
          Serial.printf("Password: %s\n", password);
        }
        if (input1) {
          Serial.printf("Input1: %s\n", input1);
        }
        if (input2) {
          Serial.printf("Input2: %s\n", input2);
        }
        if (input3) {
          Serial.printf("Input3: %s\n", input3);
        }
      });

  // Start provisioning
  provisioner.startProvisioning();
}

void loop() { delay(100); }
