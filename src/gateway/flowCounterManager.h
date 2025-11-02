#pragma once

#include "flowCounterConfig.h"

// Flow counter register definitions
#define FC_START_ADDRESS 0
#define FC_REGISTER_COUNT 23  // Total registers to read
#define FC_TEMP_PRESSURE_ADDRESS 8  // Temperature starts at register 8
#define FC_TEMP_PRESSURE_COUNT 4    // Temperature (2 regs) + Pressure (2 regs)

// Function prototypes
void init_flowCounterManager();
void reinit_modbusRTU();  // Reinitialize Modbus RTU with new settings
void manage_flowCounterManager();
void checkTriggers();
void readFlowCounter(uint8_t portIndex, bool fromTrigger = false);
void readFlowCounterTempPressure(uint8_t portIndex);  // Read only temp/pressure for periodic updates
void pollAllConfiguredDevices();        // Poll all enabled ports on startup
void checkOfflineDevices();             // Periodically poll offline devices
void periodicPollConfiguredDevices();   // Periodic poll of all configured devices (~1 minute)
void modbusResponseCallback(bool valid, uint16_t* data, uint32_t requestId);
void modbusTempPressureCallback(bool valid, uint16_t* data, uint32_t requestId);

// Global variables
extern ModbusRTUMaster modbusRTU;
extern volatile bool triggerFlags[MAX_FLOW_COUNTERS];
extern volatile bool triggerStates[MAX_FLOW_COUNTERS];  // Track current state for edge detection
