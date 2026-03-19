#include <Preferences.h>
#include "bart_hardware.h"

Adafruit_BMP5xx bmp;
bmp5xx_powermode_t desiredMode = BMP5XX_POWERMODE_CONTINUOUS;
SparkFun_ISM330DHCX_SPI IMU;
SFE_MMC5983MA mmc;

Preferences preferences;

sfe_ism_data_t accel_data;
sfe_ism_data_t gyro_data;
uint32_t mag_data[3];

norm_imu_data my_a_data;
norm_imu_data my_g_data;

void begin_pins(bool b13, bool b46, bool cen1, bool cen2) {
    pinMode(CS_MAG, OUTPUT);
    pinMode(CS_IMU, OUTPUT);
    pinMode(SEL13, OUTPUT);
    pinMode(SEL46, OUTPUT);
    pinMode(CC_EN1, OUTPUT);
    pinMode(CC_EN2, OUTPUT);
    digitalWrite(SEL13, b13);
    digitalWrite(SEL46, b46);
    digitalWrite(CC_EN1, cen1);
    digitalWrite(CC_EN2, cen2);
}

void begin_baro(uint16_t BCS) {
    bmp.begin(BCS, &SPI);
    bmp.setTemperatureOversampling(BMP5XX_OVERSAMPLING_1X);
    bmp.setPressureOversampling(BMP5XX_OVERSAMPLING_32X);
    bmp.setIIRFilterCoeff(BMP5XX_IIR_FILTER_COEFF_15);
    bmp.setOutputDataRate(BMP5XX_ODR_45_HZ);
    bmp.setPowerMode(desiredMode);
    bmp.enablePressure(true);
    bmp.configureInterrupt(BMP5XX_INTERRUPT_LATCHED, BMP5XX_INTERRUPT_ACTIVE_HIGH, BMP5XX_INTERRUPT_PUSH_PULL, BMP5XX_INTERRUPT_DATA_READY, true);
    delay(5);
}

void begin_imu(int16_t ICS, int16_t MCS) {
    pinMode(ICS, OUTPUT);
    digitalWrite(ICS, HIGH);
    delay(100);
    if (!IMU.begin(ICS)) {
        Serial.println("Did not begin.");
        while (1) {}
    }
    mmc.begin(MCS);

    IMU.setDeviceConfig();
    IMU.setBlockDataUpdate();
    IMU.setAccelDataRate(ISM_XL_ODR_104Hz);
    IMU.setAccelFullScale(ISM_2g);
    IMU.setGyroDataRate(ISM_GY_ODR_104Hz);
    IMU.setGyroFullScale(ISM_250dps);
    IMU.setAccelFilterLP2();
    IMU.setAccelSlopeFilter(ISM_LP_ODR_DIV_100);
    IMU.setGyroFilterLP1();
    IMU.setGyroLP1Bandwidth(ISM_MEDIUM);
}

void process_imu_data(sfe_ism_data_t *sdata, norm_imu_data *mydata, float div_norm, bool gravity_comp) {
    Serial.println(sdata->xData / div_norm);
    mydata->xData = sdata->xData / div_norm;
    mydata->yData = sdata->yData / div_norm;
    mydata->zData = sdata->zData / div_norm;
    mydata->length = sqrt(mydata->xData * mydata->xData + mydata->yData * mydata->yData + mydata->zData * mydata->zData) - gravity_comp * .5;
    Serial.println(mydata->length);
}

void normalize_in_place(sfe_ism_data_t *adata, float div_norm) {
    adata->xData = adata->xData / div_norm;
    adata->yData = adata->yData / div_norm;
    adata->zData = adata->zData / div_norm;
}
