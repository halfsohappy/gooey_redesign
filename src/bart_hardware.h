#ifndef BART_HARDWARE_H
#define BART_HARDWARE_H

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BMP5xx.h"
#include <SparkFun_MMC5983MA_Arduino_Library.h>
#include "SparkFun_ISM330DHCX.h"
#include <FastLED.h>
#include <Preferences.h>

static constexpr int SDO_PIN = 37;
static constexpr int SCK_PIN = 36;
static constexpr int SDI_PIN = 35;
static constexpr int CS_IMU = 42;
static constexpr int CS_MAG = 39;
static constexpr int CS_UWB = 38;
static constexpr int CS_BAR = 48;
static constexpr int INT_IMU = 41;
static constexpr int INT_BAR = 34;
static constexpr int NEO = 21;
static constexpr int UMON = 7;
static constexpr int BMON = 8;
static constexpr int SEL13 = 11;
static constexpr int SEL46 = 12;
static constexpr int CC_EN1 = 13;
static constexpr int CC_EN2 = 14;
static constexpr int CC_PWM1 = 9;
static constexpr int CC_PWM2 = 10;

#define SEALEVELPRESSURE_HPA (1013.25)
#define ACCEL_NORM 2.0
#define GYRO_NORM 250.0

extern Preferences preferences;

extern Adafruit_BMP5xx bmp;
extern bmp5xx_powermode_t desiredMode;
extern SparkFun_ISM330DHCX_SPI IMU;
extern SFE_MMC5983MA mmc;

extern sfe_ism_data_t accel_data;
extern sfe_ism_data_t gyro_data;
extern uint32_t mag_data[3];

struct norm_imu_data {
    float xData;
    float yData;
    float zData;
    float length;
};

extern norm_imu_data my_a_data;
extern norm_imu_data my_g_data;

void begin_pins(bool b13, bool b46, bool cen1, bool cen2);
void begin_baro(uint16_t BCS);
void begin_imu(int16_t ICS, int16_t MCS);
void process_imu_data(sfe_ism_data_t *sdata, norm_imu_data *mydata, float div_norm, bool gravity_comp);
void normalize_in_place(sfe_ism_data_t *adata, float div_norm);

#endif
