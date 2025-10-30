#pragma once

#include "../sys_init.h"

// LittleFS configuration
#define CONFIG_FILENAME "/network_config.json"
#define CONFIG_MAGIC_NUMBER 0x55

// Timing defines
#define NTP_MIN_SYNC_INTERVAL 70000   // Too frequent NTP requests will cause failed connections - 70s minimum
#define NTP_UPDATE_INTERVAL 86400000  // 1 day = 86400000ms
//#define NTP_UPDATE_INTERVAL 100000 // 100 seconds (for testing!)

// NTP status defines
#define NTP_STATUS_CURRENT 0
#define NTP_STATUS_STALE 1
#define NTP_STATUS_FAILED 2

// Maximum file size for downloads (5MB to be safe)
#define MAX_DOWNLOAD_SIZE 5242880

void init_network(void);
void manageNetwork(void);

// Modbus TCP functions
void init_modbus_tcp(void);
void manage_modbus_tcp(void);
void setupModbusTCPAPI(void);

// Gateway configuration API
void setupGatewayConfigAPI(void);

// Network module header
void setupEthernet(void);
bool loadNetworkConfig(void);
void saveNetworkConfig(void);
bool applyNetworkConfig(void);
void setStaticIP(void);
void setDHCP(void);

// Web server initialization
void setupWebServer(void);
void startWebServer(void);
void setupNetworkAPI(void);

void setupTimeAPI(void);

void manageEthernet(void);
void handleNetworkManager(void);
void handleWebServer(void);
void handleRoot(void);
void handleFile(const char *path);
void handleNTPUpdates(bool forceUpdate);
void ntpUpdate(void);

// File manager API functions
void handleFileManager(void);
void handleSDListDirectory(void);
void handleSDDownloadFile(void);
void handleSDViewFile(void);
void handleSDDeleteFile(void);
void handleFileManagerPage(void);

// Network configuration structure
struct NetworkConfig
{
    IPAddress ip;
    IPAddress subnet;
    IPAddress gateway;
    IPAddress dns;
    bool useDHCP;
    char hostname[32];
    char ntpServer[64];
    bool ntpEnabled;
    char timezone[8]; // Format: "+13:00"
    bool dstEnabled;  // Daylight Saving Time enabled
    uint16_t modbusTcpPort; // Modbus TCP port
};

void printNetConfig(NetworkConfig config);

// Global variables
extern NetworkConfig networkConfig;

extern Wiznet5500lwIP eth;
extern WebServer server;

// NTP update
extern uint32_t ntpUpdateTimestamp;
extern uint32_t lastNTPUpdateTime; // Last successful NTP update time

extern bool ethernetConnected;
extern bool setStaticIPcmd;
extern bool setDHCPcmd;