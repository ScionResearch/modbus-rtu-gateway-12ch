#include "modbus-rtu-master.h"

/**
 * @brief Constructor
 */
ModbusRTUMaster::ModbusRTUMaster() {
    _serial = nullptr;
    _queueCount = 0;
    _currentRequest = 0;
    _timeout = MODBUS_DEFAULT_TIMEOUT;
    _lastActivity = 0;
    _interframeDelay = MODBUS_DEFAULT_INTERFRAME_DELAY;
    _bufferLength = 0;
    _state = IDLE;
    _dePin = -1; // Default to no DE pin
    
    // Initialize queue to inactive state
    for (uint8_t i = 0; i < MODBUS_QUEUE_SIZE; i++) {
        _queue[i].active = false;
    }
}

/**
 * @brief Initialize the Modbus master
 */
bool ModbusRTUMaster::begin(HardwareSerial* serial, uint32_t baudrate, uint32_t config, int8_t dePin) {
    if (serial == nullptr) {
        return false;
    }
    
    _serial = serial;
    
    // Initialize the serial port directly
    _serial->begin(baudrate, config);
    
    _interframeDelay = _calculateInterframeDelay(baudrate);
    
    // Initialize DE pin if provided
    _dePin = dePin;
    if (_dePin >= 0) {
        pinMode(_dePin, OUTPUT);
        digitalWrite(_dePin, LOW); // Default to receive mode
    }
    
    return true;
}

/**
 * @brief Set the serial configuration during runtime
 */
void ModbusRTUMaster::setSerialConfig(uint32_t baudrate, uint32_t config) {
    if (_serial != nullptr) {
        _serial->begin(baudrate, config);
        _interframeDelay = _calculateInterframeDelay(baudrate);
    }
}

/**
 * @brief Set the response timeout
 */
void ModbusRTUMaster::setTimeout(uint16_t timeout) {
    _timeout = timeout;
}

/**
 * @brief Process the command queue
 */
void ModbusRTUMaster::manage() {
    if (_serial == nullptr) {
        return;
    }
    
    // If in waiting mode, ensure DE pin is LOW (receive mode)
    if (_state == WAITING_FOR_REPLY && _dePin >= 0) {
        digitalWrite(_dePin, LOW);
    }
    
    // Process any received data
    while (_serial->available() > 0) {
        if (_bufferLength < MODBUS_MAX_BUFFER) {
            _buffer[_bufferLength++] = _serial->read();
            _lastActivity = millis();
        } else {
            // Buffer overflow, discard the byte
            _serial->read();
        }
    }
    
    // Check the current state
    switch (_state) {
        case IDLE:
            // If there are requests in the queue, send the next one
            {
                ModbusRequest* request = _getNextRequest();
                if (request != nullptr) {
                    _bufferLength = 0; // Clear the buffer before sending
                    if (_sendRequest(request)) {
                        _state = WAITING_FOR_REPLY;
                        _lastActivity = millis();
                    }
                }
            }
            break;
            
        case WAITING_FOR_REPLY:
            // Check if we've received a complete message or timed out
            if (_bufferLength > 0) {
                // We need at least 5 bytes for a minimal valid Modbus RTU response
                // (slave id, function code, at least 1 data byte, and 2 CRC bytes)
                if (_bufferLength >= 5) {
                    // Check if we've received the expected response length
                    //uint8_t slaveId = _buffer[0];
                    uint8_t functionCode = _buffer[1];
                    
                    // Determine the expected message length based on the function code
                    uint16_t expectedLength = 0;
                    
                    // Is this an exception response?
                    if (functionCode & 0x80) {
                        expectedLength = 5; // ID, FC, Exception code, 2 CRC bytes
                    } else {
                        switch (functionCode) {
                            case MODBUS_FC_READ_COILS:
                            case MODBUS_FC_READ_DISCRETE_INPUTS:
                            case MODBUS_FC_READ_HOLDING_REGISTERS:
                            case MODBUS_FC_READ_INPUT_REGISTERS:
                                // For read functions, the second byte gives us the data length
                                if (_bufferLength >= 3) {
                                    expectedLength = 5 + _buffer[2]; // ID, FC, len, data, 2 CRC bytes
                                }
                                break;
                                
                            case MODBUS_FC_WRITE_SINGLE_COIL:
                            case MODBUS_FC_WRITE_SINGLE_REGISTER:
                                expectedLength = 8; // ID, FC, addr (2), value (2), 2 CRC bytes
                                break;
                                
                            case MODBUS_FC_WRITE_MULTIPLE_COILS:
                            case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
                                expectedLength = 8; // ID, FC, addr (2), quantity (2), 2 CRC bytes
                                break;
                                
                            default:
                                // Unknown function code, wait for timeout
                                break;
                        }
                    }
                    
                    // If we know the expected length and have received enough bytes, process the message
                    if (expectedLength > 0 && _bufferLength >= expectedLength) {
                        // Check CRC
                        uint16_t receivedCrc = (_buffer[expectedLength - 1] << 8) | _buffer[expectedLength - 2];
                        uint16_t calculatedCrc = _calculateCRC(_buffer, expectedLength - 2);
                        
                        if (receivedCrc == calculatedCrc) {
                            // Valid CRC, process the response
                            _state = PROCESSING_REPLY;
                            
                            // Process based on function code
                            uint8_t dataOffset = 2; // Default data offset (after ID and FC)
                            uint16_t dataLength = expectedLength - 4; // Excluding ID, FC, and CRC (2 bytes)
                            
                            if (functionCode & 0x80) {
                                // Exception response, treat as invalid
                                if (_queue[_currentRequest].callback) {
                                    _queue[_currentRequest].callback(false, _queue[_currentRequest].data, _queue[_currentRequest].requestId);
                                }
                            } else {
                                switch (functionCode) {
                                    case MODBUS_FC_READ_COILS:
                                    case MODBUS_FC_READ_DISCRETE_INPUTS:
                                    case MODBUS_FC_READ_HOLDING_REGISTERS:
                                    case MODBUS_FC_READ_INPUT_REGISTERS:
                                        // Data length is in the byte after function code
                                        dataOffset = 3;
                                        dataLength = _buffer[2];
                                        
                                        // For read functions, we need to fill the data buffer with received values
                                        if (functionCode == MODBUS_FC_READ_HOLDING_REGISTERS ||
                                            functionCode == MODBUS_FC_READ_INPUT_REGISTERS) {
                                            // For register reads, convert byte array to uint16_t array
                                            for (uint16_t i = 0; i < (dataLength / 2); i++) {
                                                if (_queue[_currentRequest].data != nullptr) {
                                                    _queue[_currentRequest].data[i] = (_buffer[dataOffset + i*2] << 8) | 
                                                                                     _buffer[dataOffset + i*2 + 1];
                                                }
                                            }
                                        } else {
                                            // For coil/discrete input reads, convert byte array to bit packed uint16_t array
                                            for (uint16_t i = 0; i < (dataLength * 8); i++) {
                                                if (i < _queue[_currentRequest].length && _queue[_currentRequest].data != nullptr) {
                                                    uint16_t byteIndex = i / 8;
                                                    uint8_t bitIndex = i % 8;
                                                    
                                                    if (i / 16 < _queue[_currentRequest].length) {
                                                        if (_buffer[dataOffset + byteIndex] & (1 << bitIndex)) {
                                                            _queue[_currentRequest].data[i / 16] |= (1 << (i % 16));
                                                        } else {
                                                            _queue[_currentRequest].data[i / 16] &= ~(1 << (i % 16));
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        break;
                                        
                                    case MODBUS_FC_WRITE_SINGLE_COIL:
                                    case MODBUS_FC_WRITE_SINGLE_REGISTER:
                                    case MODBUS_FC_WRITE_MULTIPLE_COILS:
                                    case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
                                        // For write functions, we don't need to modify the data buffer
                                        break;
                                        
                                    default:
                                        // Unknown function code, treat as invalid
                                        if (_queue[_currentRequest].callback) {
                                            _queue[_currentRequest].callback(false, _queue[_currentRequest].data, _queue[_currentRequest].requestId);
                                        }
                                        break;
                                }
                                
                                // Call the callback with the result
                                if (_queue[_currentRequest].callback) {
                                    _queue[_currentRequest].callback(true, _queue[_currentRequest].data, _queue[_currentRequest].requestId);
                                }
                            }
                            
                            // Mark the request as processed
                            _queue[_currentRequest].active = false;
                            _queueCount--;
                            
                            // Ensure inter-frame delay before allowing next request
                            delayMicroseconds(_interframeDelay);
                            
                            // Reset the state
                            _state = IDLE;
                            _bufferLength = 0;
                        }
                    }
                }
            }
            
            // Check for timeout
            if (_state == WAITING_FOR_REPLY && (millis() - _lastActivity) > _timeout) {
                // Timeout occurred, call the callback with invalid result
                if (_queue[_currentRequest].callback) {
                    _queue[_currentRequest].callback(false, _queue[_currentRequest].data, _queue[_currentRequest].requestId);
                }
                
                // Mark the request as processed
                _queue[_currentRequest].active = false;
                _queueCount--;
                
                // Ensure inter-frame delay before allowing next request
                delayMicroseconds(_interframeDelay);
                
                // Reset the state
                _state = IDLE;
                _bufferLength = 0;
            }
            break;
            
        case PROCESSING_REPLY:
            // This state is handled within the WAITING_FOR_REPLY case when a complete message is received
            _state = IDLE;
            break;
    }
}

/**
 * @brief Push a request to the queue
 */
bool ModbusRTUMaster::pushRequest(uint8_t slaveId, uint8_t functionCode, uint16_t address, 
                                 uint16_t* data, uint16_t length, ModbusResponseCallback callback, uint32_t requestId) {
    // Check if the queue is full
    if (_queueCount >= MODBUS_QUEUE_SIZE) {
        return false;
    }
    
    // Find an empty slot in the queue
    for (uint8_t i = 0; i < MODBUS_QUEUE_SIZE; i++) {
        if (!_queue[i].active) {
            // Fill the request
            _queue[i].slaveId = slaveId;
            _queue[i].functionCode = functionCode;
            _queue[i].address = address;
            _queue[i].data = data;
            _queue[i].length = length;
            _queue[i].callback = callback;
            _queue[i].requestId = requestId;
            _queue[i].timestamp = millis();
            _queue[i].active = true;
            
            _queueCount++;
            return true;
        }
    }
    
    // This should never be reached if _queueCount is correct
    return false;
}

/**
 * @brief Read holding registers
 */
bool ModbusRTUMaster::readHoldingRegisters(uint8_t slaveId, uint16_t address, uint16_t* data, 
                                          uint16_t length, ModbusResponseCallback callback, uint32_t requestId) {
    return pushRequest(slaveId, MODBUS_FC_READ_HOLDING_REGISTERS, address, data, length, callback, requestId);
}

/**
 * @brief Read input registers
 */
bool ModbusRTUMaster::readInputRegisters(uint8_t slaveId, uint16_t address, uint16_t* data, 
                                        uint16_t length, ModbusResponseCallback callback, uint32_t requestId) {
    return pushRequest(slaveId, MODBUS_FC_READ_INPUT_REGISTERS, address, data, length, callback, requestId);
}

/**
 * @brief Read coils
 */
bool ModbusRTUMaster::readCoils(uint8_t slaveId, uint16_t address, uint16_t* data, 
                               uint16_t length, ModbusResponseCallback callback) {
    return pushRequest(slaveId, MODBUS_FC_READ_COILS, address, data, length, callback);
}

/**
 * @brief Read discrete inputs
 */
bool ModbusRTUMaster::readDiscreteInputs(uint8_t slaveId, uint16_t address, uint16_t* data, 
                                        uint16_t length, ModbusResponseCallback callback) {
    return pushRequest(slaveId, MODBUS_FC_READ_DISCRETE_INPUTS, address, data, length, callback);
}

/**
 * @brief Write single register
 */
bool ModbusRTUMaster::writeSingleRegister(uint8_t slaveId, uint16_t address, uint16_t value, 
                                         ModbusResponseCallback callback) {
    uint16_t* data = new uint16_t[1];
    data[0] = value;
    return pushRequest(slaveId, MODBUS_FC_WRITE_SINGLE_REGISTER, address, data, 1, callback);
}

/**
 * @brief Write multiple registers
 */
bool ModbusRTUMaster::writeMultipleRegisters(uint8_t slaveId, uint16_t address, uint16_t* data, 
                                            uint16_t length, ModbusResponseCallback callback) {
    return pushRequest(slaveId, MODBUS_FC_WRITE_MULTIPLE_REGISTERS, address, data, length, callback);
}

/**
 * @brief Write single coil
 */
bool ModbusRTUMaster::writeSingleCoil(uint8_t slaveId, uint16_t address, bool value, 
                                     ModbusResponseCallback callback) {
    uint16_t* data = new uint16_t[1];
    data[0] = value ? 0xFF00 : 0x0000; // In Modbus, coil ON=0xFF00, OFF=0x0000
    return pushRequest(slaveId, MODBUS_FC_WRITE_SINGLE_COIL, address, data, 1, callback);
}

/**
 * @brief Write multiple coils
 */
bool ModbusRTUMaster::writeMultipleCoils(uint8_t slaveId, uint16_t address, uint16_t* data, 
                                        uint16_t length, ModbusResponseCallback callback) {
    return pushRequest(slaveId, MODBUS_FC_WRITE_MULTIPLE_COILS, address, data, length, callback);
}

/**
 * @brief Get the number of items in the queue
 */
uint8_t ModbusRTUMaster::getQueueCount() {
    return _queueCount;
}

/**
 * @brief Clear all requests in the queue
 */
void ModbusRTUMaster::clearQueue() {
    for (uint8_t i = 0; i < MODBUS_QUEUE_SIZE; i++) {
        _queue[i].active = false;
    }
    _queueCount = 0;
    _state = IDLE;
}

/**
 * @brief Calculate the Modbus RTU CRC
 */
uint16_t ModbusRTUMaster::_calculateCRC(uint8_t* buffer, uint16_t length) {
    uint16_t crc = 0xFFFF;
    
    for (uint16_t i = 0; i < length; i++) {
        crc ^= buffer[i];
        
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc;
}

/**
 * @brief Send a request
 */
bool ModbusRTUMaster::_sendRequest(ModbusRequest* request) {
    if (_serial == nullptr || request == nullptr) {
        return false;
    }
    
    // Set DE pin HIGH for transmission if it's defined
    if (_dePin >= 0) {
        digitalWrite(_dePin, HIGH);
    }
    
    // Create the Modbus RTU message
    uint8_t messageBuffer[MODBUS_MAX_BUFFER];
    uint16_t messageLength = 0;
    
    // Add the slave ID and function code
    messageBuffer[messageLength++] = request->slaveId;
    messageBuffer[messageLength++] = request->functionCode;
    
    // Add the address (high byte first)
    messageBuffer[messageLength++] = (request->address >> 8) & 0xFF;
    messageBuffer[messageLength++] = request->address & 0xFF;
    
    // Add function-specific data
    switch (request->functionCode) {
        case MODBUS_FC_READ_COILS:
        case MODBUS_FC_READ_DISCRETE_INPUTS:
        case MODBUS_FC_READ_HOLDING_REGISTERS:
        case MODBUS_FC_READ_INPUT_REGISTERS:
            // For read functions, add the quantity (high byte first)
            messageBuffer[messageLength++] = (request->length >> 8) & 0xFF;
            messageBuffer[messageLength++] = request->length & 0xFF;
            break;
            
        case MODBUS_FC_WRITE_SINGLE_COIL:
        case MODBUS_FC_WRITE_SINGLE_REGISTER:
            // For single write functions, add the value (high byte first)
            if (request->data != nullptr) {
                messageBuffer[messageLength++] = (request->data[0] >> 8) & 0xFF;
                messageBuffer[messageLength++] = request->data[0] & 0xFF;
            } else {
                messageBuffer[messageLength++] = 0;
                messageBuffer[messageLength++] = 0;
            }
            break;
            
        case MODBUS_FC_WRITE_MULTIPLE_COILS:
            // For multiple coils, we need to pack bits into bytes
            {
                // Calculate the number of bytes needed
                uint8_t byteCount = (request->length + 7) / 8;
                messageBuffer[messageLength++] = (request->length >> 8) & 0xFF; // Quantity of coils (high)
                messageBuffer[messageLength++] = request->length & 0xFF;       // Quantity of coils (low)
                messageBuffer[messageLength++] = byteCount;                    // Byte count
                
                // Pack the coil values into bytes
                for (uint8_t i = 0; i < byteCount; i++) {
                    uint8_t packedByte = 0;
                    
                    for (uint8_t bit = 0; bit < 8; bit++) {
                        uint16_t coilIndex = i * 8 + bit;
                        
                        if (coilIndex < request->length) {
                            uint16_t wordIndex = coilIndex / 16;
                            uint8_t bitIndex = coilIndex % 16;
                            
                            if (request->data != nullptr && (request->data[wordIndex] & (1 << bitIndex))) {
                                packedByte |= (1 << bit);
                            }
                        }
                    }
                    
                    messageBuffer[messageLength++] = packedByte;
                }
            }
            break;
            
        case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
            // For multiple registers, add the quantity, byte count, and values
            messageBuffer[messageLength++] = (request->length >> 8) & 0xFF; // Quantity of registers (high)
            messageBuffer[messageLength++] = request->length & 0xFF;       // Quantity of registers (low)
            messageBuffer[messageLength++] = request->length * 2;          // Byte count (2 bytes per register)
            
            // Add each register value (high byte first)
            for (uint16_t i = 0; i < request->length; i++) {
                if (request->data != nullptr) {
                    messageBuffer[messageLength++] = (request->data[i] >> 8) & 0xFF;
                    messageBuffer[messageLength++] = request->data[i] & 0xFF;
                } else {
                    messageBuffer[messageLength++] = 0;
                    messageBuffer[messageLength++] = 0;
                }
            }
            break;
            
        default:
            // Unknown function code
            return false;
    }
    
    // Calculate and add the CRC (low byte first)
    uint16_t crc = _calculateCRC(messageBuffer, messageLength);
    messageBuffer[messageLength++] = crc & 0xFF;         // CRC low byte
    messageBuffer[messageLength++] = (crc >> 8) & 0xFF; // CRC high byte
    
    // Ensure inter-frame delay before sending
    delayMicroseconds(_interframeDelay);
    
    // Send the message
    _serial->write(messageBuffer, messageLength);
    _serial->flush();
    
    // Switch to receive mode (DE pin LOW) after sending if DE pin is defined
    if (_dePin >= 0) {
        // Keep DE pin HIGH for a brief moment after transmission
        delayMicroseconds(50);
        digitalWrite(_dePin, LOW);
    }
    
    // Update last activity timestamp
    _lastActivity = millis();
    
    // Move to waiting state
    _state = WAITING_FOR_REPLY;
    
    return true;
}

ModbusRequest* ModbusRTUMaster::_getNextRequest() {
    // Check if there are any requests in the queue
    if (_queueCount == 0) {
        return nullptr;
    }
    
    // Find the next active request, starting from current + 1
    for (uint8_t i = 0; i < MODBUS_QUEUE_SIZE; i++) {
        uint8_t index = (_currentRequest + i + 1) % MODBUS_QUEUE_SIZE;
        
        if (_queue[index].active) {
            _currentRequest = index;
            return &_queue[index];
        }
    }
    
    // This should never be reached if _queueCount is correct
    _queueCount = 0; // Reset queue count as a failsafe
    return nullptr;
}

/**
 * @brief Calculate the inter-frame delay based on baud rate
 */
uint16_t ModbusRTUMaster::_calculateInterframeDelay(uint32_t baudrate) {
    // The Modbus RTU standard requires at least 3.5 character times between frames
    // 1 character = 11 bits (1 start + 8 data + 1 parity + 1 stop)
    // Time for 1 bit = 1000000 / baudrate microseconds
    // So time for 3.5 characters = 3.5 * 11 * 1000000 / baudrate
    
    uint32_t delay = (uint32_t)(3.5 * 11 * 1000000 / baudrate);
    
    // Ensure minimum delay (1ms)
    return (delay < 1000) ? 1000 : (uint16_t)delay;
}
