#include <SparkFun_MMC5983MA_Arduino_Library.h>
#include "SparkFun_ISM330DHCX.h"

#define ACCEL_NORM 2.0
#define GYRO_NORM 250.0
SparkFun_ISM330DHCX_SPI IMU; 
SFE_MMC5983MA mmc;

sfe_ism_data_t accel_data;
sfe_ism_data_t gyro_data;
double mag_data[3];

typedef struct{
    float xData;
    float yData;
    float zData;
    float length;
} norm_imu_data;
norm_imu_data my_a_data;
norm_imu_data my_g_data;



void begin_imu(int16_t ICS, int16_t MCS){
    pinMode(ICS, OUTPUT);
    digitalWrite(ICS, HIGH);
    delay(100);
	if( !IMU.begin(ICS) ){
		Serial.println("Did not begin.");
	  while(1);
	}
    mmc.begin(MCS);

    IMU.setDeviceConfig();
    IMU.setBlockDataUpdate();
    // Set the output data rate and precision of the accelerometer
    IMU.setAccelDataRate(ISM_XL_ODR_104Hz);
    IMU.setAccelFullScale(ISM_2g); 
    // Set the output data rate and precision of the gyroscope
    IMU.setGyroDataRate(ISM_GY_ODR_104Hz);
    IMU.setGyroFullScale(ISM_250dps); 
    // Turn on the accelerometer's filter and apply settings. 
    IMU.setAccelFilterLP2();
    IMU.setAccelSlopeFilter(ISM_LP_ODR_DIV_100);
    // Turn on the gyroscope's filter and apply settings. 
    IMU.setGyroFilterLP1();
    IMU.setGyroLP1Bandwidth(ISM_MEDIUM);
}

void process_imu_data(sfe_ism_data_t *sdata, norm_imu_data *mydata, float div_norm, bool gravity_comp){
    Serial.println(sdata->xData / div_norm);
    mydata->xData = sdata->xData / div_norm;
    mydata->yData = sdata->yData / div_norm;
    mydata->zData = sdata->zData / div_norm;
    mydata->length = sqrt(mydata->xData*mydata->xData + mydata->yData*mydata->yData + mydata->zData*mydata->zData) - gravity_comp*.5;
    Serial.println(mydata->length);
}

void normalize_in_place(sfe_ism_data_t *adata, float div_norm){
    adata->xData = adata->xData / div_norm;
    adata->yData = adata->yData / div_norm;
    adata->zData = adata->zData / div_norm;
}