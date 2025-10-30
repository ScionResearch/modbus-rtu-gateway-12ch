/**
 * @file SimpleModbusMaster.ino
 * @brief Example for the ModbusRTUMaster library
 * 
 * This example demonstrates how to use the ModbusRTUMaster library to read
 * holding registers from a Modbus slave device. It shows the non-blocking
 * nature of the library by toggling an LED while processing Modbus requests.
 */

#include "modbus-rtu-master.h"

// Create Modbus master instance
ModbusRTUMaster modbus;

// Configuration
#define SLAVE_ID 1           // Modbus slave ID
#define START_ADDRESS 0      // Starting register address to read
#define REGISTER_COUNT 10    // Number of registers to read
#define LED_PIN LED_BUILTIN  // LED for visual feedback
#define MODBUS_INTERVAL 1000 // Interval between Modbus requests (ms)

// Buffer to store register values
uint16_t registers[REGISTER_COUNT];

// Timing variables
unsigned long lastModbusRequest = 0;
unsigned long lastLedToggle = 0;
bool ledState = false;

// Flag to track if a request is pending
bool requestPending = false;

/**
 * @brief Callback function for Modbus responses
 * 
 * This function is called when a response is received or a timeout occurs.
 * 
 * @param valid True if a valid response was received, false on timeout/error
 * @param data Pointer to the data buffer with the response
 */
void modbusCallback(bool valid, uint16_t* data) {
  if (valid) {
    Serial.println("Received valid Modbus response:");
    for (int i = 0; i < REGISTER_COUNT; i++) {
      Serial.print("Register ");
      Serial.print(START_ADDRESS + i);
      Serial.print(": ");
      Serial.println(registers[i]);
    }
  } else {
    Serial.println("Modbus request failed (timeout or error)");
  }
  
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
  
  Serial.println("Modbus RTU Master Example");
  Serial.println("------------------------");
  Serial.println("Reading holding registers from slave device");
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
  if (!requestPending && (millis() - lastModbusRequest >= MODBUS_INTERVAL)) {
    // Read holding registers
    if (modbus.readHoldingRegisters(SLAVE_ID, START_ADDRESS, registers, REGISTER_COUNT, modbusCallback)) {
      Serial.println("Sent Modbus request to read holding registers");
      requestPending = true;
      lastModbusRequest = millis();
    } else {
      Serial.println("Failed to queue Modbus request (queue might be full)");
    }
  }
  
  // Other non-blocking tasks can be performed here
  // ...
}
