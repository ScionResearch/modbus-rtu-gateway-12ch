#pragma once

// Version information - use PlatformIO build flags if available, otherwise fallback
#ifndef VERSION_MAJOR
    #define VERSION_MAJOR 1
#endif

#ifndef VERSION_MINOR
    #define VERSION_MINOR 1
#endif

#ifndef VERSION_PATCH
    #define VERSION_PATCH 2
#endif

// Helper macros for proper string handling
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// Create version strings using macro stringification - this avoids the PROJECT_VERSION issue
#define VERSION TOSTRING(VERSION_MAJOR) "." TOSTRING(VERSION_MINOR) "." TOSTRING(VERSION_PATCH)
#define VERSION_STRING "Modbus RTU-TCP Gateway V" VERSION

// Include libraries
#include <Arduino.h>
#include <W5500lwIP.h>
#include <WebServer.h>
#include <NTPClient.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include "Adafruit_Neopixel.h"
#include "modbus-rtu-master.h"

// Include program files
#include "hardware/pins.h"

#include "network/network.h"

#include "utils/logger.h"
#include "utils/statusManager.h"
#include "utils/terminalManager.h"

#include "storage/sdManager.h"

#include "gateway/flowCounterConfig.h"
#include "gateway/flowCounterManager.h"

void init_core0(void);
void init_core1(void);
void manage_core0(void);
void manage_core1(void);

// Task handler prototypes
void handleSDManager(void);
void handleLEDManager(void);
void handleTerminalManager(void);
void handleIpcManager(void);
void handleNetworkManager(void);

// Object definitions

extern bool core0setupComplete;
extern bool core1setupComplete;

extern bool debug;

// Millis rollover tracking
extern volatile uint32_t millisRolloverCount;