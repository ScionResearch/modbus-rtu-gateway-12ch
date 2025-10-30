/**
 * @file ModbusWriteExample.ino
 * @brief Example for writing data using the ModbusRTUMaster library
 * 
 * This example demonstrates how to use the ModbusRTUMaster library to write
 * to registers and coils on a Modbus slave device.
 */

#include "modbus-rtu-master.h"

// Create Modbus master instance
ModbusRTUMaster modbus;

// Configuration
#define SLAVE_ID 1             // Modbus slave ID
#define REGISTER_ADDRESS 100   // Register address to write
#define COIL_ADDRESS 200       // Coil address to write
#define LED_PIN LED_BUILTIN    // LED for visual feedback
#define MODBUS_INTERVAL 3000   // Interval between Modbus operations (ms)

// Multiple registers example
uint16_t multipleRegisters[] = {1111, 2222, 3333, 4444, 5555};
#define MULTI_REG_COUNT (sizeof(multipleRegisters) / sizeof(multipleRegisters[0]))

// Multiple coils example (16 coils per uint16_t)
uint16_t multipleCoils[] = {0xAAAA, 0x5555}; // Alternating on/off pattern
#define MULTI_COIL_COUNT 32 // 2 uint16_t values x 16 bits each = 32 coils

// Timing variables
unsigned long lastModbusOperation = 0;
unsigned long lastLedToggle = 0;
bool ledState = false;

// State machine for cycling through different Modbus operations
enum OperationState {
  WRITE_SINGLE_REGISTER,
  WRITE_MULTIPLE_REGISTERS,
  WRITE_SINGLE_COIL,
  WRITE_MULTIPLE_COILS
};
OperationState currentOperation = WRITE_SINGLE_REGISTER;

// Flag to track if a request is pending
bool requestPending = false;

/**
 * @brief Callback function for Modbus responses
 * 
 * @param valid True if a valid response was received, false on timeout/error
 * @param data Pointer to the data buffer (not used for write operations)
 */
void modbusCallback(bool valid, uint16_t* data) {
  if (valid) {
    switch (currentOperation) {
      case WRITE_SINGLE_REGISTER:
        Serial.println("Successfully wrote single register");
        break;
      case WRITE_MULTIPLE_REGISTERS:
        Serial.println("Successfully wrote multiple registers");
        break;
      case WRITE_SINGLE_COIL:
        Serial.println("Successfully wrote single coil");
        break;
      case WRITE_MULTIPLE_COILS:
        Serial.println("Successfully wrote multiple coils");
        break;
    }
  } else {
    Serial.print("Modbus operation failed: ");
    switch (currentOperation) {
      case WRITE_SINGLE_REGISTER:
        Serial.println("write single register");
        break;
      case WRITE_MULTIPLE_REGISTERS:
        Serial.println("write multiple registers");
        break;
      case WRITE_SINGLE_COIL:
        Serial.println("write single coil");
        break;
      case WRITE_MULTIPLE_COILS:
        Serial.println("write multiple coils");
        break;
    }
  }
  
  // Move to next operation
  currentOperation = (OperationState)((currentOperation + 1) % 4);
  
  // Clear the pending request flag
  requestPending = false;
}

void setup() {
  // Initialize serial ports
  Serial.begin(115200);  // Debug serial port
  Serial1.begin(9600, SERIAL_8N1);  // Modbus serial port
  
  // Initialize the Modbus master with Serial1
  modbus.begin(&Serial1);
  
  // Set timeout to 500ms (default is 1000ms)
  modbus.setTimeout(500);
  
  // Set LED pin as output
  pinMode(LED_PIN, OUTPUT);
  
  Serial.println("Modbus RTU Master Write Example");
  Serial.println("-------------------------------");
  Serial.println("Cycling through different write operations");
}

void loop() {
  // Call manage() regularly to process the Modbus queue
  modbus.manage();
  
  // Toggle LED every 200ms to demonstrate non-blocking operation
  if (millis() - lastLedToggle >= 200) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
    lastLedToggle = millis();
  }
  
  // Send a new Modbus request if no request is pending and the interval has elapsed
  if (!requestPending && (millis() - lastModbusOperation >= MODBUS_INTERVAL)) {
    bool success = false;
    
    switch (currentOperation) {
      case WRITE_SINGLE_REGISTER:
        {
          uint16_t value = random(1, 1000); // Random value between 1-999
          Serial.print("Writing value ");
          Serial.print(value);
          Serial.print(" to register ");
          Serial.println(REGISTER_ADDRESS);
          success = modbus.writeSingleRegister(SLAVE_ID, REGISTER_ADDRESS, value, modbusCallback);
        }
        break;
        
      case WRITE_MULTIPLE_REGISTERS:
        Serial.print("Writing ");
        Serial.print(MULTI_REG_COUNT);
        Serial.print(" registers starting at address ");
        Serial.println(REGISTER_ADDRESS);
        success = modbus.writeMultipleRegisters(SLAVE_ID, REGISTER_ADDRESS, 
                                               multipleRegisters, MULTI_REG_COUNT, 
                                               modbusCallback);
        break;
        
      case WRITE_SINGLE_COIL:
        {
          bool value = (millis() % 2000) < 1000; // Alternate true/false
          Serial.print("Writing coil ");
          Serial.print(COIL_ADDRESS);
          Serial.print(" to state: ");
          Serial.println(value ? "ON" : "OFF");
          success = modbus.writeSingleCoil(SLAVE_ID, COIL_ADDRESS, value, modbusCallback);
        }
        break;
        
      case WRITE_MULTIPLE_COILS:
        Serial.print("Writing ");
        Serial.print(MULTI_COIL_COUNT);
        Serial.print(" coils starting at address ");
        Serial.println(COIL_ADDRESS);
        success = modbus.writeMultipleCoils(SLAVE_ID, COIL_ADDRESS, 
                                           multipleCoils, MULTI_COIL_COUNT, 
                                           modbusCallback);
        break;
    }
    
    if (success) {
      Serial.println("Request queued successfully");
      requestPending = true;
      lastModbusOperation = millis();
    } else {
      Serial.println("Failed to queue request (queue might be full)");
    }
  }
  
  // Other non-blocking tasks can be performed here
  // ...
}
