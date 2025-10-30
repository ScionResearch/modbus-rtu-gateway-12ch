#include "flowCounterManager.h"
#include "../storage/sdManager.h"
#include "../utils/statusManager.h"

// Global variables
ModbusRTUMaster modbusRTU;
volatile bool triggerFlags[MAX_FLOW_COUNTERS] = {false};
volatile bool triggerStates[MAX_FLOW_COUNTERS] = {false};  // Track previous state
static uint32_t lastTriggerCheck = 0;
// lastOfflineCheck removed - checkOfflineDevices() no longer used
static uint32_t lastPeriodicPoll = 0;
static uint16_t modbusBuffer[FC_REGISTER_COUNT];
static uint16_t modbusTempPressureBuffer[FC_TEMP_PRESSURE_COUNT];

#define TRIGGER_CHECK_INTERVAL 10        // Check triggers every 10ms
#define PERIODIC_POLL_INTERVAL 10000     // Poll all configured devices every 10 seconds (for testing)

void init_flowCounterManager() {
    // Initialize ModbusRTU on Serial1 (UART0)
    Serial1.setRX(PIN_RS485_RX);
    Serial1.setTX(PIN_RS485_TX);
    
    // Set DE pin
    pinMode(PIN_RS485_DE, OUTPUT);
    digitalWrite(PIN_RS485_DE, LOW);
    
    // Set SLR pin high (as requested)
    pinMode(PIN_RS485_TERM, OUTPUT);
    digitalWrite(PIN_RS485_TERM, HIGH);
    
    // Initialize Modbus RTU Master
    if (!modbusRTU.begin(&Serial1, gatewayConfig.rs485.baudRate, 
                         gatewayConfig.rs485.serialConfig, PIN_RS485_DE)) {
        log(LOG_ERROR, false, "Failed to initialize Modbus RTU Master\n");
        return;
    }
    
    modbusRTU.setTimeout(gatewayConfig.rs485.responseTimeout);
    
    // Parse serial config for readable logging
    // Arduino constants: parity in bits 0-3, stop bits in bits 4-7, data bits in bits 8-11
    const char* parity = "N";
    const char* stopBits = "1";
    uint32_t cfg = gatewayConfig.rs485.serialConfig;
    uint8_t parityBits = cfg & 0xF;
    uint8_t stopBitField = (cfg >> 4) & 0xF;
    
    if (parityBits == 0x1) parity = "E";
    else if (parityBits == 0x2) parity = "O";
    else if (parityBits == 0x3) parity = "N";
    
    if (stopBitField == 0x3) stopBits = "2";
    else if (stopBitField == 0x1) stopBits = "1";
    
    log(LOG_INFO, false, "Flow Counter Manager initialized (Baud: %d, Format: 8%s%s, Timeout: %d ms, DE pin: %d)\n", 
        gatewayConfig.rs485.baudRate, parity, stopBits,
        gatewayConfig.rs485.responseTimeout, PIN_RS485_DE);
    
    // Initialize trigger states by reading current GPIO levels
    // This prevents false edge detection during startup
    for (int i = 0; i < MAX_FLOW_COUNTERS; i++) {
        if (gatewayConfig.ports[i].enabled) {
            triggerStates[i] = (digitalRead(gatewayConfig.ports[i].triggerPin) == LOW);
            log(LOG_INFO, false, "Port %d: Trigger pin %d initialized to state %d\n", 
                i + 1, gatewayConfig.ports[i].triggerPin, triggerStates[i]);
        }
    }
    
    // Poll all configured devices on startup (after a longer delay to allow hardware to stabilize)
    // Flow counter devices need time to initialize their Modbus interface after power-up
    log(LOG_INFO, false, "Waiting for flow counters to initialize...\n");
    delay(1000);  // Increased from 100ms to 1000ms
    pollAllConfiguredDevices();
}

// Reinitialize Modbus RTU with new configuration (e.g., after settings change)
void reinit_modbusRTU() {
    log(LOG_INFO, false, "Reinitializing Modbus RTU with new configuration...\n");
    
    // Reinitialize Modbus RTU Master with new settings
    if (!modbusRTU.begin(&Serial1, gatewayConfig.rs485.baudRate, 
                         gatewayConfig.rs485.serialConfig, PIN_RS485_DE)) {
        log(LOG_ERROR, false, "Failed to reinitialize Modbus RTU Master\n");
        return;
    }
    
    modbusRTU.setTimeout(gatewayConfig.rs485.responseTimeout);
    
    // Parse serial config for readable logging
    // Arduino constants: parity in bits 0-3, stop bits in bits 4-7, data bits in bits 8-11
    const char* parity = "N";
    const char* stopBits = "1";
    uint32_t cfg = gatewayConfig.rs485.serialConfig;
    uint8_t parityBits = cfg & 0xF;
    uint8_t stopBitField = (cfg >> 4) & 0xF;
    
    if (parityBits == 0x1) parity = "E";
    else if (parityBits == 0x2) parity = "O";
    else if (parityBits == 0x3) parity = "N";
    
    if (stopBitField == 0x3) stopBits = "2";
    else if (stopBitField == 0x1) stopBits = "1";
    
    log(LOG_INFO, false, "Modbus RTU reinitialized (Baud: %d, Format: 8%s%s, Timeout: %d ms)\n", 
        gatewayConfig.rs485.baudRate, parity, stopBits, 
        gatewayConfig.rs485.responseTimeout);
}

void manage_flowCounterManager() {
    // Always call modbusRTU.manage() to process queue
    modbusRTU.manage();
    
    // Check for triggers periodically
    if (millis() - lastTriggerCheck >= TRIGGER_CHECK_INTERVAL) {
        lastTriggerCheck = millis();
        checkTriggers();
    }
    
    // NOTE: checkOfflineDevices() is now redundant - periodicPollConfiguredDevices() handles:
    //   - Never-connected devices (full reads to get initial data)
    //   - Connected devices (temp/pressure reads)
    //   - Error recovery (temp/pressure reads check if offline devices have recovered)
    // Keeping the function for potential future use but not calling it
    
    // Periodic poll of all configured devices (every 10 seconds during testing, normally 1 minute)
    if (millis() - lastPeriodicPoll >= PERIODIC_POLL_INTERVAL) {
        lastPeriodicPoll = millis();
        periodicPollConfiguredDevices();
    }
    
    // Process any triggered ports
    for (int i = 0; i < MAX_FLOW_COUNTERS; i++) {
        if (triggerFlags[i] && gatewayConfig.ports[i].enabled) {
            // Clear flag immediately after queuing to prevent multiple reads
            log(LOG_DEBUG, false, "Processing trigger for port %d (triggerState:%d)\n", 
                i + 1, triggerStates[i]);
            triggerFlags[i] = false;
            readFlowCounter(i);
            break;  // Process one at a time to avoid queue overflow
        }
    }
    
    // Check for devices needing initial read after configuration
    static uint32_t lastPendingCheck = 0;
    if (millis() - lastPendingCheck >= 2000) {  // Check every 2 seconds
        lastPendingCheck = millis();
        
        for (int i = 0; i < MAX_FLOW_COUNTERS; i++) {
            if (flowCounterData[i].pendingInitialRead && gatewayConfig.ports[i].enabled) {
                log(LOG_INFO, false, "Processing pending initial read for port %d\n", i + 1);
                flowCounterData[i].pendingInitialRead = false;
                readFlowCounter(i);
                break;  // Process one at a time
            }
        }
    }
}

void checkTriggers() {
    for (int i = 0; i < MAX_FLOW_COUNTERS; i++) {
        if (!gatewayConfig.ports[i].enabled) continue;
        
        // Read current trigger state (active LOW)
        int pinValue = digitalRead(gatewayConfig.ports[i].triggerPin);
        bool currentState = (pinValue == LOW);
        
        // Debug: Log any state changes
        if (currentState != triggerStates[i]) {
            log(LOG_INFO, false, "Port %d: GPIO pin %d changed from %d to %d (state:%d->%d)\n",
                i + 1, gatewayConfig.ports[i].triggerPin, 
                triggerStates[i] ? LOW : HIGH, pinValue,
                triggerStates[i], currentState);
        }
        
        // Detect falling edge (HIGH -> LOW transition)
        if (currentState && !triggerStates[i]) {
            // Falling edge detected - set trigger flag to queue a read
            triggerFlags[i] = true;
            log(LOG_INFO, false, "Trigger FALLING edge on port %d (was:%d now:%d)\n", 
                i + 1, triggerStates[i], currentState);
        }
        
        // Detect rising edge (LOW -> HIGH transition)
        if (!currentState && triggerStates[i]) {
            // Rising edge - trigger released
            log(LOG_INFO, false, "Trigger RISING edge on port %d (was:%d now:%d)\n", 
                i + 1, triggerStates[i], currentState);
        }
        
        // Update state
        triggerStates[i] = currentState;
    }
}

void readFlowCounter(uint8_t portIndex) {
    if (portIndex >= MAX_FLOW_COUNTERS) return;
    
    uint8_t slaveId = gatewayConfig.ports[portIndex].slaveId;
    
    // Queue the read request
    if (!modbusRTU.readHoldingRegisters(slaveId, FC_START_ADDRESS, modbusBuffer, 
                                        FC_REGISTER_COUNT, modbusResponseCallback, 
                                        portIndex)) {
        log(LOG_WARNING, false, "Failed to queue read request for port %d\n", portIndex + 1);
        
        // Mark as comm error only if device was previously connected
        if (!flowCounterDataLocked) {
            flowCounterDataLocked = true;
            if (flowCounterData[portIndex].dataValid) {
                flowCounterData[portIndex].commError = true;
                leds.setPixelColor(portIndex + 2, LED_COLOR_RED);  // Red for error
            } else {
                leds.setPixelColor(portIndex + 2, LED_COLOR_PURPLE);  // Purple for not yet connected
            }
            flowCounterData[portIndex].modbusRequestPending = false;
            flowCounterDataLocked = false;
            leds.show();
        }
    } else {
        // Set LED state immediately to show cyan
        leds.setPixelColor(portIndex + 2, LED_COLOR_CYAN);  // Cyan
        leds.setPixelColor(1, LED_COLOR_CYAN);  // Com LED
        leds.show();
        // Request successfully queued - set pending flag
        if (!flowCounterDataLocked) {
            flowCounterDataLocked = true;
            flowCounterData[portIndex].modbusRequestPending = true;
            flowCounterDataLocked = false;
        }
    }
}

// Read only temperature and pressure registers for periodic polling
// This preserves volume/flow values that should only update on triggers
void readFlowCounterTempPressure(uint8_t portIndex) {
    if (portIndex >= MAX_FLOW_COUNTERS) return;
    
    uint8_t slaveId = gatewayConfig.ports[portIndex].slaveId;
    
    log(LOG_DEBUG, false, "Reading temp/pressure on port %d (Slave ID: %d)\n", 
        portIndex + 1, slaveId);
    
    // Queue the read request for registers 8-11 (temperature and pressure only)
    if (!modbusRTU.readHoldingRegisters(slaveId, FC_TEMP_PRESSURE_ADDRESS, modbusTempPressureBuffer, 
                                        FC_TEMP_PRESSURE_COUNT, modbusTempPressureCallback, 
                                        portIndex)) {
        log(LOG_WARNING, false, "Failed to queue temp/pressure read request for port %d\n", portIndex + 1);
        
        // Mark as comm error
        if (!flowCounterDataLocked) {
            flowCounterDataLocked = true;
            flowCounterData[portIndex].commError = true;
            flowCounterData[portIndex].modbusRequestPending = false;
            flowCounterDataLocked = false;
            
            // Set LED state immediately to show red
            leds.setPixelColor(portIndex + 2, LED_COLOR_RED);  // Red
            leds.show();
        }
    } else {
        // Request successfully queued - set pending flag
        if (!flowCounterDataLocked) {
            flowCounterDataLocked = true;
            flowCounterData[portIndex].modbusRequestPending = true;
            flowCounterDataLocked = false;
            
            // Set LED state immediately to show cyan
            leds.setPixelColor(portIndex + 2, LED_COLOR_CYAN);  // Cyan
            leds.setPixelColor(1, LED_COLOR_CYAN);  // Com LED
            leds.show();
        }
    }
}

void modbusResponseCallback(bool valid, uint16_t* data, uint32_t requestId) {
    uint8_t portIndex = (uint8_t)requestId;
    
    if (portIndex >= MAX_FLOW_COUNTERS) {
        log(LOG_ERROR, false, "Invalid port index in callback: %d\n", portIndex);
        return;
    }
    
    if (!valid || data == nullptr) {
        log(LOG_WARNING, false, "Modbus read failed for port %d\n", portIndex + 1);
        
        if (!flowCounterDataLocked) {
            flowCounterDataLocked = true;
            // Only set commError if device was previously connected
            if (flowCounterData[portIndex].dataValid) {
                flowCounterData[portIndex].commError = true;
            }
            flowCounterData[portIndex].modbusRequestPending = false;
            flowCounterDataLocked = false;
            leds.setPixelColor(portIndex + 2, LED_COLOR_RED);  // Red for error
            leds.show();
        }
        return;
    }
    
    // Parse the response data
    if (!flowCounterDataLocked) {
        flowCounterDataLocked = true;
        
        // Convert register pairs to floats and uint32_t
        // Each float is 2 registers (4 bytes), big-endian
        uint8_t regIdx = 0;
        
        // Helper function to convert 2 registers to float (CDAB word order)
        auto regsToFloat = [](uint16_t* regs) -> float {
            // Modbus typically uses CDAB word order for 32-bit values
            // Swap the words: [low_word][high_word] -> [high_word][low_word]
            uint32_t value = ((uint32_t)regs[1] << 16) | regs[0];
            float result;
            memcpy(&result, &value, sizeof(float));
            return result;
        };
        
        // Helper function to convert 2 registers to uint32_t (CDAB word order)
        auto regsToUint32 = [](uint16_t* regs) -> uint32_t {
            return ((uint32_t)regs[1] << 16) | regs[0];
        };
        
        flowCounterData[portIndex].volume = regsToFloat(&data[regIdx]);
        regIdx += 2;
        
        flowCounterData[portIndex].volume_normalised = regsToFloat(&data[regIdx]);
        regIdx += 2;
        
        flowCounterData[portIndex].flow = regsToFloat(&data[regIdx]);
        regIdx += 2;
        
        flowCounterData[portIndex].flow_normalised = regsToFloat(&data[regIdx]);
        regIdx += 2;
        
        flowCounterData[portIndex].temperature = regsToFloat(&data[regIdx]);
        flowCounterData[portIndex].currentTemperature = flowCounterData[portIndex].temperature;  // Also update current
        regIdx += 2;
        
        flowCounterData[portIndex].pressure = regsToFloat(&data[regIdx]);
        flowCounterData[portIndex].currentPressure = flowCounterData[portIndex].pressure;  // Also update current
        regIdx += 2;
        
        flowCounterData[portIndex].timestamp = regsToUint32(&data[regIdx]);
        regIdx += 2;
        
        flowCounterData[portIndex].psu_volts = regsToFloat(&data[regIdx]);
        regIdx += 2;
        
        flowCounterData[portIndex].batt_volts = regsToFloat(&data[regIdx]);
        regIdx += 2;
        
        // Extract unit_ID (5 registers = 10 bytes)
        // Flow counter uses low byte first, then high byte (BA DC FE HG JI order)
        for (int i = 0; i < 5; i++) {
            flowCounterData[portIndex].unit_ID[i * 2] = data[regIdx + i] & 0xFF;          // Low byte first
            flowCounterData[portIndex].unit_ID[i * 2 + 1] = (data[regIdx + i] >> 8) & 0xFF;  // High byte second
        }
        flowCounterData[portIndex].unit_ID[10] = '\0';  // Null terminate (buffer is 11 bytes)
        
        // Debug: log the raw register values
        log(LOG_DEBUG, false, "Unit ID registers: 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X -> '%s'\\n",
            data[regIdx], data[regIdx+1], data[regIdx+2], data[regIdx+3], data[regIdx+4],
            flowCounterData[portIndex].unit_ID);
        
        bool wasFirstConnection = !flowCounterData[portIndex].dataValid;
        
        flowCounterData[portIndex].dataValid = true;
        flowCounterData[portIndex].commError = false;
        flowCounterData[portIndex].lastUpdate = millis();
        flowCounterData[portIndex].triggerCount++;
        flowCounterData[portIndex].modbusRequestPending = false;  // Clear pending flag
        
        flowCounterDataLocked = false;
        
        // Set LED to green - data is valid
        leds.setPixelColor(portIndex + 2, LED_COLOR_GREEN);  // Green
        leds.show();
        
        if (wasFirstConnection) {
            log(LOG_INFO, true, "Port %d: Device '%s' connected for the first time\n", portIndex + 1, flowCounterData[portIndex].unit_ID);
        }
        
        log(LOG_DEBUG, false, "Port %d TRIGGER: Unit='%s', Vol=%.2f, Vol_N=%.2f, Flow=%.2f, Flow_N=%.2f, Temp=%.1f°C, Press=%.1fkPa\n",
            portIndex + 1,
            flowCounterData[portIndex].unit_ID,
            flowCounterData[portIndex].volume,
            flowCounterData[portIndex].volume_normalised,
            flowCounterData[portIndex].flow,
            flowCounterData[portIndex].flow_normalised,
            flowCounterData[portIndex].temperature,
            flowCounterData[portIndex].pressure);
        
        // Log to SD card if enabled
        if (gatewayConfig.ports[portIndex].logToSD && sdInfo.ready) {
            char csvLine[256];
            snprintf(csvLine, sizeof(csvLine),
                    "%lu,%.3f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.2f\n",
                    flowCounterData[portIndex].timestamp,
                    flowCounterData[portIndex].volume,
                    flowCounterData[portIndex].volume_normalised,
                    flowCounterData[portIndex].flow,
                    flowCounterData[portIndex].flow_normalised,
                    flowCounterData[portIndex].temperature,
                    flowCounterData[portIndex].pressure,
                    flowCounterData[portIndex].psu_volts,
                    flowCounterData[portIndex].batt_volts);
            
            // Create filename from portName and unit_ID
            char filename[64];
            if (strlen(gatewayConfig.ports[portIndex].portName) > 0) {
                snprintf(filename, sizeof(filename), "/%s_%s.csv", 
                        gatewayConfig.ports[portIndex].portName, 
                        flowCounterData[portIndex].unit_ID);
            } else {
                snprintf(filename, sizeof(filename), "/%s.csv", flowCounterData[portIndex].unit_ID);
            }
            
            // Write header if file doesn't exist
            bool fileExists = false;
            if (!sdLocked) {
                sdLocked = true;
                fileExists = sd.exists(filename);
                sdLocked = false;
            }
            
            if (!fileExists) {
                const char* header = "Timestamp,Volume,Volume_Norm,Flow,Flow_Norm,Temperature,Pressure,PSU_Volts,Batt_Volts\n";
                writeSensorData(header, filename, true);
            }
            
            // Write data
            writeSensorData(csvLine, filename, false);
        }
    }
}

// Callback for temperature/pressure only reads
void modbusTempPressureCallback(bool valid, uint16_t* data, uint32_t requestId) {
    uint8_t portIndex = (uint8_t)requestId;
    
    if (portIndex >= MAX_FLOW_COUNTERS) {
        log(LOG_ERROR, false, "Invalid port index in temp/press callback: %d\n", portIndex);
        return;
    }
    
    if (!valid || data == nullptr) {
        log(LOG_WARNING, false, "Modbus temp/pressure read failed for port %d\n", portIndex + 1);
        
        if (!flowCounterDataLocked) {
            flowCounterDataLocked = true;
            // Only set commError if device was previously connected
            // If device never connected (dataValid == false), don't mark as error
            if (flowCounterData[portIndex].dataValid) {
                flowCounterData[portIndex].commError = true;
            }
            flowCounterData[portIndex].modbusRequestPending = false;  // Clear pending flag
            flowCounterDataLocked = false;
            
            // Set LED directly to red if this is a comm error, purple if never connected
            if (flowCounterData[portIndex].dataValid) {
                leds.setPixelColor(portIndex + 2, LED_COLOR_RED);  // Red for error
            } else {
                leds.setPixelColor(portIndex + 2, LED_COLOR_PURPLE);  // Purple for not yet connected
            }
            leds.show();
        }
        return;
    }
    
    // Parse only temperature and pressure from the response data
    if (!flowCounterDataLocked) {
        flowCounterDataLocked = true;
        
        // Helper function to convert 2 registers to float (CDAB word order)
        auto regsToFloat = [](uint16_t* regs) -> float {
            // Modbus typically uses CDAB word order for 32-bit values
            // Swap the words: [low_word][high_word] -> [high_word][low_word]
            uint32_t value = ((uint32_t)regs[1] << 16) | regs[0];
            float result;
            memcpy(&result, &value, sizeof(float));
            return result;
        };
        
        // Store old values for comparison
        float oldCurrentTemp = flowCounterData[portIndex].currentTemperature;
        float oldCurrentPressure = flowCounterData[portIndex].currentPressure;
        float snapshotTemp = flowCounterData[portIndex].temperature;
        float snapshotPressure = flowCounterData[portIndex].pressure;
        float snapshotVolume = flowCounterData[portIndex].volume;
        float snapshotFlow = flowCounterData[portIndex].flow;
        
        // Update ONLY currentTemperature and currentPressure (for registers 30-33)
        // Do NOT update temperature/pressure (registers 8-11) - those are snapshot values
        flowCounterData[portIndex].currentTemperature = regsToFloat(&data[0]);  // First 2 registers
        flowCounterData[portIndex].currentPressure = regsToFloat(&data[2]);     // Next 2 registers
        
        // Clear comm error since we got a successful response
        // If this is the first successful periodic poll, dataValid may still be false
        // (full data comes from trigger reads), so we just clear any error state
        bool wasInError = flowCounterData[portIndex].commError;
        flowCounterData[portIndex].commError = false;
        flowCounterData[portIndex].modbusRequestPending = false;  // Clear pending flag
        
        // Update lastUpdate timestamp
        flowCounterData[portIndex].lastUpdate = millis();
        
        flowCounterDataLocked = false;
        
        // Set LED directly - green if we have data, purple if not yet fully connected
        if (flowCounterData[portIndex].dataValid) {
            leds.setPixelColor(portIndex + 2, LED_COLOR_GREEN);  // Green
            if (wasInError) {
                log(LOG_INFO, true, "Port %d: Device recovered from error\n", portIndex + 1);
            }
        } else {
            leds.setPixelColor(portIndex + 2, LED_COLOR_PURPLE);  // Purple - not yet fully connected
        }
        leds.show();
        
        log(LOG_DEBUG, false, "Port %d PERIODIC: Current Temp %.1f->%.1f°C, Current Press %.1f->%.1fkPa | Snapshot: Vol=%.2f, Flow=%.2f, Temp=%.1f°C, Press=%.1fkPa (all unchanged)\n",
            portIndex + 1,
            oldCurrentTemp, flowCounterData[portIndex].currentTemperature,
            oldCurrentPressure, flowCounterData[portIndex].currentPressure,
            snapshotVolume, snapshotFlow, snapshotTemp, snapshotPressure);
    }
}

// Poll all configured devices on startup
void pollAllConfiguredDevices() {
    for (int i = 0; i < MAX_FLOW_COUNTERS; i++) {
        if (gatewayConfig.ports[i].enabled) {
            readFlowCounter(i);
            
            // Wait for response before polling next device
            // Allow up to 500ms for each device to respond
            // IMPORTANT: Keep calling checkTriggers to maintain edge detection
            uint32_t startTime = millis();
            uint32_t lastTrigCheck = millis();
            while (millis() - startTime < 500) {
                modbusRTU.manage();
                
                // Check triggers every 10ms to keep edge detection in sync
                if (millis() - lastTrigCheck >= TRIGGER_CHECK_INTERVAL) {
                    lastTrigCheck = millis();
                    checkTriggers();
                }
                
                delay(1);  // Short delay to prevent tight loop
            }
            
            // After each device poll, clear its trigger flag to prevent
            // double-reading if a trigger occurred during the poll
            // BUT: Make sure we still detect the rising edge to reset LED
            if (triggerFlags[i]) {
                triggerFlags[i] = false;
            }
        }
    }
}

// Check offline devices periodically
void checkOfflineDevices() {
    static uint8_t checkIndex = 0;
    
    // Check one offline device per interval to avoid flooding the bus
    for (int attempts = 0; attempts < MAX_FLOW_COUNTERS; attempts++) {
        uint8_t i = (checkIndex + attempts) % MAX_FLOW_COUNTERS;
        
        if (gatewayConfig.ports[i].enabled && 
            (flowCounterData[i].commError || !flowCounterData[i].dataValid)) {
            
            log(LOG_INFO, false, "Checking offline device on port %d (Slave ID: %d)\n", 
                i + 1, gatewayConfig.ports[i].slaveId);
            
            readFlowCounter(i);
            checkIndex = (i + 1) % MAX_FLOW_COUNTERS;
            return;  // Only check one device per interval
        }
    }
    
    // All devices are online or none configured
    checkIndex = 0;
}

// Periodic poll of all configured devices (every 10 seconds during testing, normally 1 minute)
// This keeps temperature and pressure readings up-to-date and verifies device connectivity
// Strategy:
//   - Never-connected devices (dataValid == false): Do full read to get initial data
//   - Connected devices (dataValid == true): Do temp/pressure-only read to preserve snapshot values
//   - Devices in error that were previously connected: Do temp/pressure read to check recovery
void periodicPollConfiguredDevices() {
    // Poll all enabled devices with appropriate read type
    for (int i = 0; i < MAX_FLOW_COUNTERS; i++) {
        if (gatewayConfig.ports[i].enabled) {
            if (!flowCounterData[i].dataValid) {
                // Never connected - do full read to get initial data
                readFlowCounter(i);
            } else {
                // Previously connected - temp/pressure only to preserve snapshot
                readFlowCounterTempPressure(i);
            }
            
            // Small delay between device reads to avoid bus congestion
            delay(10);
        }
    }
}
