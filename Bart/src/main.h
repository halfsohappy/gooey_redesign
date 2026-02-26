#ifndef _MAIN_H_
#define _MAIN_H_

#include <FastLED.h>
#include <MicroOsc.h>
#include <MicroOscUdp.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BMP5xx.h"
#include <WiFi.h>
#include <cstring>
#include <esp_wifi.h>
#include <SparkFun_MMC5983MA_Arduino_Library.h>
#include "SparkFun_ISM330DHCX.h"
#include <SensorFusion.h>
#include <Filters.h>



#define NUM_DATA_STREAMS 12
#define ACCELX 0
#define ACCELY 1
#define ACCELZ 2
#define ACCELLENGTH 3
#define GYROX 4
#define GYROY 5
#define GYROZ 6
#define GYROLENGTH 7
#define BARO 8
#define EULERX 9
#define EULERY 10
#define EULERZ 11

CRGB led[5];
long change;
long old;
float start_point;
float max_yet;

float data_streams[NUM_DATA_STREAMS]; // accel x,y,z, accel length, gyro x,y,z, gyro length, baro, euler x,y,z

#include "bart_def.h"
#include "bart_imu.h"
#include "bart_baro.h"
#include "OSC.h"
#include "OscMailman.h"
#include "OscRoute.h"

void setup();
void loop();

#endif  // _MAIN_H_
