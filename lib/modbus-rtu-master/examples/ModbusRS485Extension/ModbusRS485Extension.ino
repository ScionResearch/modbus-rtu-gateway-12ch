/**
 * @file ModbusRS485Extension.ino
 * @brief Example using the ModbusRTUMaster_RS485 extension
 * 
 * This example demonstrates how to use the ModbusRTUMaster_RS485 extension class
 * which adds proper RS485 support with transmission callbacks for controlling
 * DE/RE pins. This approach is cleaner than manually controlling the pins in
 * the main sketch.
 */

#include "ModbusRTUMaster_RS485.h"

// Create RS485-enabled Modbus master instance
ModbusRTUMaster_RS485 modbus;

// Configuration
#define SLAVE_ID 1           // Modbus slave ID
#define REGISTER_ADDRESS 100 // Starting register address to read
#define REGISTER_COUNT 5     // Number of registers to read
#define LED_PIN LED_BUILTIN  // LED for visual feedback

// RS485 control pin
#define RS485_DE_PIN 2       // Data Enable pin for RS485 transceiver
#define RS485_RE_PIN 2       // Receiver Enable pin (connected to same pin as DE in this example)

// Timing variables
unsigned long lastModbusRequest = 0;
unsigned long lastLedToggle = 0;
bool ledState = false;

// Data buffer
uint16_t registers[REGISTER_COUNT];

// Flag to track if a request is pending
bool requestPending = false;

/**
 * @brief Function to enable RS485 transmission mode
 */
void preTransmission() {
  digitalWrite(RS485_DE_PIN, HIGH); // Enable driver, disable receiver
  delayMicroseconds(50);            // Small delay to allow line to stabilize
}

/**
 * @brief Function to enable RS485 reception mode
 */
void postTransmission() {
  delayMicroseconds(50);           // Small delay before releasing the line
  digitalWrite(RS485_DE_PIN, LOW); // Disable driver, enable receiver
}

/**
 * @brief Callback function for Modbus responses
 * 
 * @param valid True if a valid response was received, false on timeout/error
 * @param data Pointer to the data buffer with the response
 */
void modbusCallback(bool valid, uint16_t* data) {
  if (valid) {
    Serial.println("Received valid Modbus response:");
    for (int i = 0; i < REGISTER_COUNT; i++) {
      Serial.print("Register ");
      Serial.print(REGISTER_ADDRESS + i);
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
  
  // Set up RS485 control pin
  pinMode(RS485_DE_PIN, OUTPUT);
  
  // Initially set to receive mode
  digitalWrite(RS485_DE_PIN, LOW);
  
  // Initialize the Modbus master with Serial1
  modbus.begin(&Serial1);
  
  // Set the RS485 transmission callbacks
  modbus.setTransmissionCallbacks(preTransmission, postTransmission);
  
  // Set timeout to 500ms (default is 1000ms)
  modbus.setTimeout(500);
  
  // Set LED pin as output
  pinMode(LED_PIN, OUTPUT);
  
  Serial.println("Modbus RTU Master with RS485 Extension Example");
  Serial.println("---------------------------------------------");
  Serial.println("Reading holding registers every 3 seconds");
}

void loop() {
  // Call manage() regularly to process the Modbus queue
  // The RS485 direction control is now handled automatically by the callbacks
  modbus.manage();
  
  // Toggle LED every 200ms to demonstrate non-blocking operation
  if (millis() - lastLedToggle >= 200) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
    lastLedToggle = millis();
  }
  
  // Send a new Modbus request if no request is pending and the interval has elapsed
  if (!requestPending && (millis() - lastModbusRequest >= 3000)) {
    Serial.print("Reading ");
    Serial.print(REGISTER_COUNT);
    Serial.print(" registers starting at address ");
    Serial.println(REGISTER_ADDRESS);
    
    // The RS485 direction will be controlled automatically by the callbacks
    if (modbus.readHoldingRegisters(SLAVE_ID, REGISTER_ADDRESS, registers, REGISTER_COUNT, modbusCallback)) {
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
 * Implementation notes:
 * 
 * 1. This example uses the ModbusRTUMaster_RS485 extension which inherits from 
 *    the base ModbusRTUMaster class and adds support for RS485 direction control.
 * 
 * 2. The callbacks preTransmission() and postTransmission() are automatically called
 *    at the appropriate times to control the DE/RE pins of the RS485 transceiver.
 * 
 * 3. Some RS485 transceivers have separate DE and RE pins, while others have them
 *    combined. This example assumes they're on the same pin, but you can modify the
 *    callbacks to control them separately if needed.
 *
 * 4. The small delays in the callbacks (delayMicroseconds) help ensure line stability
 *    during direction changes. You may need to adjust these based on your specific
 *    hardware and baud rate.
 */
