/**
 * @file CompleteModbusExample.ino
 * @brief Comprehensive example for the ModbusRTUMaster library
 * 
 * This example demonstrates how to read and write different types of Modbus data:
 * - Holding Registers
 * - Input Registers
 * - Coils
 * - Discrete Inputs
 * 
 * The example rotates through different operations to showcase the non-blocking
 * nature of the library and proper handling of responses using callbacks.
 */

#include "modbus-rtu-master.h"

// Create Modbus master instance
ModbusRTUMaster modbus;

// Configuration
#define SLAVE_ID 1             // Modbus slave ID
#define LED_PIN LED_BUILTIN    // LED for visual feedback
#define MODBUS_INTERVAL 2000   // Interval between Modbus operations (ms)
#define SERIAL_BAUD 9600       // Modbus serial baud rate

// Register and coil addresses (these would be specific to your slave device)
#define HOLDING_REG_ADDR 1000
#define INPUT_REG_ADDR 2000
#define COIL_ADDR 3000
#define DISCRETE_INPUT_ADDR 4000

// Data buffer sizes
#define REG_COUNT 6
#define COIL_COUNT 32

// Data buffers
uint16_t holdingRegisters[REG_COUNT];
uint16_t inputRegisters[REG_COUNT];
uint16_t coils[2];          // 2 uint16_t can store 32 coils (16 each)
uint16_t discreteInputs[2]; // 2 uint16_t can store 32 inputs (16 each)

// Timing variables
unsigned long lastModbusOperation = 0;
unsigned long lastLedToggle = 0;
bool ledState = false;

// State machine for cycling through different Modbus operations
enum OperationState {
  READ_HOLDING_REGISTERS,
  READ_INPUT_REGISTERS,
  READ_COILS,
  READ_DISCRETE_INPUTS,
  WRITE_HOLDING_REGISTER,
  WRITE_MULTIPLE_REGISTERS,
  WRITE_COIL,
  WRITE_MULTIPLE_COILS
};
OperationState currentOperation = READ_HOLDING_REGISTERS;

// Flag to track if a request is pending
bool requestPending = false;

/**
 * @brief Print the bit status from a uint16_t array
 * 
 * @param bitArray Array containing bit values
 * @param startAddr Starting address (for display purposes)
 * @param count Number of bits to display
 */
void printBitValues(uint16_t* bitArray, uint16_t startAddr, uint16_t count) {
  for (uint16_t i = 0; i < count; i++) {
    uint16_t byteIndex = i / 16;
    uint16_t bitIndex = i % 16;
    bool bitValue = bitArray[byteIndex] & (1 << bitIndex);
    
    Serial.print("Bit ");
    Serial.print(startAddr + i);
    Serial.print(": ");
    Serial.println(bitValue ? "ON" : "OFF");
  }
}

/**
 * @brief Callback function for Modbus responses
 * 
 * @param valid True if a valid response was received, false on timeout/error
 * @param data Pointer to the data buffer with the response
 */
void modbusCallback(bool valid, uint16_t* data) {
  if (valid) {
    Serial.println("Received valid response for: ");
    
    switch (currentOperation) {
      case READ_HOLDING_REGISTERS:
        Serial.println("Read Holding Registers");
        for (int i = 0; i < REG_COUNT; i++) {
          Serial.print("Register ");
          Serial.print(HOLDING_REG_ADDR + i);
          Serial.print(": ");
          Serial.println(holdingRegisters[i]);
        }
        break;
        
      case READ_INPUT_REGISTERS:
        Serial.println("Read Input Registers");
        for (int i = 0; i < REG_COUNT; i++) {
          Serial.print("Register ");
          Serial.print(INPUT_REG_ADDR + i);
          Serial.print(": ");
          Serial.println(inputRegisters[i]);
        }
        break;
        
      case READ_COILS:
        Serial.println("Read Coils");
        printBitValues(coils, COIL_ADDR, COIL_COUNT);
        break;
        
      case READ_DISCRETE_INPUTS:
        Serial.println("Read Discrete Inputs");
        printBitValues(discreteInputs, DISCRETE_INPUT_ADDR, COIL_COUNT);
        break;
        
      case WRITE_HOLDING_REGISTER:
        Serial.println("Write Single Register - SUCCESS");
        break;
        
      case WRITE_MULTIPLE_REGISTERS:
        Serial.println("Write Multiple Registers - SUCCESS");
        break;
        
      case WRITE_COIL:
        Serial.println("Write Single Coil - SUCCESS");
        break;
        
      case WRITE_MULTIPLE_COILS:
        Serial.println("Write Multiple Coils - SUCCESS");
        break;
    }
  } else {
    Serial.print("Modbus operation FAILED: ");
    
    switch (currentOperation) {
      case READ_HOLDING_REGISTERS:
        Serial.println("Read Holding Registers");
        break;
      case READ_INPUT_REGISTERS:
        Serial.println("Read Input Registers");
        break;
      case READ_COILS:
        Serial.println("Read Coils");
        break;
      case READ_DISCRETE_INPUTS:
        Serial.println("Read Discrete Inputs");
        break;
      case WRITE_HOLDING_REGISTER:
        Serial.println("Write Single Register");
        break;
      case WRITE_MULTIPLE_REGISTERS:
        Serial.println("Write Multiple Registers");
        break;
      case WRITE_COIL:
        Serial.println("Write Single Coil");
        break;
      case WRITE_MULTIPLE_COILS:
        Serial.println("Write Multiple Coils");
        break;
    }
  }
  
  // Move to next operation
  currentOperation = (OperationState)((currentOperation + 1) % 8);
  
  // Clear the pending request flag
  requestPending = false;
}

void setup() {
  // Initialize serial ports
  Serial.begin(115200);  // Debug serial port
  Serial1.begin(SERIAL_BAUD, SERIAL_8N1);  // Modbus serial port
  
  // Initialize the Modbus master with Serial1
  modbus.begin(&Serial1);
  
  // Set timeout to 500ms (default is 1000ms)
  modbus.setTimeout(500);
  
  // Set LED pin as output
  pinMode(LED_PIN, OUTPUT);
  
  // Initialize data for writing operations
  for (int i = 0; i < REG_COUNT; i++) {
    holdingRegisters[i] = 1000 + i;
  }
  
  // Pattern for coils (alternating on/off)
  coils[0] = 0xAAAA;
  coils[1] = 0x5555;
  
  Serial.println("Modbus RTU Master Complete Example");
  Serial.println("----------------------------------");
  Serial.println("Cycling through different Modbus operations");
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
    
    // Print current operation
    Serial.print("Performing operation: ");
    
    switch (currentOperation) {
      case READ_HOLDING_REGISTERS:
        Serial.println("Read Holding Registers");
        success = modbus.readHoldingRegisters(SLAVE_ID, HOLDING_REG_ADDR, 
                                             holdingRegisters, REG_COUNT, 
                                             modbusCallback);
        break;
        
      case READ_INPUT_REGISTERS:
        Serial.println("Read Input Registers");
        success = modbus.readInputRegisters(SLAVE_ID, INPUT_REG_ADDR, 
                                           inputRegisters, REG_COUNT, 
                                           modbusCallback);
        break;
        
      case READ_COILS:
        Serial.println("Read Coils");
        success = modbus.readCoils(SLAVE_ID, COIL_ADDR, 
                                  coils, COIL_COUNT, 
                                  modbusCallback);
        break;
        
      case READ_DISCRETE_INPUTS:
        Serial.println("Read Discrete Inputs");
        success = modbus.readDiscreteInputs(SLAVE_ID, DISCRETE_INPUT_ADDR, 
                                           discreteInputs, COIL_COUNT, 
                                           modbusCallback);
        break;
        
      case WRITE_HOLDING_REGISTER:
        {
          uint16_t value = random(1, 1000); // Random value between 1-999
          Serial.print("Write Single Register - Value: ");
          Serial.println(value);
          success = modbus.writeSingleRegister(SLAVE_ID, HOLDING_REG_ADDR, 
                                              value, modbusCallback);
        }
        break;
        
      case WRITE_MULTIPLE_REGISTERS:
        Serial.println("Write Multiple Registers");
        success = modbus.writeMultipleRegisters(SLAVE_ID, HOLDING_REG_ADDR, 
                                               holdingRegisters, REG_COUNT, 
                                               modbusCallback);
        break;
        
      case WRITE_COIL:
        {
          bool value = (millis() % 2000) < 1000; // Alternate true/false
          Serial.print("Write Single Coil - Value: ");
          Serial.println(value ? "ON" : "OFF");
          success = modbus.writeSingleCoil(SLAVE_ID, COIL_ADDR, value, modbusCallback);
        }
        break;
        
      case WRITE_MULTIPLE_COILS:
        Serial.println("Write Multiple Coils");
        success = modbus.writeMultipleCoils(SLAVE_ID, COIL_ADDR, 
                                           coils, COIL_COUNT, 
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
