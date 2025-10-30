/**
 * @file ModbusWithRS485.ino
 * @brief Example showing how to use ModbusRTUMaster with RS485 communication
 * 
 * This example demonstrates how to use the ModbusRTUMaster library with an RS485 transceiver,
 * properly controlling the DE/RE pins for direction control. RS485 is a common physical layer
 * for Modbus RTU communication in industrial environments.
 */

#include "modbus-rtu-master.h"

// Create Modbus master instance
ModbusRTUMaster modbus;

// Configuration
#define SLAVE_ID 1           // Modbus slave ID
#define REGISTER_ADDRESS 100 // Starting register address to read/write
#define REGISTER_COUNT 5     // Number of registers to read/write
#define LED_PIN LED_BUILTIN  // LED for visual feedback

// RS485 control pin
#define RS485_DE_PIN 2       // Data Enable pin for RS485 transceiver
#define RS485_RE_PIN 3       // Receiver Enable pin (often connected to DE pin)

// Timing variables
unsigned long lastModbusRequest = 0;
unsigned long lastLedToggle = 0;
bool ledState = false;

// Operation state
enum OperationState {
  READ_REGISTERS,
  WRITE_REGISTERS
};
OperationState currentOperation = READ_REGISTERS;

// Data buffer
uint16_t registers[REGISTER_COUNT];

// Request tracking
bool requestPending = false;

/**
 * @brief Function to enable RS485 transmission mode
 */
void preTransmission() {
  digitalWrite(RS485_DE_PIN, HIGH); // Enable driver (transmit mode)
  digitalWrite(RS485_RE_PIN, HIGH); // Disable receiver
  delayMicroseconds(50);            // Small delay to allow line to stabilize
}

/**
 * @brief Function to enable RS485 reception mode
 */
void postTransmission() {
  delayMicroseconds(50);            // Small delay before releasing the line
  digitalWrite(RS485_DE_PIN, LOW);  // Disable driver (receive mode)
  digitalWrite(RS485_RE_PIN, LOW);  // Enable receiver
}

/**
 * @brief Callback function for Modbus responses
 * 
 * @param valid True if a valid response was received, false on timeout/error
 * @param data Pointer to the data buffer with the response
 */
void modbusCallback(bool valid, uint16_t* data) {
  if (valid) {
    if (currentOperation == READ_REGISTERS) {
      Serial.println("Successfully read registers:");
      for (int i = 0; i < REGISTER_COUNT; i++) {
        Serial.print("Register ");
        Serial.print(REGISTER_ADDRESS + i);
        Serial.print(": ");
        Serial.println(registers[i]);
      }
    } else {
      Serial.println("Successfully wrote registers");
    }
  } else {
    Serial.print("Modbus operation failed: ");
    Serial.println(currentOperation == READ_REGISTERS ? "read registers" : "write registers");
  }
  
  // Toggle operation for next request
  currentOperation = (currentOperation == READ_REGISTERS) ? WRITE_REGISTERS : READ_REGISTERS;
  
  // Clear the pending request flag
  requestPending = false;
}

void setup() {
  // Initialize serial ports
  Serial.begin(115200);  // Debug serial port
  Serial1.begin(9600, SERIAL_8N1);  // Modbus serial port
  
  // Set up RS485 control pins
  pinMode(RS485_DE_PIN, OUTPUT);
  pinMode(RS485_RE_PIN, OUTPUT);
  
  // Initially set to receive mode
  digitalWrite(RS485_DE_PIN, LOW);
  digitalWrite(RS485_RE_PIN, LOW);
  
  // Initialize the Modbus master with Serial1
  modbus.begin(&Serial1);
  
  // Set the pre- and post-transmission callbacks
  // Note: This is simulated here as our library doesn't directly support callbacks.
  // You'll need to add this functionality to the library or manually control the pins.
  // See the explanation below in the loop() function.
  
  // Set timeout to 1000ms
  modbus.setTimeout(1000);
  
  // Set LED pin as output
  pinMode(LED_PIN, OUTPUT);
  
  // Initialize test data
  for (int i = 0; i < REGISTER_COUNT; i++) {
    registers[i] = 1000 + i;
  }
  
  Serial.println("Modbus RTU Master with RS485 Example");
  Serial.println("------------------------------------");
  Serial.println("This example alternates between reading and writing registers");
}

void loop() {
  // Toggle LED every 200ms to demonstrate non-blocking operation
  if (millis() - lastLedToggle >= 200) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
    lastLedToggle = millis();
  }
  
  // Note: When using RS485, you'll need to manually control the DE/RE pins
  // during the manage() function. Here's how to simulate this if the library
  // doesn't have native support for RS485 callbacks:
  
  // 1. Before sending data, call preTransmission()
  // 2. Call manage() to process the queue and send data
  // 3. After sending data, call postTransmission()
  
  // For demonstration purposes, we'll use these around our manage call:
  if (requestPending) {
    // We only want to set transmit mode when we know there's a request pending
    preTransmission();
  }
  
  // Call manage() regularly to process the Modbus queue
  modbus.manage();
  
  if (requestPending) {
    // Switch back to receive mode after manage() has handled sending
    postTransmission();
  }
  
  // Send a new Modbus request every 5 seconds if no request is pending
  if (!requestPending && (millis() - lastModbusRequest >= 5000)) {
    bool success = false;
    
    // Before sending, ensure the transceiver is in transmit mode
    preTransmission();
    
    if (currentOperation == READ_REGISTERS) {
      Serial.print("Reading ");
      Serial.print(REGISTER_COUNT);
      Serial.print(" registers starting at address ");
      Serial.println(REGISTER_ADDRESS);
      
      success = modbus.readHoldingRegisters(SLAVE_ID, REGISTER_ADDRESS, 
                                           registers, REGISTER_COUNT, 
                                           modbusCallback);
    } else {
      Serial.print("Writing ");
      Serial.print(REGISTER_COUNT);
      Serial.print(" registers starting at address ");
      Serial.println(REGISTER_ADDRESS);
      
      // Update the values before writing
      for (int i = 0; i < REGISTER_COUNT; i++) {
        registers[i] = 1000 + i + (millis() % 1000);
      }
      
      success = modbus.writeMultipleRegisters(SLAVE_ID, REGISTER_ADDRESS, 
                                             registers, REGISTER_COUNT, 
                                             modbusCallback);
    }
    
    // Switch back to receive mode after sending the request
    postTransmission();
    
    if (success) {
      Serial.println("Request queued successfully");
      requestPending = true;
      lastModbusRequest = millis();
    } else {
      Serial.println("Failed to queue request (queue might be full)");
    }
  }
  
  // Other non-blocking tasks can be performed here
  // ...
}

/**
 * Implementation notes for integrating RS485 with ModbusRTUMaster:
 * 
 * Option 1: Manual control (as shown in this example)
 * - Manually control the DE/RE pins around the manage() call and when sending requests
 * - This works but requires careful timing and may not be ideal for all applications
 * 
 * Option 2: Library modification (recommended for production use)
 * - Modify the ModbusRTUMaster library to support pre/post transmission callbacks
 * - Add the following to the class header:
 *   - typedef void (*ModbusTransmissionCallback)();
 *   - ModbusTransmissionCallback _preTransmissionCallback;
 *   - ModbusTransmissionCallback _postTransmissionCallback;
 *   - void setTransmissionCallbacks(ModbusTransmissionCallback pre, ModbusTransmissionCallback post);
 * 
 * - Call the callbacks in the _sendRequest and manage methods:
 *   - Before writing to the serial port: if(_preTransmissionCallback) _preTransmissionCallback();
 *   - After writing to the serial port: if(_postTransmissionCallback) _postTransmissionCallback();
 * 
 * This latter approach is how libraries like ModbusMaster handle this scenario.
 */
