# Modbus RTU Master Library

A non-blocking Modbus RTU master library for Arduino-compatible microcontrollers.

## Features

- **Non-blocking operation**: No delays that would halt your main program flow
- **FIFO command queue**: Queue multiple Modbus requests that will be processed in order
- **Callback-based responses**: Each request can have its own callback function
- **Easy to use**: Simple API with helper methods for common Modbus operations
- **Comprehensive error handling**: CRC validation, timeouts, and exception responses
- **Support for all standard Modbus functions**:
  - Read Coils (0x01)
  - Read Discrete Inputs (0x02)
  - Read Holding Registers (0x03)
  - Read Input Registers (0x04)
  - Write Single Coil (0x05)
  - Write Single Register (0x06)
  - Write Multiple Coils (0x0F)
  - Write Multiple Registers (0x10)

## Installation

1. Copy the `src` folder to your PlatformIO project
2. Include the header file in your sketch: `#include "modbus-rtu-master.h"`

## Usage

### Basic Setup

```cpp
#include "modbus-rtu-master.h"

ModbusRTUMaster modbus;

void setup() {
  Serial1.begin(9600, SERIAL_8N1);  // Serial port for Modbus communication
  modbus.begin(&Serial1);           // Initialize with the serial port
}

void loop() {
  modbus.manage();                  // Must be called regularly
  // Your code here
}
```

### Reading Holding Registers

```cpp
#define SLAVE_ID 1
#define START_ADDRESS 0
#define REGISTER_COUNT 10

uint16_t registers[REGISTER_COUNT];

void responseCallback(bool valid, uint16_t* data) {
  if (valid) {
    // Process the data in the 'registers' buffer
    // Note: 'data' points to the same buffer passed to readHoldingRegisters
  } else {
    // Handle error or timeout
  }
}

void requestRegisters() {
  modbus.readHoldingRegisters(SLAVE_ID, START_ADDRESS, registers, REGISTER_COUNT, responseCallback);
}
```

### Writing Registers

```cpp
#define SLAVE_ID 1
#define REGISTER_ADDRESS 10

void writeCallback(bool valid, uint16_t* data) {
  if (valid) {
    // Write was successful
  } else {
    // Write failed
  }
}

void writeSingleValue() {
  uint16_t value = 42;
  modbus.writeSingleRegister(SLAVE_ID, REGISTER_ADDRESS, value, writeCallback);
}

void writeMultipleValues() {
  uint16_t values[] = {10, 20, 30, 40, 50};
  modbus.writeMultipleRegisters(SLAVE_ID, REGISTER_ADDRESS, values, 5, writeCallback);
}
```

### Coil Operations

```cpp
void writeCoil() {
  // Turn ON a coil
  modbus.writeSingleCoil(SLAVE_ID, COIL_ADDRESS, true, coilCallback);
  
  // Turn OFF a coil
  modbus.writeSingleCoil(SLAVE_ID, COIL_ADDRESS, false, coilCallback);
}

void readCoils() {
  uint16_t coilValues[10]; // Each uint16_t can store 16 coil values
  modbus.readCoils(SLAVE_ID, START_ADDRESS, coilValues, 160, coilCallback);
  // This reads 160 coils, packed into 10 uint16_t values
}
```

## Advanced Usage

### Setting Timeout

```cpp
// Set timeout to 500ms (default is 1000ms)
modbus.setTimeout(500);
```

### Managing the Queue

```cpp
// Get the number of pending requests
uint8_t pendingRequests = modbus.getQueueCount();

// Clear all pending requests
modbus.clearQueue();
```

### Custom Requests

For advanced use cases, you can create custom Modbus requests:

```cpp
uint16_t data[10];
modbus.pushRequest(
  slaveId,        // Target slave ID
  functionCode,   // Modbus function code
  address,        // Starting address
  data,           // Data buffer
  length,         // Length of data (in 16-bit units)
  callback        // Callback function
);
```

## Limitations

- The queue size is defined by `MODBUS_QUEUE_SIZE` (default: 10)
- Maximum buffer size for Modbus messages is defined by `MODBUS_MAX_BUFFER` (default: 256 bytes)
- Default response timeout is 1000ms, which can be changed with `setTimeout()`

## Memory Considerations

When using `writeSingleRegister()` and `writeSingleCoil()`, a small memory leak can occur if requests are queued faster than they are processed. This is because these functions allocate memory for the data buffer using `new`. In future versions, this will be optimized.

## License

This library is open-source software. Feel free to use, modify, and distribute as needed.
