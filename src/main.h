// =============================================================================
// main.h — Top-level include orchestrator for the TheaterGWD firmware
// =============================================================================
//
// This header is included only from main.cpp and pulls in every module in
// the correct dependency order.
//
// Build with -DAB7_BUILD to target the ab7 board (BNO085 IMU via SlimeIMU,
// SK6812 LED, two buttons, orientation tracker).  Without this flag the
// build targets the Bart board (BMP5xx, LSM6DSV16XTR via SlimeIMU).
// =============================================================================

#ifndef _MAIN_H_
#define _MAIN_H_

// --- Hardware and sensor layer ----------------------------------------------
#ifdef AB7_BUILD
#include "ab7_hardware.h"
#else
#include "bart_hardware.h"
#endif

// --- Sensor data stream definitions -----------------------------------------
#include "data_streams.h"

// --- Orientation tracker (ab7 only) -----------------------------------------
#ifdef AB7_BUILD
#include "ori_tracker.h"
#endif

// --- OSC object model -------------------------------------------------------
#include "osc_message.h"   // OscMessage class
#include "osc_patch.h"     // OscPatch class
#include "osc_registry.h"  // OscRegistry singleton + method implementations

// --- Device address (set during provisioning, used in command dispatch) ------
String device_adr;

// --- Status reporting -------------------------------------------------------
#include "osc_status.h"

// --- Sending engine (FreeRTOS tasks, UDP, MicroOsc) -------------------------
#include "osc_engine.h"

// --- Non-volatile storage for patches and messages --------------------------
#include "osc_storage.h"

// --- Network provisioning (captive portal) ----------------------------------
#include "network_setup.h"

// --- Incoming OSC command handler -------------------------------------------
#include "osc_commands.h"

// --- Serial debug command interface -----------------------------------------
#include "serial_commands.h"

#endif // _MAIN_H_
