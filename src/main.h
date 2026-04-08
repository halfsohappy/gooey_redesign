// =============================================================================
// main.h — Top-level include orchestrator for the TheaterGWD firmware
// =============================================================================
//
// This header is included only from main.cpp and pulls in every module in
// the correct dependency order.
//
// Build with -DAB7_BUILD to target the ab7 board (BNO085 IMU via SlimeIMU,
// SK6812 LED, two buttons).  Without this flag the build targets the Bart
// board (ISM330DHCX + MMC5983MA + BMP5xx via SlimeIMU).  Both boards support oris.
// =============================================================================

#ifndef _MAIN_H_
#define _MAIN_H_

// --- Hardware and sensor layer ----------------------------------------------
#ifdef AB7_BUILD
#include "ab7_hardware.h"
#else
#include "bart_hardware.h"
#endif

// --- Alternative Euler decompositions (ZXY for gimbal-lock avoidance) -------
#include "euler_utils.h"

// --- Sensor data stream definitions -----------------------------------------
#include "data_streams.h"

// --- Orientation tracker -------------------------------------------------------
#include "ori_tracker.h"

// --- OSC object model -------------------------------------------------------
#include "osc_message.h"   // OscMessage class
#include "osc_scene.h"     // OscScene class
#include "osc_registry.h"  // OscRegistry singleton + method implementations

// --- Device address (set during provisioning, used in command dispatch) ------
String device_adr;

// --- Status reporting -------------------------------------------------------
#include "osc_status.h"

// --- Sending engine (FreeRTOS tasks, UDP, MicroOsc) -------------------------
#include "osc_engine.h"

// --- Non-volatile storage for scenes and messages --------------------------
#include "osc_storage.h"

// --- Network provisioning (captive portal) ----------------------------------
#include "network_setup.h"

// --- Incoming OSC command handler -------------------------------------------
#include "osc_commands.h"

// --- Serial debug command interface -----------------------------------------
#include "serial_commands.h"

#endif // _MAIN_H_
