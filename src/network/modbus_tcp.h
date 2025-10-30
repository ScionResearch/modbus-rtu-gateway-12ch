#pragma once

#include "../sys_init.h"
#include <WiFiServer.h>
#include <WiFiClient.h>

// Modbus TCP configuration
#define MODBUS_TCP_DEFAULT_PORT 502
#define MAX_MODBUS_CLIENTS 4
#define MODBUS_TCP_TIMEOUT 300000 // 5 minutes (like reference implementation)

// Modbus function codes
#define MODBUS_FC_READ_COILS 0x01
#define MODBUS_FC_READ_DISCRETE_INPUTS 0x02
#define MODBUS_FC_READ_HOLDING_REGISTERS 0x03
#define MODBUS_FC_READ_INPUT_REGISTERS 0x04
#define MODBUS_FC_WRITE_SINGLE_COIL 0x05
#define MODBUS_FC_WRITE_SINGLE_REGISTER 0x06
#define MODBUS_FC_WRITE_MULTIPLE_COILS 0x0F
#define MODBUS_FC_WRITE_MULTIPLE_REGISTERS 0x10

// Modbus exception codes
#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION 0x01
#define MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS 0x02
#define MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE 0x03
#define MODBUS_EXCEPTION_SLAVE_DEVICE_FAILURE 0x04

// MBAP Header structure
struct ModbusMBAPHeader {
    uint16_t transactionId;
    uint16_t protocolId;
    uint16_t length;
    uint8_t unitId;
};

// Client connection structure
struct ModbusClientConnection {
    WiFiClient client;
    uint32_t lastActivity;
    uint32_t connectionTime;
    bool active;
    String clientIP;
};

// Modbus TCP configuration structure
struct ModbusTCPConfig {
    uint16_t port;
    bool enabled;
};

// Modbus TCP server class
class ModbusTCPServer {
public:
    ModbusTCPServer();
    ~ModbusTCPServer();
    
    bool begin(uint16_t port = MODBUS_TCP_DEFAULT_PORT);
    void stop();
    void poll();
    
    // Client management
    int getConnectedClientCount();
    String getClientInfo(int index);
    void disconnectAllClients();
    
    // Configuration
    void setEnabled(bool enabled);
    bool isEnabled() const;
    void setPort(uint16_t port);
    uint16_t getPort() const;
    
private:
    WiFiServer* _server;
    ModbusClientConnection _clients[MAX_MODBUS_CLIENTS];
    ModbusTCPConfig _config;
    bool _running;
    
    // Client management
    void acceptNewClients();
    void processClientRequests();
    void cleanupInactiveClients();
    int findFreeClientSlot();
    
    // Modbus protocol handling
    bool processModbusRequest(ModbusClientConnection& client);
    void sendModbusResponse(ModbusClientConnection& client, uint8_t* response, uint16_t length);
    void sendModbusException(ModbusClientConnection& client, uint16_t transactionId, uint8_t unitId, uint8_t functionCode, uint8_t exceptionCode);
    
    // RTU gateway functions
    bool handleReadRequest(uint8_t slaveId, uint8_t functionCode, uint16_t startAddress, 
                          uint16_t quantity, uint8_t* response, uint16_t& responseLength);
    
    // Utility functions
    uint16_t calculateCRC16(uint8_t* data, uint16_t length);
    uint16_t swapBytes(uint16_t value);
};

// Global functions
void init_modbus_tcp();
void manage_modbus_tcp();
bool loadModbusTCPConfig();
void saveModbusTCPConfig();
void setupModbusTCPAPI();

// Global variables
extern ModbusTCPServer modbusServer;
extern ModbusTCPConfig modbusTCPConfig;
