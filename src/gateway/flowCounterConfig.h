#pragma once

// Maximum number of flow counter ports (must be defined before sys_init.h)
#define MAX_FLOW_COUNTERS 12

#include "../sys_init.h"

// LittleFS configuration file
#define GATEWAY_CONFIG_FILENAME "/gateway_config.json"
#define GATEWAY_CONFIG_MAGIC_NUMBER 0xFC

// Modbus RTU configuration defaults
#define DEFAULT_MODBUS_BAUD 9600
#define DEFAULT_MODBUS_CONFIG SERIAL_8N1  // SERIAL_8N1=1043, 8N2=1075, 8E1=1041, 8E2=1073, 8O1=1042, 8O2=1074

// Flow counter data structure (matches Modbus register layout)
struct FlowCounterData {
    // Registers 0-22: Snapshot values (only updated on trigger events)
    float volume;
    float volume_normalised;
    float flow;
    float flow_normalised;
    float temperature;           // Snapshot temp (registers 8-9)
    float pressure;              // Snapshot pressure (registers 10-11)
    uint32_t timestamp;
    float psu_volts;
    float batt_volts;
    char unit_ID[11];  // 10 chars + null terminator
    
    // Registers 30-33: Live values (updated by periodic polling)
    float currentTemperature;    // Live temp for registers 30-31
    float currentPressure;       // Live pressure for registers 32-33
    
    // Metadata
    uint32_t lastUpdate;        // millis() when last updated
    bool dataValid;              // True if we have valid data
    bool commError;              // True if last communication failed
    uint32_t triggerCount;       // Count of triggers received
    bool pendingInitialRead;     // True if device needs initial poll after config
    bool modbusRequestPending;   // True if a Modbus request is currently pending
};

// Per-port configuration
struct FlowCounterPortConfig {
    bool enabled;                // Port is enabled
    uint8_t slaveId;            // Modbus slave ID (1-247)
    char portName[16];          // User-friendly name for this port
    bool logToSD;               // Enable SD card logging for this port
    uint8_t triggerPin;         // GPIO pin for trigger input
};

// Gateway RS485 configuration
struct GatewayRS485Config {
    uint32_t baudRate;          // Baud rate
    uint32_t serialConfig;      // Serial config (SERIAL_8N1, SERIAL_8E1, etc.)
    uint16_t responseTimeout;   // Response timeout in ms
};

// Gateway configuration structure
struct GatewayConfig {
    GatewayRS485Config rs485;
    FlowCounterPortConfig ports[MAX_FLOW_COUNTERS];
};

// Function prototypes
void init_gatewayConfig();
bool loadGatewayConfig();
void saveGatewayConfig();
void setDefaultGatewayConfig();
void setupGatewayConfigAPI();

// Global variables
extern GatewayConfig gatewayConfig;
extern FlowCounterData flowCounterData[MAX_FLOW_COUNTERS];
extern volatile bool flowCounterDataLocked;
