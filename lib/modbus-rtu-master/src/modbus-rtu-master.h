#pragma once

/**
 * @file modbus-rtu-master.h
 * @brief Non-blocking Modbus RTU Master Library for Arduino Framework
 * 
 * This library implements a non-blocking Modbus RTU master that can be used 
 * with Arduino-compatible microcontrollers. It provides an easy-to-use interface 
 * for sending Modbus requests to slave devices and handling responses asynchronously.
 */

#ifndef MODBUS_RTU_MASTER_H
#define MODBUS_RTU_MASTER_H

#include <Arduino.h>

// Maximum queue size
#define MODBUS_QUEUE_SIZE 10

// Modbus function codes
#define MODBUS_FC_READ_COILS              0x01
#define MODBUS_FC_READ_DISCRETE_INPUTS    0x02
#define MODBUS_FC_READ_HOLDING_REGISTERS  0x03
#define MODBUS_FC_READ_INPUT_REGISTERS    0x04
#define MODBUS_FC_WRITE_SINGLE_COIL       0x05
#define MODBUS_FC_WRITE_SINGLE_REGISTER   0x06
#define MODBUS_FC_WRITE_MULTIPLE_COILS    0x0F
#define MODBUS_FC_WRITE_MULTIPLE_REGISTERS 0x10

// Modbus exception codes
#define MODBUS_EX_ILLEGAL_FUNCTION        0x01
#define MODBUS_EX_ILLEGAL_DATA_ADDRESS    0x02
#define MODBUS_EX_ILLEGAL_DATA_VALUE      0x03
#define MODBUS_EX_SLAVE_DEVICE_FAILURE    0x04
#define MODBUS_EX_ACKNOWLEDGE             0x05
#define MODBUS_EX_SLAVE_DEVICE_BUSY       0x06
#define MODBUS_EX_MEMORY_PARITY_ERROR     0x08
#define MODBUS_EX_GATEWAY_PATH_UNAVAILABLE 0x0A
#define MODBUS_EX_GATEWAY_TARGET_FAILED   0x0B

// Maximum buffer size for Modbus messages
#define MODBUS_MAX_BUFFER 256

// Response timeout in milliseconds
#define MODBUS_DEFAULT_TIMEOUT 1000
// Inter-frame silent interval (3.5 character times) - calculated at runtime
// based on baud rate, but default to 3.5ms (3500µs) for standard baud rates
#define MODBUS_DEFAULT_INTERFRAME_DELAY 3500

/**
 * @brief Callback function type for Modbus responses
 * 
 * @param valid Boolean indicating if a valid response was received
 * @param data Pointer to buffer containing response data (if any)
 * @param requestId User-defined ID to match response to request
 */
typedef void (*ModbusResponseCallback)(bool valid, uint16_t* data, uint32_t requestId);

/**
 * @brief Structure for a Modbus request in the queue
 */
typedef struct {
    uint8_t slaveId;              ///< Target slave ID (1-247)
    uint8_t functionCode;         ///< Modbus function code
    uint16_t address;             ///< Starting address (0-65535)
    uint16_t* data;               ///< Data buffer (for reading/writing)
    uint16_t length;              ///< Length of data in 16-bit units
    ModbusResponseCallback callback; ///< Callback function for response
    uint32_t requestId;           ///< User-defined ID to match response to request
    uint32_t timestamp;           ///< Timestamp when request was queued
    bool active;                  ///< Flag to indicate if this entry is active
} ModbusRequest;

/**
 * @brief Modbus RTU Master Class
 */
class ModbusRTUMaster {
public:
    /**
     * @brief Constructor
     */
    ModbusRTUMaster();
    /**
     * @brief Initialize the Modbus master
     * 
     * @param serial Pointer to the Serial port to use
     * @param baudrate Baud rate for serial communication (default: 9600)
     * @param config Serial configuration (default: SERIAL_8N1)
     * @param dePin Arduino pin number connected to DE/RE of RS485 transceiver (default: -1, disabled)
     * @return true if initialization was successful
     */
    bool begin(HardwareSerial* serial, uint32_t baudrate = 9600, uint32_t config = SERIAL_8N1, int8_t dePin = -1);
    
    /**
     * @brief Set the serial configuration
     * 
     * @param baudrate Baud rate for serial communication
     * @param config Serial configuration
     */
    void setSerialConfig(uint32_t baudrate, uint32_t config);
    
    /**
     * @brief Set the response timeout
     * 
     * @param timeout Timeout in milliseconds
     */
    void setTimeout(uint16_t timeout);
    
    /**
     * @brief Process the command queue (must be called regularly)
     * 
     * This function handles sending pending requests and processing responses.
     * It should be called frequently, typically in the main loop.
     */
    void manage();
    
    /**
     * @brief Push a request to the queue
     * 
     * @param slaveId Target slave ID
     * @param functionCode Modbus function code
     * @param address Starting address
     * @param data Data buffer (for reading/writing)
     * @param length Length of data in 16-bit units
     * @param callback Callback function for response
     * @param requestId User-defined ID to match response (default: 0)
     * @return true if request was successfully queued
     */
    bool pushRequest(uint8_t slaveId, uint8_t functionCode, uint16_t address, 
                   uint16_t* data, uint16_t length, ModbusResponseCallback callback, uint32_t requestId = 0);
    
    /**
     * @brief Read holding registers (function code 0x03)
     * 
     * @param slaveId Target slave ID
     * @param address Starting address
     * @param data Buffer to store the read values
     * @param length Number of registers to read
     * @param callback Callback function for response
     * @param requestId User-defined ID to match response (default: 0)
     * @return true if request was successfully queued
     */
    bool readHoldingRegisters(uint8_t slaveId, uint16_t address, uint16_t* data, 
                            uint16_t length, ModbusResponseCallback callback, uint32_t requestId = 0);
    
    /**
     * @brief Read input registers (function code 0x04)
     * 
     * @param slaveId Target slave ID
     * @param address Starting address
     * @param data Buffer to store the read values
     * @param length Number of registers to read
     * @param callback Callback function for response
     * @param requestId User-defined ID to match response (default: 0)
     * @return true if request was successfully queued
     */
    bool readInputRegisters(uint8_t slaveId, uint16_t address, uint16_t* data, 
                          uint16_t length, ModbusResponseCallback callback, uint32_t requestId = 0);
    
    /**
     * @brief Read coils (function code 0x01)
     * 
     * @param slaveId Target slave ID
     * @param address Starting address
     * @param data Buffer to store the read values (1 bit per coil, packed)
     * @param length Number of coils to read
     * @param callback Callback function for response
     * @return true if request was successfully queued
     */
    bool readCoils(uint8_t slaveId, uint16_t address, uint16_t* data, 
                 uint16_t length, ModbusResponseCallback callback);
    
    /**
     * @brief Read discrete inputs (function code 0x02)
     * 
     * @param slaveId Target slave ID
     * @param address Starting address
     * @param data Buffer to store the read values (1 bit per input, packed)
     * @param length Number of inputs to read
     * @param callback Callback function for response
     * @return true if request was successfully queued
     */
    bool readDiscreteInputs(uint8_t slaveId, uint16_t address, uint16_t* data, 
                          uint16_t length, ModbusResponseCallback callback);
    
    /**
     * @brief Write single register (function code 0x06)
     * 
     * @param slaveId Target slave ID
     * @param address Register address
     * @param value Value to write
     * @param callback Callback function for response
     * @return true if request was successfully queued
     */
    bool writeSingleRegister(uint8_t slaveId, uint16_t address, uint16_t value, 
                           ModbusResponseCallback callback);
    
    /**
     * @brief Write multiple registers (function code 0x10)
     * 
     * @param slaveId Target slave ID
     * @param address Starting address
     * @param data Values to write
     * @param length Number of registers to write
     * @param callback Callback function for response
     * @return true if request was successfully queued
     */
    bool writeMultipleRegisters(uint8_t slaveId, uint16_t address, uint16_t* data, 
                              uint16_t length, ModbusResponseCallback callback);
    
    /**
     * @brief Write single coil (function code 0x05)
     * 
     * @param slaveId Target slave ID
     * @param address Coil address
     * @param value Coil value (true = ON, false = OFF)
     * @param callback Callback function for response
     * @return true if request was successfully queued
     */
    bool writeSingleCoil(uint8_t slaveId, uint16_t address, bool value, 
                       ModbusResponseCallback callback);
    
    /**
     * @brief Write multiple coils (function code 0x0F)
     * 
     * @param slaveId Target slave ID
     * @param address Starting address
     * @param data Coil values (1 bit per coil, packed)
     * @param length Number of coils to write
     * @param callback Callback function for response
     * @return true if request was successfully queued
     */
    bool writeMultipleCoils(uint8_t slaveId, uint16_t address, uint16_t* data, 
                          uint16_t length, ModbusResponseCallback callback);
    
    /**
     * @brief Get the number of items in the queue
     * 
     * @return Number of active requests in the queue
     */
    uint8_t getQueueCount();
    
    /**
     * @brief Clear all requests in the queue
     */
    void clearQueue();

private:
    HardwareSerial* _serial;           ///< Serial port for communication
    ModbusRequest _queue[MODBUS_QUEUE_SIZE]; ///< Request queue
    uint8_t _queueCount;               ///< Number of active items in the queue
    uint8_t _currentRequest;           ///< Index of the current request
    uint16_t _timeout;                 ///< Response timeout in milliseconds
    uint32_t _lastActivity;            ///< Timestamp of last activity
    uint16_t _interframeDelay;         ///< Delay between frames in microseconds
    uint8_t _buffer[MODBUS_MAX_BUFFER]; ///< Buffer for message processing
    uint16_t _bufferLength;            ///< Current length of data in the buffer
    int8_t _dePin;                     ///< DE/RE pin for RS485 control (-1 if not used)
    enum {
        IDLE,                         ///< No active transaction
        WAITING_FOR_REPLY,            ///< Waiting for a response
        PROCESSING_REPLY              ///< Processing a response
    } _state;                          ///< Current state of the master
    
    /**
     * @brief Calculate the Modbus RTU CRC
     * 
     * @param buffer Data buffer
     * @param length Length of data
     * @return Calculated CRC
     */
    uint16_t _calculateCRC(uint8_t* buffer, uint16_t length);
    
    /**
     * @brief Send a request
     * 
     * @param request The request to send
     * @return true if the request was sent successfully
     */
    bool _sendRequest(ModbusRequest* request);
    
    /**
     * @brief Process a response
     * 
     * @param slaveId The slave ID that responded
     * @param functionCode The function code in the response
     * @param data The response data
     * @param length The length of the response data
     */
    void _processResponse(uint8_t slaveId, uint8_t functionCode, uint8_t* data, uint16_t length);
    
    /**
     * @brief Get the next request from the queue
     * 
     * @return Pointer to the next request, or NULL if the queue is empty
     */
    ModbusRequest* _getNextRequest();
    
    /**
     * @brief Calculate the inter-frame delay based on baud rate
     * 
     * @param baudrate The baud rate
     * @return The inter-frame delay in microseconds
     */
    uint16_t _calculateInterframeDelay(uint32_t baudrate);
};

#endif // MODBUS_RTU_MASTER_H
