#include <Adafruit_Sensor.h>
#include "Adafruit_BMP5xx.h"


Adafruit_BMP5xx bmp; // Create BMP5xx object
#define SEALEVELPRESSURE_HPA (1013.25)
bmp5xx_powermode_t desiredMode = BMP5XX_POWERMODE_CONTINUOUS; // Cache desired power mode


void begin_baro(uint16_t BCS){
  bmp.begin(BCS, &SPI);
  bmp.setTemperatureOversampling(BMP5XX_OVERSAMPLING_1X);
  bmp.setPressureOversampling(BMP5XX_OVERSAMPLING_32X);
  bmp.setIIRFilterCoeff(BMP5XX_IIR_FILTER_COEFF_15);
  bmp.setOutputDataRate(BMP5XX_ODR_45_HZ);
  bmp.setPowerMode(BMP5XX_POWERMODE_CONTINUOUS);
  bmp.enablePressure(true);
  bmp.configureInterrupt(BMP5XX_INTERRUPT_LATCHED, BMP5XX_INTERRUPT_ACTIVE_HIGH, BMP5XX_INTERRUPT_PUSH_PULL, BMP5XX_INTERRUPT_DATA_READY, true);
  delay(5);
}