/**
 * @file ModbusRTUMaster_RS485.h
 * @brief Extension of ModbusRTUMaster with RS485 support
 * 
 * This file extends the ModbusRTUMaster class to add proper RS485 support
 * with pre/post transmission callbacks for controlling DE/RE pins.
 */

#pragma once
#include "modbus-rtu-master.h"

/**
 * @class ModbusRTUMaster_RS485
 * @brief Extension of ModbusRTUMaster with RS485 support
 * 
 * This class extends the ModbusRTUMaster to add proper handling of
 * RS485 direction control pins through pre/post transmission callbacks.
 */
class ModbusRTUMaster_RS485 : public ModbusRTUMaster {
public:
    // Define callback function type
    typedef void (*TransmissionCallback)();

    /**
     * @brief Constructor
     */
    ModbusRTUMaster_RS485() : ModbusRTUMaster(),
        _preTransmissionCB(nullptr),
        _postTransmissionCB(nullptr) {}

    /**
     * @brief Set callbacks for RS485 pin control
     * 
     * @param preTransmissionCB Callback to run before transmission (set DE/RE pins HIGH)
     * @param postTransmissionCB Callback to run after transmission (set DE/RE pins LOW)
     */
    void setTransmissionCallbacks(TransmissionCallback preTransmissionCB, 
                                TransmissionCallback postTransmissionCB) {
        _preTransmissionCB = preTransmissionCB;
        _postTransmissionCB = postTransmissionCB;
    }

    /**
     * @brief Overridden manage method that calls the RS485 callbacks
     * 
     * This method adds the pre/post transmission callbacks to the
     * standard manage method.
     */
    void manage() override {
        if (_serial == nullptr) {
            return;
        }
        
        // Process any received data
        while (_serial->available() > 0) {
            if (_bufferLength < MODBUS_MAX_BUFFER) {
                _buffer[_bufferLength++] = _serial->read();
                _lastActivity = micros();
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
                        
                        // Call pre-transmission callback if set
                        if (_preTransmissionCB) {
                            _preTransmissionCB();
                        }
                        
                        // Send the request
                        if (_sendRequest(request)) {
                            _state = WAITING_FOR_REPLY;
                            _lastActivity = micros();
                        }
                        
                        // Call post-transmission callback if set
                        if (_postTransmissionCB) {
                            _postTransmissionCB();
                        }
                    }
                }
                break;
                
            case WAITING_FOR_REPLY:
                // Process received data and check for timeouts
                // This is the same as the base class
                
                // Check if we've received a complete message
                if (_bufferLength > 0) {
                    uint8_t slaveId = _buffer[0];
                    
                    // Check if the response is for the expected slave
                    if (slaveId == _queue[_currentRequest].slaveId) {
                        // Check for Modbus exception response
                        if (_buffer[1] & 0x80) {
                            // This is an exception response
                            if (_bufferLength >= 5) { // Exception response is 5 bytes
                                // Validate CRC
                                uint16_t crc = (_buffer[3] << 8) | _buffer[2];
                                uint16_t calculatedCRC = _calculateCRC(_buffer, 3);
                                
                                if (crc == calculatedCRC) {
                                    // Call the callback with invalid result and error code
                                    if (_queue[_currentRequest].callback) {
                                        _queue[_currentRequest].callback(false, nullptr);
                                    }
                                    
                                    // Mark the request as processed
                                    _queue[_currentRequest].active = false;
                                    _queueCount--;
                                    
                                    // Reset the state
                                    _state = IDLE;
                                    _bufferLength = 0;
                                }
                            }
                        } else {
                            // Process normal response based on the function code
                            uint8_t functionCode = _buffer[1];
                            uint8_t expectedLength = 0;
                            
                            // Determine expected length based on function code
                            switch (functionCode) {
                                // Various function code processing (same as base class)
                                // ...
                                
                                case MODBUS_FC_READ_HOLDING_REGISTERS:
                                case MODBUS_FC_READ_INPUT_REGISTERS:
                                    if (_bufferLength >= 3) {
                                        uint8_t byteCount = _buffer[2];
                                        expectedLength = 5 + byteCount; // 1 slave ID + 1 function code + 1 byte count + byteCount + 2 CRC
                                        
                                        if (_bufferLength >= expectedLength) {
                                            // We have a complete message, check CRC
                                            uint16_t crc = (_buffer[expectedLength - 1] << 8) | _buffer[expectedLength - 2];
                                            uint16_t calculatedCRC = _calculateCRC(_buffer, expectedLength - 2);
                                            
                                            if (crc == calculatedCRC) {
                                                // Valid response, extract the register values
                                                uint8_t dataOffset = 3; // Start after slave ID, function code, and byte count
                                                
                                                // Each register is 2 bytes (high byte first)
                                                for (uint16_t i = 0; i < byteCount / 2; i++) {
                                                    _queue[_currentRequest].data[i] = (_buffer[dataOffset + (i * 2)] << 8) | _buffer[dataOffset + (i * 2) + 1];
                                                }
                                                
                                                // Call the callback with valid result
                                                if (_queue[_currentRequest].callback) {
                                                    _queue[_currentRequest].callback(true, _queue[_currentRequest].data);
                                                }
                                                
                                                // Mark the request as processed
                                                _queue[_currentRequest].active = false;
                                                _queueCount--;
                                                
                                                // Reset the state
                                                _state = IDLE;
                                                _bufferLength = 0;
                                            }
                                        }
                                    }
                                    break;
                                
                                case MODBUS_FC_READ_COILS:
                                case MODBUS_FC_READ_DISCRETE_INPUTS:
                                    // Similar processing as for registers
                                    // ...
                                    break;
                                
                                case MODBUS_FC_WRITE_SINGLE_COIL:
                                case MODBUS_FC_WRITE_SINGLE_REGISTER:
                                case MODBUS_FC_WRITE_MULTIPLE_COILS:
                                case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
                                    // These responses are fixed length
                                    expectedLength = 8; // 1 slave ID + 1 function code + 2 address + 2 data + 2 CRC
                                    
                                    if (_bufferLength >= expectedLength) {
                                        // We have a complete message, check CRC
                                        uint16_t crc = (_buffer[expectedLength - 1] << 8) | _buffer[expectedLength - 2];
                                        uint16_t calculatedCRC = _calculateCRC(_buffer, expectedLength - 2);
                                        
                                        if (crc == calculatedCRC) {
                                            // Valid response, call the callback with valid result
                                            if (_queue[_currentRequest].callback) {
                                                _queue[_currentRequest].callback(true, _queue[_currentRequest].data);
                                            }
                                            
                                            // Mark the request as processed
                                            _queue[_currentRequest].active = false;
                                            _queueCount--;
                                            
                                            // Reset the state
                                            _state = IDLE;
                                            _bufferLength = 0;
                                        }
                                    }
                                    break;
                            }
                        }
                    }
                }
                
                // Check for timeout
                if (_state == WAITING_FOR_REPLY && (millis() - (_lastActivity / 1000)) > _timeout) {
                    // Call pre-transmission callback if set (for consistency)
                    if (_preTransmissionCB) {
                        _preTransmissionCB();
                    }
                    
                    // Timeout occurred, call the callback with invalid result
                    if (_queue[_currentRequest].callback) {
                        _queue[_currentRequest].callback(false, nullptr);
                    }
                    
                    // Mark the request as processed
                    _queue[_currentRequest].active = false;
                    _queueCount--;
                    
                    // Reset the state
                    _state = IDLE;
                    _bufferLength = 0;
                    
                    // Call post-transmission callback if set
                    if (_postTransmissionCB) {
                        _postTransmissionCB();
                    }
                }
                break;
                
            case PROCESSING_REPLY:
                // This state is handled within the WAITING_FOR_REPLY case
                _state = IDLE;
                break;
        }
    }

private:
    TransmissionCallback _preTransmissionCB;     ///< Callback to run before transmission
    TransmissionCallback _postTransmissionCB;    ///< Callback to run after transmission

    /**
     * @brief Override the _sendRequest method to add callbacks
     * 
     * @param request Pointer to the request to send
     * @return true if request was sent successfully
     * @return false if there was an error sending the request
     */
    bool _sendRequest(ModbusRequest* request) override {
        // The base class's _sendRequest is already being called in the manage method
        // with pre/post transmission callbacks around it, so we can just call the parent method
        return ModbusRTUMaster::_sendRequest(request);
    }
};
