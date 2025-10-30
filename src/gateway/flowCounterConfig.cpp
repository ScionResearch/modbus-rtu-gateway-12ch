#include "flowCounterConfig.h"
#include "flowCounterManager.h"
#include "../network/network.h"

// Global variables
GatewayConfig gatewayConfig;
FlowCounterData flowCounterData[MAX_FLOW_COUNTERS];
volatile bool flowCounterDataLocked = false;

void init_gatewayConfig() {
    // Initialize flow counter data FIRST
    for (int i = 0; i < MAX_FLOW_COUNTERS; i++) {
        flowCounterData[i].dataValid = false;
        flowCounterData[i].commError = false;
        flowCounterData[i].lastUpdate = 0;
        flowCounterData[i].triggerCount = 0;
        flowCounterData[i].pendingInitialRead = false;
        flowCounterData[i].modbusRequestPending = false;
        flowCounterData[i].currentTemperature = 0.0f;
        flowCounterData[i].currentPressure = 0.0f;
        memset(flowCounterData[i].unit_ID, 0, sizeof(flowCounterData[i].unit_ID));
    }
    
    // Load configuration from LittleFS to get correct pin assignments
    if (!loadGatewayConfig()) {
        log(LOG_WARNING, false, "Failed to load gateway config, using defaults\n");
        setDefaultGatewayConfig();
        saveGatewayConfig();
    }
    
    // NOW configure trigger pins with the correct pin numbers from config
    for (int i = 0; i < MAX_FLOW_COUNTERS; i++) {
        pinMode(gatewayConfig.ports[i].triggerPin, INPUT_PULLUP);
        delay(1);  // Allow pull-up to settle
        int pinState = digitalRead(gatewayConfig.ports[i].triggerPin);
        log(LOG_INFO, false, "Port %d: Trigger pin %d set to INPUT_PULLUP, reads as %d (%s)\n",
            i + 1, gatewayConfig.ports[i].triggerPin, pinState, pinState == HIGH ? "HIGH/idle" : "LOW/triggered");
    }
    
    log(LOG_INFO, false, "Gateway configuration initialized\n");
}

void setDefaultGatewayConfig() {
    // RS485 defaults
    gatewayConfig.rs485.baudRate = DEFAULT_MODBUS_BAUD;
    gatewayConfig.rs485.serialConfig = DEFAULT_MODBUS_CONFIG;
    gatewayConfig.rs485.responseTimeout = 200;
    
    // Port defaults - map trigger pins to ports
    const uint8_t triggerPins[MAX_FLOW_COUNTERS] = {
        PIN_TRIG_1, PIN_TRIG_2, PIN_TRIG_3, PIN_TRIG_4,
        PIN_TRIG_5, PIN_TRIG_6, PIN_TRIG_7, PIN_TRIG_8,
        PIN_TRIG_9, PIN_TRIG_10, PIN_TRIG_11, PIN_TRIG_12
    };
    
    for (int i = 0; i < MAX_FLOW_COUNTERS; i++) {
        gatewayConfig.ports[i].enabled = false;
        gatewayConfig.ports[i].slaveId = i + 1;  // Default slave IDs 1-12
        snprintf(gatewayConfig.ports[i].portName, sizeof(gatewayConfig.ports[i].portName), 
                 "Port %d", i + 1);
        gatewayConfig.ports[i].logToSD = false;
        gatewayConfig.ports[i].triggerPin = triggerPins[i];
    }
}

bool loadGatewayConfig() {
    log(LOG_INFO, true, "Loading gateway configuration\n");
    
    if (!LittleFS.begin()) {
        log(LOG_WARNING, true, "Failed to mount LittleFS\n");
        return false;
    }
    
    if (!LittleFS.exists(GATEWAY_CONFIG_FILENAME)) {
        log(LOG_WARNING, true, "Gateway config file not found\n");
        LittleFS.end();
        return false;
    }
    
    File configFile = LittleFS.open(GATEWAY_CONFIG_FILENAME, "r");
    if (!configFile) {
        log(LOG_WARNING, true, "Failed to open gateway config file\n");
        LittleFS.end();
        return false;
    }
    
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();
    LittleFS.end();
    
    if (error) {
        log(LOG_WARNING, true, "Failed to parse gateway config: %s\n", error.c_str());
        return false;
    }
    
    // Check magic number
    uint8_t magicNumber = doc["magic_number"] | 0;
    if (magicNumber != GATEWAY_CONFIG_MAGIC_NUMBER) {
        log(LOG_WARNING, true, "Invalid magic number in gateway config\n");
        return false;
    }
    
    // Parse RS485 configuration
    gatewayConfig.rs485.baudRate = doc["rs485"]["baud_rate"] | DEFAULT_MODBUS_BAUD;
    gatewayConfig.rs485.serialConfig = doc["rs485"]["serial_config"] | DEFAULT_MODBUS_CONFIG;
    gatewayConfig.rs485.responseTimeout = doc["rs485"]["response_timeout"] | 200;
    
    // Validate serial config - must not be 0
    if (gatewayConfig.rs485.serialConfig == 0) {
        log(LOG_WARNING, false, "Invalid serial config (0x0), using default SERIAL_8N1\n");
        gatewayConfig.rs485.serialConfig = SERIAL_8N1;
    }
    
    // Parse port configurations
    JsonArray portsArray = doc["ports"];
    if (portsArray) {
        int idx = 0;
        for (JsonObject portObj : portsArray) {
            if (idx >= MAX_FLOW_COUNTERS) break;
            
            gatewayConfig.ports[idx].enabled = portObj["enabled"] | false;
            gatewayConfig.ports[idx].slaveId = portObj["slave_id"] | (idx + 1);
            strlcpy(gatewayConfig.ports[idx].portName, 
                   portObj["name"] | "", 
                   sizeof(gatewayConfig.ports[idx].portName));
            gatewayConfig.ports[idx].logToSD = portObj["log_to_sd"] | false;
            gatewayConfig.ports[idx].triggerPin = portObj["trigger_pin"] | PIN_TRIG_1;
            
            idx++;
        }
    }
    
    log(LOG_INFO, true, "Gateway configuration loaded successfully\n");
    return true;
}

void saveGatewayConfig() {
    log(LOG_INFO, true, "Saving gateway configuration\n");
    
    if (!LittleFS.begin()) {
        log(LOG_WARNING, true, "Failed to mount LittleFS\n");
        return;
    }
    
    StaticJsonDocument<2048> doc;
    
    // Store magic number
    doc["magic_number"] = GATEWAY_CONFIG_MAGIC_NUMBER;
    
    // Store RS485 configuration
    JsonObject rs485 = doc.createNestedObject("rs485");
    rs485["baud_rate"] = gatewayConfig.rs485.baudRate;
    rs485["serial_config"] = (uint32_t)gatewayConfig.rs485.serialConfig;
    rs485["response_timeout"] = gatewayConfig.rs485.responseTimeout;
    
    // Store port configurations
    JsonArray portsArray = doc.createNestedArray("ports");
    for (int i = 0; i < MAX_FLOW_COUNTERS; i++) {
        JsonObject portObj = portsArray.createNestedObject();
        portObj["enabled"] = gatewayConfig.ports[i].enabled;
        portObj["slave_id"] = gatewayConfig.ports[i].slaveId;
        portObj["name"] = gatewayConfig.ports[i].portName;
        portObj["log_to_sd"] = gatewayConfig.ports[i].logToSD;
        portObj["trigger_pin"] = gatewayConfig.ports[i].triggerPin;
    }
    
    File configFile = LittleFS.open(GATEWAY_CONFIG_FILENAME, "w");
    if (!configFile) {
        log(LOG_WARNING, true, "Failed to open gateway config file for writing\n");
        LittleFS.end();
        return;
    }
    
    size_t bytesWritten = serializeJson(doc, configFile);
    if (bytesWritten == 0) {
        log(LOG_WARNING, true, "Failed to serialize gateway config\n");
        configFile.close();
        LittleFS.end();
        return;
    }
    
    configFile.close();
    LittleFS.end();
    log(LOG_INFO, true, "Gateway configuration saved\n");
}

void setupGatewayConfigAPI() {
    // Get gateway configuration
    server.on("/api/gateway/config", HTTP_GET, []() {
        StaticJsonDocument<2048> doc;
        
        // RS485 configuration
        JsonObject rs485 = doc.createNestedObject("rs485");
        rs485["baud_rate"] = gatewayConfig.rs485.baudRate;
        rs485["serial_config"] = gatewayConfig.rs485.serialConfig;
        rs485["response_timeout"] = gatewayConfig.rs485.responseTimeout;
        
        // Port configurations
        JsonArray portsArray = doc.createNestedArray("ports");
        for (int i = 0; i < MAX_FLOW_COUNTERS; i++) {
            JsonObject portObj = portsArray.createNestedObject();
            portObj["port"] = i + 1;
            portObj["enabled"] = gatewayConfig.ports[i].enabled;
            portObj["slave_id"] = gatewayConfig.ports[i].slaveId;
            portObj["name"] = gatewayConfig.ports[i].portName;
            portObj["log_to_sd"] = gatewayConfig.ports[i].logToSD;
        }
        
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });
    
    // Update gateway configuration
    server.on("/api/gateway/config", HTTP_POST, []() {
        if (!server.hasArg("plain")) {
            server.send(400, "application/json", "{\"error\":\"No data received\"}");
            return;
        }
        
        StaticJsonDocument<2048> doc;
        DeserializationError error = deserializeJson(doc, server.arg("plain"));
        
        if (error) {
            server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        
        bool rs485Changed = false;
        bool timeoutChanged = false;
        
        // Update RS485 configuration if provided
        if (doc.containsKey("rs485")) {
            JsonObject rs485 = doc["rs485"];
            if (rs485.containsKey("baud_rate")) {
                uint32_t newBaud = rs485["baud_rate"];
                if (newBaud != gatewayConfig.rs485.baudRate) {
                    gatewayConfig.rs485.baudRate = newBaud;
                    rs485Changed = true;
                }
            }
            if (rs485.containsKey("serial_config")) {
                uint32_t newConfig = rs485["serial_config"];
                if (newConfig != gatewayConfig.rs485.serialConfig) {
                    gatewayConfig.rs485.serialConfig = newConfig;
                    rs485Changed = true;
                }
            }
            if (rs485.containsKey("response_timeout")) {
                uint16_t newTimeout = rs485["response_timeout"];
                if (newTimeout != gatewayConfig.rs485.responseTimeout) {
                    gatewayConfig.rs485.responseTimeout = newTimeout;
                    timeoutChanged = true;  // Timeout doesn't need restart, just update
                }
            }
        }
        
        // Update port configurations if provided
        if (doc.containsKey("ports")) {
            JsonArray portsArray = doc["ports"];
            for (JsonObject portObj : portsArray) {
                int portIdx = portObj["port"] | 0;
                if (portIdx >= 1 && portIdx <= MAX_FLOW_COUNTERS) {
                    int idx = portIdx - 1;
                    if (portObj.containsKey("enabled")) {
                        gatewayConfig.ports[idx].enabled = portObj["enabled"];
                    }
                    if (portObj.containsKey("slave_id")) {
                        gatewayConfig.ports[idx].slaveId = portObj["slave_id"];
                    }
                    if (portObj.containsKey("name")) {
                        strlcpy(gatewayConfig.ports[idx].portName, 
                               portObj["name"] | "", 
                               sizeof(gatewayConfig.ports[idx].portName));
                    }
                    if (portObj.containsKey("log_to_sd")) {
                        gatewayConfig.ports[idx].logToSD = portObj["log_to_sd"];
                    }
                }
            }
        }
        
        // Save configuration
        saveGatewayConfig();
        
        // Apply RS485 changes immediately - reinitialize Modbus RTU if needed
        if (rs485Changed || timeoutChanged) {
            reinit_modbusRTU();
            server.send(200, "application/json", 
                       "{\"status\":\"success\",\"message\":\"Configuration saved. RS485 interface reinitialized.\"}");
        } else {
            server.send(200, "application/json", 
                       "{\"status\":\"success\",\"message\":\"Configuration saved.\"}");
        }
        
        // Mark enabled ports as needing initial read - will be polled from main loop
        // Reset disabled ports to clean state
        if (!flowCounterDataLocked) {
            flowCounterDataLocked = true;
            for (int i = 0; i < MAX_FLOW_COUNTERS; i++) {
                if (gatewayConfig.ports[i].enabled) {
                    flowCounterData[i].pendingInitialRead = true;
                } else {
                    // Port is disabled - reset to clean state
                    flowCounterData[i].dataValid = false;
                    flowCounterData[i].commError = false;
                    flowCounterData[i].modbusRequestPending = false;
                    flowCounterData[i].pendingInitialRead = false;
                    flowCounterData[i].triggerCount = 0;
                }
            }
            flowCounterDataLocked = false;
        }
    });
    
    // Get flow counter data
    server.on("/api/gateway/data", HTTP_GET, []() {
        if (flowCounterDataLocked) {
            server.send(423, "application/json", "{\"error\":\"Data locked\"}");
            return;
        }
        
        flowCounterDataLocked = true;
        
        StaticJsonDocument<4096> doc;
        
        // Add system timing info for client-side calculations
        doc["current_millis"] = millis();
        doc["millis_rollover_count"] = millisRolloverCount;
        
        JsonArray dataArray = doc.createNestedArray("flow_counters");
        
        for (int i = 0; i < MAX_FLOW_COUNTERS; i++) {
            JsonObject fcObj = dataArray.createNestedObject();
            fcObj["port"] = i + 1;
            fcObj["enabled"] = gatewayConfig.ports[i].enabled;
            fcObj["slave_id"] = gatewayConfig.ports[i].slaveId;
            fcObj["name"] = gatewayConfig.ports[i].portName;
            fcObj["data_valid"] = flowCounterData[i].dataValid;
            fcObj["comm_error"] = flowCounterData[i].commError;
            fcObj["trigger_count"] = flowCounterData[i].triggerCount;
            
            if (flowCounterData[i].dataValid) {
                JsonObject data = fcObj.createNestedObject("data");
                data["volume"] = flowCounterData[i].volume;
                data["volume_normalised"] = flowCounterData[i].volume_normalised;
                data["flow"] = flowCounterData[i].flow;
                data["flow_normalised"] = flowCounterData[i].flow_normalised;
                data["temperature"] = flowCounterData[i].temperature;  // Snapshot temp (regs 8-9)
                data["pressure"] = flowCounterData[i].pressure;  // Snapshot pressure (regs 10-11)
                data["current_temperature"] = flowCounterData[i].currentTemperature;  // Live temp (regs 30-31)
                data["current_pressure"] = flowCounterData[i].currentPressure;  // Live pressure (regs 32-33)
                data["timestamp"] = flowCounterData[i].timestamp;
                data["psu_volts"] = flowCounterData[i].psu_volts;
                data["batt_volts"] = flowCounterData[i].batt_volts;
                data["unit_id"] = flowCounterData[i].unit_ID;
                data["last_update"] = flowCounterData[i].lastUpdate;
            }
        }
        
        flowCounterDataLocked = false;
        
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });
    
    // Manual read trigger for a specific port
    server.on("/api/gateway/manual-read", HTTP_POST, []() {
        if (!server.hasArg("port")) {
            server.send(400, "application/json", "{\"error\":\"Missing port parameter\"}");
            return;
        }
        
        int portNum = server.arg("port").toInt();
        
        if (portNum < 1 || portNum > MAX_FLOW_COUNTERS) {
            server.send(400, "application/json", "{\"error\":\"Invalid port number\"}");
            return;
        }
        
        int portIndex = portNum - 1;
        
        if (!gatewayConfig.ports[portIndex].enabled) {
            server.send(400, "application/json", "{\"error\":\"Port not enabled\"}");
            return;
        }
        
        // Trigger a manual read
        readFlowCounter(portIndex);
        
        server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Manual read triggered\"}");
    });
}
