#ifndef _MAIN_H_
#define _MAIN_H_

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
float data_streams[NUM_DATA_STREAMS];

#include "bart_hardware.h"
#include "OscClass.h"

String device_adr;

#include "network_setup.h"
#include "OSC.h"


#endif  // _MAIN_H_
