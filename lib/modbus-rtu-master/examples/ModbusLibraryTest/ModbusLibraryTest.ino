/**
 * @file ModbusLibraryTest.ino
 * @brief Test suite for the ModbusRTUMaster library
 * 
 * This sketch provides a comprehensive test of the ModbusRTUMaster library
 * functionality. It allows testing different Modbus functions against a real
 * slave device or a simulator. The test results are reported via Serial.
 * 
 * Tests include:
 * - Queue management
 * - Reading and writing different data types
 * - Error handling
 * - Timeout behavior
 */

#include "modbus-rtu-master.h"

// Create Modbus master instance
ModbusRTUMaster modbus;

// Configuration
#define SLAVE_ID 1             // Modbus slave ID to test with
#define LED_PIN LED_BUILTIN    // LED for visual feedback
#define SERIAL_BAUD 9600       // Modbus serial baud rate
#define TEST_INTERVAL 3000     // Time between tests (ms)

// Test addresses (adjust these based on your slave device)
#define HOLDING_REG_ADDR 100
#define INPUT_REG_ADDR 200
#define COIL_ADDR 300
#define DISCRETE_INPUT_ADDR 400

// Data buffers
uint16_t regBuffer[10];
uint16_t coilBuffer[2];  // 32 coils (16 bits * 2)

// Test status variables
bool testInProgress = false;
unsigned long lastTestTime = 0;
unsigned long lastLedToggle = 0;
bool ledState = false;
int currentTest = 0;
int totalPassed = 0;
int totalFailed = 0;

// Forward declarations
void startNextTest();
void printTestHeader(const char* testName);
void reportTestResult(bool success, const char* message);

/**
 * @brief Callback for test results
 */
void testCallback(bool valid, uint16_t* data) {
  if (currentTest == 1) {
    // Test 1: Read holding registers
    reportTestResult(valid, "Read holding registers");
    
    if (valid) {
      Serial.println("Register values:");
      for (int i = 0; i < 5; i++) {
        Serial.print("  Reg[");
        Serial.print(HOLDING_REG_ADDR + i);
        Serial.print("]: ");
        Serial.println(regBuffer[i]);
      }
    }
  } 
  else if (currentTest == 2) {
    // Test 2: Read input registers
    reportTestResult(valid, "Read input registers");
    
    if (valid) {
      Serial.println("Input register values:");
      for (int i = 0; i < 5; i++) {
        Serial.print("  Reg[");
        Serial.print(INPUT_REG_ADDR + i);
        Serial.print("]: ");
        Serial.println(regBuffer[i]);
      }
    }
  }
  else if (currentTest == 3) {
    // Test 3: Read coils
    reportTestResult(valid, "Read coils");
    
    if (valid) {
      Serial.println("Coil values (first 16):");
      for (int i = 0; i < 16; i++) {
        Serial.print("  Coil[");
        Serial.print(COIL_ADDR + i);
        Serial.print("]: ");
        Serial.println((coilBuffer[0] & (1 << i)) ? "ON" : "OFF");
      }
    }
  }
  else if (currentTest == 4) {
    // Test 4: Read discrete inputs
    reportTestResult(valid, "Read discrete inputs");
    
    if (valid) {
      Serial.println("Discrete input values (first 16):");
      for (int i = 0; i < 16; i++) {
        Serial.print("  Input[");
        Serial.print(DISCRETE_INPUT_ADDR + i);
        Serial.print("]: ");
        Serial.println((coilBuffer[0] & (1 << i)) ? "ON" : "OFF");
      }
    }
  }
  else if (currentTest == 5) {
    // Test 5: Write single register
    reportTestResult(valid, "Write single register");
  }
  else if (currentTest == 6) {
    // Test 6: Write multiple registers
    reportTestResult(valid, "Write multiple registers");
  }
  else if (currentTest == 7) {
    // Test 7: Write single coil
    reportTestResult(valid, "Write single coil");
  }
  else if (currentTest == 8) {
    // Test 8: Write multiple coils
    reportTestResult(valid, "Write multiple coils");
  }
  else if (currentTest == 9) {
    // Test 9: Invalid slave ID (expect failure)
    reportTestResult(!valid, "Invalid slave ID test");
  }
  else if (currentTest == 10) {
    // Test 10: Queue management
    reportTestResult(valid, "Queue management");
  }
  
  testInProgress = false;
}

void setup() {
  // Initialize serial ports
  Serial.begin(115200);  // Debug serial port
  Serial1.begin(SERIAL_BAUD, SERIAL_8N1);  // Modbus serial port
  
  // Initialize the Modbus master with Serial1
  modbus.begin(&Serial1);
  
  // Set timeout to 500ms for faster testing
  modbus.setTimeout(500);
  
  // Set LED pin as output
  pinMode(LED_PIN, OUTPUT);
  
  // Initialize test data
  for (int i = 0; i < 10; i++) {
    regBuffer[i] = 1000 + i;
  }
  
  coilBuffer[0] = 0xAAAA; // 10101010 pattern
  coilBuffer[1] = 0x5555; // 01010101 pattern
  
  Serial.println();
  Serial.println("======================================");
  Serial.println(" ModbusRTUMaster Library Test Suite  ");
  Serial.println("======================================");
  Serial.println();
  Serial.println("Connect this device to a Modbus slave with ID 1");
  Serial.println("Tests will start in 5 seconds...");
  Serial.println();
  
  delay(5000); // Wait before starting tests
  
  currentTest = 0;
  totalPassed = 0;
  totalFailed = 0;
}

void loop() {
  // Call manage() regularly to process the Modbus queue
  modbus.manage();
  
  // Toggle LED every 100ms during testing
  if (millis() - lastLedToggle >= 100) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
    lastLedToggle = millis();
  }
  
  // Start next test if the current one is done and the interval has elapsed
  if (!testInProgress && (millis() - lastTestTime >= TEST_INTERVAL)) {
    startNextTest();
  }
}

/**
 * @brief Start the next test in the sequence
 */
void startNextTest() {
  currentTest++;
  
  switch (currentTest) {
    case 1:
      // Test 1: Read holding registers
      printTestHeader("Reading Holding Registers");
      testInProgress = modbus.readHoldingRegisters(SLAVE_ID, HOLDING_REG_ADDR, regBuffer, 5, testCallback);
      break;
      
    case 2:
      // Test 2: Read input registers
      printTestHeader("Reading Input Registers");
      testInProgress = modbus.readInputRegisters(SLAVE_ID, INPUT_REG_ADDR, regBuffer, 5, testCallback);
      break;
      
    case 3:
      // Test 3: Read coils
      printTestHeader("Reading Coils");
      testInProgress = modbus.readCoils(SLAVE_ID, COIL_ADDR, coilBuffer, 32, testCallback);
      break;
      
    case 4:
      // Test 4: Read discrete inputs
      printTestHeader("Reading Discrete Inputs");
      testInProgress = modbus.readDiscreteInputs(SLAVE_ID, DISCRETE_INPUT_ADDR, coilBuffer, 32, testCallback);
      break;
      
    case 5:
      // Test 5: Write single register
      printTestHeader("Writing Single Register");
      testInProgress = modbus.writeSingleRegister(SLAVE_ID, HOLDING_REG_ADDR, 42, testCallback);
      break;
      
    case 6:
      // Test 6: Write multiple registers
      printTestHeader("Writing Multiple Registers");
      testInProgress = modbus.writeMultipleRegisters(SLAVE_ID, HOLDING_REG_ADDR, regBuffer, 5, testCallback);
      break;
      
    case 7:
      // Test 7: Write single coil
      printTestHeader("Writing Single Coil");
      testInProgress = modbus.writeSingleCoil(SLAVE_ID, COIL_ADDR, true, testCallback);
      break;
      
    case 8:
      // Test 8: Write multiple coils
      printTestHeader("Writing Multiple Coils");
      testInProgress = modbus.writeMultipleCoils(SLAVE_ID, COIL_ADDR, coilBuffer, 32, testCallback);
      break;
      
    case 9:
      // Test 9: Invalid slave ID (expect failure)
      printTestHeader("Testing Invalid Slave ID (Expected Failure)");
      testInProgress = modbus.readHoldingRegisters(255, HOLDING_REG_ADDR, regBuffer, 5, testCallback);
      break;
      
    case 10:
      // Test 10: Queue management
      printTestHeader("Testing Queue Management");
      
      // Clear the queue first
      modbus.clearQueue();
      
      // Fill the queue
      bool queueFull = false;
      int queueCount = 0;
      
      while (!queueFull && queueCount < 20) {
        if (modbus.readHoldingRegisters(SLAVE_ID, HOLDING_REG_ADDR + queueCount, regBuffer, 1, testCallback)) {
          queueCount++;
        } else {
          queueFull = true;
        }
      }
      
      Serial.print("Queue filled with ");
      Serial.print(queueCount);
      Serial.println(" requests");
      
      // Check queue count
      uint8_t reportedCount = modbus.getQueueCount();
      
      Serial.print("Reported queue count: ");
      Serial.println(reportedCount);
      
      // We'll consider the test passed if we were able to add requests
      // and the reported count matches what we added
      testInProgress = true;
      testCallback(queueCount > 0 && reportedCount == queueCount, nullptr);
      
      // Clear the queue
      modbus.clearQueue();
      break;
      
    default:
      // All tests completed
      Serial.println();
      Serial.println("======================================");
      Serial.println(" Test Summary                        ");
      Serial.println("======================================");
      Serial.print("Total tests: ");
      Serial.println(currentTest - 1);
      Serial.print("Passed: ");
      Serial.println(totalPassed);
      Serial.print("Failed: ");
      Serial.println(totalFailed);
      Serial.println("======================================");
      Serial.println();
      
      // Reset the test counter so it will start over
      currentTest = 0;
      totalPassed = 0;
      totalFailed = 0;
      break;
  }
  
  if (testInProgress) {
    lastTestTime = millis();
  }
}

/**
 * @brief Print the header for a test
 * 
 * @param testName Name of the test
 */
void printTestHeader(const char* testName) {
  Serial.println();
  Serial.print("TEST ");
  Serial.print(currentTest);
  Serial.print(": ");
  Serial.println(testName);
  Serial.println("--------------------------------------");
}

/**
 * @brief Report the result of a test
 * 
 * @param success Whether the test passed
 * @param message Description of the test
 */
void reportTestResult(bool success, const char* message) {
  if (success) {
    Serial.print("[PASS] ");
    totalPassed++;
  } else {
    Serial.print("[FAIL] ");
    totalFailed++;
  }
  
  Serial.println(message);
}
