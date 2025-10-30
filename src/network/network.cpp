#include "network.h"
#include "modbus_tcp.h"

// Global variables
NetworkConfig networkConfig;

Wiznet5500lwIP eth(PIN_ETH_CS, SPI, PIN_ETH_IRQ);
WebServer server(80);

// NTP update tracking
bool ntpUpdateRequested = false;
uint32_t ntpUpdateTimestamp = 0 - NTP_MIN_SYNC_INTERVAL;
uint32_t lastNTPUpdateTime = 0; // Last successful NTP update time

// Device MAC address (stored as string)
char deviceMacAddress[18];

bool ethernetConnected = false;
bool setStaticIPcmd = false;
bool setDHCPcmd = false;
unsigned long lastNetworkCheckTime = 0;

// Network component initialisation functions ------------------------------>
void init_network() {
    setupEthernet();
    
    // Make sure all API endpoints are set up BEFORE starting the web server
    setupNetworkAPI();
    setupTimeAPI();
    setupModbusTCPAPI();
    setupGatewayConfigAPI();
    
    // Initialize Modbus TCP server
    init_modbus_tcp();
    
    // Import: DO NOT call server.begin() here
    // It will be called after all API endpoints are registered
}

void manageNetwork(void) {
    // Periodic heap monitoring (every 30 seconds) - only log if usage >= 90%
    static uint32_t lastHeapCheck = 0;
    if (millis() - lastHeapCheck >= 30000) {
        lastHeapCheck = millis();
        uint32_t freeHeap = rp2040.getFreeHeap();
        uint32_t totalHeap = rp2040.getTotalHeap();
        uint32_t usedHeap = totalHeap - freeHeap;
        float heapUsage = (float)usedHeap / totalHeap * 100.0f;
        
        // Only log if heap usage is at or above 90%
        if (heapUsage >= 90.0f) {
            log(LOG_WARNING, false, "WARNING: Heap usage critical: %d/%d bytes (%.1f%%), %d free\n", 
                usedHeap, totalHeap, heapUsage, freeHeap);
        }
    }
    
    manageEthernet();
    if (networkConfig.ntpEnabled) handleNTPUpdates(false);
    manage_modbus_tcp();
}

void setupEthernet()
{
  // Load network configuration
  if (!loadNetworkConfig())
  {
    // Set default configuration if load fails
    log(LOG_INFO, false, "Invalid network configuration, using defaults\n");
    networkConfig.ntpEnabled = false;
    networkConfig.useDHCP = true;
    networkConfig.ip = IPAddress(192, 168, 1, 100);
    networkConfig.subnet = IPAddress(255, 255, 255, 0);
    networkConfig.gateway = IPAddress(192, 168, 1, 1);
    networkConfig.dns = IPAddress(8, 8, 8, 8);
    strcpy(networkConfig.timezone, "+12:00");
    strcpy(networkConfig.hostname, "flow-gateway");
    strcpy(networkConfig.ntpServer, "pool.ntp.org");
    networkConfig.dstEnabled = false;
    networkConfig.modbusTcpPort = 502;
    saveNetworkConfig();
  }

  SPI.setMOSI(PIN_ETH_MOSI);
  SPI.setMISO(PIN_ETH_MISO);
  SPI.setSCK(PIN_ETH_SCK);
  SPI.setCS(PIN_ETH_CS);

  eth.setSPISpeed(80000000);

  eth.hostname(networkConfig.hostname);

  // Apply network configuration
  if (!applyNetworkConfig())
  {
    log(LOG_WARNING, false, "Failed to apply network configuration\n");
  }

  else {
    // Get and store MAC address
    uint8_t mac[6];
    eth.macAddress(mac);
    snprintf(deviceMacAddress, sizeof(deviceMacAddress), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    log(LOG_INFO, false, "MAC Address: %s\n", deviceMacAddress);
  }

  // Wait for Ethernet to connect
  uint32_t startTime = millis();
  uint32_t timeout = 10000;
  while (eth.linkStatus() == LinkOFF) {
    if (millis() - startTime > timeout) {
      break;
    }
  }

  if (eth.linkStatus() == LinkOFF) {
    log(LOG_WARNING, false, "Ethernet not connected\n");
    ethernetConnected = false;
  }
  else {
    log(LOG_INFO, false, "Ethernet connected, IP address: %s, Gateway: %s\n",
                eth.localIP().toString().c_str(),
                eth.gatewayIP().toString().c_str());
    ethernetConnected = true;
  }
}

bool loadNetworkConfig()
{
  log(LOG_INFO, true, "Loading network configuration:\n");
  
  // Check if LittleFS is mounted
  if (!LittleFS.begin()) {
    log(LOG_WARNING, true, "Failed to mount LittleFS\n");
    return false;
  }

  // Check if config file exists
  if (!LittleFS.exists(CONFIG_FILENAME)) {
    log(LOG_WARNING, true, "Config file not found\n");
    LittleFS.end();
    return false;
  }

  // Open config file
  File configFile = LittleFS.open(CONFIG_FILENAME, "r");
  if (!configFile) {
    log(LOG_WARNING, true, "Failed to open config file\n");
    LittleFS.end();
    return false;
  }

  // Allocate a buffer to store contents of the file
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();

  if (error) {
    log(LOG_WARNING, true, "Failed to parse config file: %s\n", error.c_str());
    LittleFS.end();
    return false;
  }

  // Check magic number
  uint8_t magicNumber = doc["magic_number"] | 0;
  log(LOG_INFO, true, "Magic number: %x\n", magicNumber);
  if (magicNumber != CONFIG_MAGIC_NUMBER) {
    log(LOG_WARNING, true, "Invalid magic number\n");
    LittleFS.end();
    return false;
  }

  // Parse network configuration
  networkConfig.useDHCP = doc["use_dhcp"] | true;
  
  // Parse IP addresses
  IPAddress ip, subnet, gateway, dns;
  if (ip.fromString(doc["ip"] | "192.168.1.100")) networkConfig.ip = ip;
  if (subnet.fromString(doc["subnet"] | "255.255.255.0")) networkConfig.subnet = subnet;
  if (gateway.fromString(doc["gateway"] | "192.168.1.1")) networkConfig.gateway = gateway;
  if (dns.fromString(doc["dns"] | "8.8.8.8")) networkConfig.dns = dns;
  
  // Parse strings
  strlcpy(networkConfig.hostname, doc["hostname"] | "open-reactor", sizeof(networkConfig.hostname));
  strlcpy(networkConfig.ntpServer, doc["ntp_server"] | "pool.ntp.org", sizeof(networkConfig.ntpServer));
  strlcpy(networkConfig.timezone, doc["timezone"] | "+13:00", sizeof(networkConfig.timezone));
  
  // Parse booleans
  networkConfig.ntpEnabled = doc["ntp_enabled"] | false;
  networkConfig.dstEnabled = doc["dst_enabled"] | false;
  
  // Parse Modbus TCP port
  networkConfig.modbusTcpPort = doc["modbus_tcp_port"] | 502;

  LittleFS.end();
  //debugPrintNetConfig(networkConfig);
  return true;
}

void saveNetworkConfig()
{
  log(LOG_INFO, true, "Saving network configuration:\n");
  printNetConfig(networkConfig);
  
  // Check if LittleFS is mounted
  if (!LittleFS.begin()) {
    log(LOG_WARNING, true, "Failed to mount LittleFS\n");
    return;
  }

  // Create JSON document
  StaticJsonDocument<512> doc;
  
  // Store magic number
  doc["magic_number"] = CONFIG_MAGIC_NUMBER;
  
  // Store network configuration
  doc["use_dhcp"] = networkConfig.useDHCP;
  
  // Store IP addresses as strings
  doc["ip"] = networkConfig.ip.toString();
  doc["subnet"] = networkConfig.subnet.toString();
  doc["gateway"] = networkConfig.gateway.toString();
  doc["dns"] = networkConfig.dns.toString();
  
  // Store strings
  doc["hostname"] = networkConfig.hostname;
  doc["ntp_server"] = networkConfig.ntpServer;
  doc["timezone"] = networkConfig.timezone;
  
  // Store booleans
  doc["ntp_enabled"] = networkConfig.ntpEnabled;
  doc["dst_enabled"] = networkConfig.dstEnabled;
  
  // Store Modbus TCP port
  doc["modbus_tcp_port"] = networkConfig.modbusTcpPort;
    
  // Open file for writing
  File configFile = LittleFS.open(CONFIG_FILENAME, "w");
  if (!configFile) {
    log(LOG_WARNING, true, "Failed to open config file for writing\n");
    LittleFS.end();
    return;
  }
  
  // Write to file
  if (serializeJson(doc, configFile) == 0) {
    log(LOG_WARNING, true, "Failed to write config file\n");
  }
  
  // Close file
  configFile.close();
}

bool applyNetworkConfig()
{
  if (networkConfig.useDHCP)
  {
    // Call eth.end() to release the DHCP lease if we already had one since last boot (handles changing networks on the fly)
    // NOTE: requires modification of end function in LwipIntDev.h, added dhcp_release_and_stop(&_netif); before netif_remove(&_netif); line 452)
    eth.end();
    
    if (!eth.begin())
    {
      log(LOG_WARNING, true, "Failed to configure Ethernet using DHCP, falling back to 192.168.1.100\n");
      IPAddress defaultIP = {192, 168, 1, 100};
      eth.config(defaultIP);
      if (!eth.begin()) {
        log(LOG_WARNING, true, "Failed to configure Ethernet using static IP, falling back to saved configuration\n");
        return false;
      }
    }
  }
  else
  {
    eth.config(networkConfig.ip, networkConfig.gateway, networkConfig.subnet, networkConfig.dns);
    if (!eth.begin()) return false;
  }
  return true;
}

void setStaticIP()
{
  log(LOG_INFO, true, "Setting static IP to 192.168.1.100\n");
  networkConfig.useDHCP = false;
  saveNetworkConfig();
  log(LOG_INFO, true, "Restarting...\n");
  delay(1000);
  rp2040.restart();
}

void setDHCP()
{
  log(LOG_INFO, true, "Setting DHCP\n");
  networkConfig.useDHCP = true;
  saveNetworkConfig();
  log(LOG_INFO, true, "Restarting...\n");
  delay(1000);
  rp2040.restart();
}

void setupNetworkAPI()
{
  server.on("/api/network", HTTP_GET, []()
            {
        StaticJsonDocument<512> doc;
        doc["mode"] = networkConfig.useDHCP ? "dhcp" : "static";
        
        // Get current IP configuration
        IPAddress ip = eth.localIP();
        IPAddress subnet = eth.subnetMask();
        IPAddress gateway = eth.gatewayIP();
        IPAddress dns = eth.dnsIP();
        
        char ipStr[16];
        snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        doc["ip"] = ipStr;
        
        char subnetStr[16];
        snprintf(subnetStr, sizeof(subnetStr), "%d.%d.%d.%d", subnet[0], subnet[1], subnet[2], subnet[3]);
        doc["subnet"] = subnetStr;
        
        char gatewayStr[16];
        snprintf(gatewayStr, sizeof(gatewayStr), "%d.%d.%d.%d", gateway[0], gateway[1], gateway[2], gateway[3]);
        doc["gateway"] = gatewayStr;
        
        char dnsStr[16];
        snprintf(dnsStr, sizeof(dnsStr), "%d.%d.%d.%d", dns[0], dns[1], dns[2], dns[3]);
        doc["dns"] = dnsStr;

        doc["mac"] = deviceMacAddress;
        doc["hostname"] = networkConfig.hostname;
        doc["ntp"] = networkConfig.ntpServer;
        doc["dst"] = networkConfig.dstEnabled;
        doc["modbusTcpPort"] = networkConfig.modbusTcpPort;
        
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response); });

  server.on("/api/network", HTTP_POST, []()
            {
              if (!server.hasArg("plain"))
              {
                server.send(400, "application/json", "{\"error\":\"No data received\"}");
                return;
              }

              StaticJsonDocument<512> doc;
              DeserializationError error = deserializeJson(doc, server.arg("plain"));

              if (error)
              {
                server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
              }

              // Update network configuration
              networkConfig.useDHCP = doc["mode"] == "dhcp";

              if (!networkConfig.useDHCP)
              {
                // Validate and parse IP addresses
                if (!networkConfig.ip.fromString(doc["ip"] | ""))
                {
                  server.send(400, "application/json", "{\"error\":\"Invalid IP address\"}");
                  return;
                }
                if (!networkConfig.subnet.fromString(doc["subnet"] | ""))
                {
                  server.send(400, "application/json", "{\"error\":\"Invalid subnet mask\"}");
                  return;
                }
                if (!networkConfig.gateway.fromString(doc["gateway"] | ""))
                {
                  server.send(400, "application/json", "{\"error\":\"Invalid gateway\"}");
                  return;
                }
                if (!networkConfig.dns.fromString(doc["dns"] | ""))
                {
                  server.send(400, "application/json", "{\"error\":\"Invalid DNS server\"}");
                  return;
                }
              }

              // Update hostname
              strlcpy(networkConfig.hostname, doc["hostname"] | "open-reactor", sizeof(networkConfig.hostname));

              // Update NTP server
              strlcpy(networkConfig.ntpServer, doc["ntp"] | "pool.ntp.org", sizeof(networkConfig.ntpServer));

              // Update DST setting if provided
              if (doc.containsKey("dst")) {
                networkConfig.dstEnabled = doc["dst"];
              }

              // Update Modbus TCP port if provided
              if (doc.containsKey("modbusTcpPort")) {
                uint16_t port = doc["modbusTcpPort"];
                if (port >= 1 && port <= 65535) {
                  networkConfig.modbusTcpPort = port;
                }
              }

              // Log network configuration change
              log(LOG_INFO, true, "Network configuration changed via API: mode=%s, hostname=%s\n",
                  networkConfig.useDHCP ? "DHCP" : "Static", networkConfig.hostname);

              // Save configuration to storage
              saveNetworkConfig();

              // Send success response before applying changes
              server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Configuration saved\"}");

              // Apply new configuration after a short delay
              delay(1000);
              rp2040.reboot();
            });
}

void setupWebServer()
{
  // Initialize LittleFS for serving web files
  if (!LittleFS.begin())
  {
    log(LOG_ERROR, true, "LittleFS Mount Failed\n");
    return;
  }

  // Route handlers
  server.on("/", HTTP_GET, handleRoot);
  server.on("/filemanager", HTTP_GET, handleFileManager);
  
  // API endpoints for file manager
  server.on("/api/sd/list", HTTP_GET, handleSDListDirectory);
  server.on("/api/sd/download", HTTP_GET, handleSDDownloadFile);
  server.on("/api/sd/view", HTTP_GET, handleSDViewFile);
  server.on("/api/sd/delete", HTTP_DELETE, handleSDDeleteFile);

  // Comprehensive system status endpoint
  server.on("/api/system/status", HTTP_GET, []() {
    StaticJsonDocument<768> doc;
    
    // Try to acquire locks with a short retry period
    int retries = 5;
    while (retries > 0 && (statusLocked || flowCounterDataLocked || sdLocked)) {
      delay(2);
      retries--;
    }
    
    // If still locked after retries, return cached/partial data instead of error
    if (statusLocked || flowCounterDataLocked) {
      // Return minimal response to keep client connection alive
      doc["uptime"] = millis() / 1000;
      doc["busy"] = true;
      String response;
      serializeJson(doc, response);
      server.send(200, "application/json", response);
      return;
    }
    
    statusLocked = true;
    flowCounterDataLocked = true;
    
    // Ethernet info
    JsonObject ethernet = doc.createNestedObject("ethernet");
    ethernet["connected"] = ethernetConnected;
    if (ethernetConnected) {
      ethernet["ip"] = eth.localIP().toString();
      ethernet["gateway"] = eth.gatewayIP().toString();
      ethernet["subnet"] = eth.subnetMask().toString();
      ethernet["dhcp"] = networkConfig.useDHCP;
    }
    
    // System uptime and version
    doc["uptime"] = millis() / 1000;
    doc["version"] = VERSION;
          
    // SD card info
    JsonObject sd = doc.createNestedObject("sd");
    if (!sdLocked) {
      sdLocked = true;
      sd["inserted"] = sdInfo.inserted;
      sd["ready"] = sdInfo.ready;
      
      // Only include these if SD card is ready
      if (sdInfo.ready) {
        sd["capacityGB"] = sdInfo.cardSizeBytes / 1000000000.0;
        sd["freeSpaceGB"] = sdInfo.cardFreeBytes / 1000000000.0;
        sd["logFileSizeKB"] = sdInfo.logSizeBytes / 1000.0;
        sd["sensorFileSizeKB"] = sdInfo.sensorSizeBytes / 1000.0;
      }
      sdLocked = false;
    }
    
    // RS485 Modbus RTU status
    JsonObject modbus = doc.createNestedObject("modbus");
    modbus["connected"] = status.modbusConnected;
    
    // Check for communication errors across all flow counters (now safely locked)
    bool hasError = false;
    int activeDevices = 0;
    int errorDevices = 0;
    for (int i = 0; i < MAX_FLOW_COUNTERS; i++) {
      if (gatewayConfig.ports[i].enabled) {
        activeDevices++;
        if (flowCounterData[i].commError) {
          hasError = true;
          errorDevices++;
        }
      }
    }
    modbus["hasError"] = hasError;
    modbus["activeDevices"] = activeDevices;
    modbus["errorDevices"] = errorDevices;
    
    // Modbus TCP status
    JsonObject modbusTcp = doc.createNestedObject("modbusTcp");
    modbusTcp["enabled"] = modbusTCPConfig.enabled;
    modbusTcp["port"] = modbusTCPConfig.port > 0 ? modbusTCPConfig.port : MODBUS_TCP_DEFAULT_PORT;
    modbusTcp["connectedClients"] = modbusServer.getConnectedClientCount();
    
    // Detailed client information
    JsonArray clients = modbusTcp.createNestedArray("clients");
    for (int i = 0; i < MAX_MODBUS_CLIENTS; i++) {
      String clientInfo = modbusServer.getClientInfo(i);
      if (clientInfo.length() > 0) {
        clients.add(clientInfo);
      }
    }
    
    flowCounterDataLocked = false;
    statusLocked = false;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });

  // System version endpoint
  server.on("/api/system/version", HTTP_GET, []() {
    StaticJsonDocument<128> doc;
    doc["version"] = VERSION;
    doc["version_string"] = VERSION_STRING;
    doc["major"] = VERSION_MAJOR;
    doc["minor"] = VERSION_MINOR;
    doc["patch"] = VERSION_PATCH;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });

  // System reboot endpoint
  server.on("/api/system/reboot", HTTP_POST, []() {
    // Send response first before rebooting
    server.send(200, "application/json", "{\"success\":true,\"message\":\"System is rebooting...\"}");
    
    // Small delay to ensure response is sent
    delay(100);
    
    // Trigger system reboot
    log(LOG_INFO, true, "System reboot requested via API\n");
    rp2040.restart();
  });

  // Handle static files
  server.onNotFound([]()
                    { handleFile(server.uri().c_str()); });

  // NOTE: server.begin() is now moved to startWebServer() function
  log(LOG_INFO, true, "Web server configured, but not yet started\n");
}

// Start the web server after all API endpoints have been registered
void startWebServer() {
  log(LOG_INFO, true, "Starting web server...\n");
  
  // Start the server
  server.begin();
  log(LOG_INFO, true, "HTTP server started\n");
  
  // Set Webserver Status
  if (!statusLocked) {
    statusLocked = true;
    status.webserverUp = true;
    status.webserverBusy = false;
    status.updated = true;
    statusLocked = false;
  }
}

void setupTimeAPI()
{
  // Gateway does not have RTC - time API disabled
  server.on("/api/time", HTTP_GET, []()
            {
        StaticJsonDocument<256> doc;
        doc["error"] = "No RTC available";
        doc["note"] = "Flow counters provide their own timestamps";
        
        // Return uptime instead
        doc["uptime"] = millis() / 1000;
        
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
        
        /* DateTime dt;
        if (getGlobalDateTime(dt)) {
            char dateStr[11];
            snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d", 
                    dt.year, dt.month, dt.day);
            doc["date"] = dateStr;
            
            char timeStr[9];  // Increased size to accommodate seconds
            snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", 
                    dt.hour, dt.minute, dt.second);  // Added seconds
            doc["time"] = timeStr;
            
            doc["timezone"] = networkConfig.timezone;
            doc["ntpEnabled"] = networkConfig.ntpEnabled;
            doc["dst"] = networkConfig.dstEnabled;
            
            // Add NTP status information
            if (networkConfig.ntpEnabled) {
                // Calculate NTP status
                uint8_t ntpStatus = NTP_STATUS_FAILED;
                uint32_t timeSinceLastUpdate = 0;
                
                if (lastNTPUpdateTime > 0) {
                    timeSinceLastUpdate = millis() - lastNTPUpdateTime;
                    if (timeSinceLastUpdate < (NTP_UPDATE_INTERVAL * 3)) {
                        ntpStatus = NTP_STATUS_CURRENT;
                    } else {
                        ntpStatus = NTP_STATUS_STALE;
                    }
                }
                
                doc["ntpStatus"] = ntpStatus;
                
                // Format last update time
                if (lastNTPUpdateTime > 0) {
                    char lastUpdateStr[20];
                    uint32_t seconds = timeSinceLastUpdate / 1000;
                    uint32_t minutes = seconds / 60;
                    uint32_t hours = minutes / 60;
                    uint32_t days = hours / 24;
                    
                    if (days > 0) {
                        snprintf(lastUpdateStr, sizeof(lastUpdateStr), "%d days ago", days);
                    } else if (hours > 0) {
                        snprintf(lastUpdateStr, sizeof(lastUpdateStr), "%d hours ago", hours);
                    } else if (minutes > 0) {
                        snprintf(lastUpdateStr, sizeof(lastUpdateStr), "%d minutes ago", minutes);
                    } else {
                        snprintf(lastUpdateStr, sizeof(lastUpdateStr), "%d seconds ago", seconds);
                    }
                    doc["lastNtpUpdate"] = lastUpdateStr;
                } else {
                    doc["lastNtpUpdate"] = "Never";
                }
            }
            
            String response;
            serializeJson(doc, response);
            server.send(200, "application/json", response);
        } else {
            server.send(500, "application/json", "{\"error\": \"Failed to get current time\"}");
        }
        */
    });

  server.on("/api/time", HTTP_POST, []() {
        // Gateway does not have RTC - time setting disabled
        server.send(501, "application/json", "{\"error\":\"No RTC available - time setting not supported\"}");
        
        /*
        StaticJsonDocument<200> doc;
        String json = server.arg("plain");
        DeserializationError error = deserializeJson(doc, json);

        log(LOG_INFO, true, "Received JSON: %s\n", json.c_str());
        
        if (error) {
            server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            log(LOG_ERROR, true, "JSON parsing error: %s\n", error.c_str());
            return;
        }

        // Validate required fields
        if (!doc.containsKey("date") || !doc.containsKey("time")) {
            server.send(400, "application/json", "{\"error\":\"Missing required fields\"}");
            log(LOG_ERROR, true, "Missing required fields in JSON\n");
            return;
        }

        // Update timezone if provided
        if (doc.containsKey("timezone")) {
          const char* tz = doc["timezone"];
          log(LOG_INFO, true, "Received timezone: %s\n", tz);
          // Basic timezone format validation (+/-HH:MM)
          int tzHour, tzMin;
          if (sscanf(tz, "%d:%d", &tzHour, &tzMin) != 2 ||
              tzHour < -12 || tzHour > 14 || tzMin < 0 || tzMin > 59) {
              server.send(400, "application/json", "{\"error\":\"Invalid timezone format\"}");
              return;
          }
          strncpy(networkConfig.timezone, tz, sizeof(networkConfig.timezone) - 1);
          networkConfig.timezone[sizeof(networkConfig.timezone) - 1] = '\0';
          log(LOG_INFO, true, "Updated timezone: %s\n", networkConfig.timezone);
        }

        // Update NTP enabled status if provided
        if (doc.containsKey("ntpEnabled")) {
          bool ntpWasEnabled = networkConfig.ntpEnabled;
          networkConfig.ntpEnabled = doc["ntpEnabled"];
          if (networkConfig.ntpEnabled) {
            // Update DST setting if provided
            if (doc.containsKey("dstEnabled")) {
              networkConfig.dstEnabled = doc["dstEnabled"];
            }
            handleNTPUpdates(true);
            server.send(200, "application/json", "{\"status\": \"success\", \"message\": \"NTP enabled, manual time update ignored\"}");
            saveNetworkConfig(); // Save to storage when NTP settings change
            return;
          }
          if (ntpWasEnabled) {
            server.send(200, "application/json", "{\"status\": \"success\", \"message\": \"NTP disabled, manual time update required\"}");
            saveNetworkConfig(); // Save to storage when NTP settings change
          }
        }

        // Validate and parse date and time
        const char* dateStr = doc["date"];
        uint16_t year;
        uint8_t month, day;
        const char* timeStr = doc["time"];
        uint8_t hour, minute;

        // Parse date string (format: YYYY-MM-DD)
        if (sscanf(dateStr, "%hu-%hhu-%hhu", &year, &month, &day) != 3 ||
            year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31) {
            server.send(400, "application/json", "{\"error\": \"Invalid date format or values\"}");
            log(LOG_ERROR, true, "Invalid date format or values in JSON\n");
            return;
        }

        // Parse time string (format: HH:MM)          
        if (sscanf(timeStr, "%hhu:%hhu", &hour, &minute) != 2 ||
            hour > 23 || minute > 59) {
            server.send(400, "application/json", "{\"error\": \"Invalid time format or values\"}");
            return;
        }

        DateTime newDateTime = {year, month, day, hour, minute, 0};
        if (updateGlobalDateTime(newDateTime)) {
                server.send(200, "application/json", "{\"status\": \"success\"}");
        } else {
                server.send(500, "application/json", "{\"error\": \"Failed to update time\"}");
        }
        */
  } );
}

// Network management functions --------------------------------------------->
// Handle ethernet plug and unplug events (from main loop)
void manageEthernet(void)
{
  // Do network tasks if ethernet is connected
  if (ethernetConnected) {
    if (eth.linkStatus() == LinkOFF) {
      ethernetConnected = false;
      // Set Webserver Status
      if (!statusLocked) {
        statusLocked = true;
        status.webserverUp = false;
        status.webserverBusy = false;
        status.updated = true;
        statusLocked = false;
      }
      log(LOG_INFO, true, "Ethernet disconnected, waiting for reconnect\n");
    } else {
      if (setStaticIPcmd) {
        setStaticIP();
      } else if (setDHCPcmd) {
        setDHCP();
      }
      // Ethernet is still connected
      handleWebServer();
    }
  }
  else if (eth.linkStatus() == LinkON) {
    ethernetConnected = true;
    if(!applyNetworkConfig()) {
      log(LOG_ERROR, true, "Failed to apply network configuration!\n");
    }
    else {
      log(LOG_INFO, true, "Ethernet re-connected, IP address: %s, Gateway: %s\n",
                  eth.localIP().toString().c_str(),
                  eth.gatewayIP().toString().c_str());
    }
  }
}

// Handle web server requests
void handleWebServer() {
  if(!ethernetConnected) {
    return;
  }
  server.handleClient();
  if (!statusLocked) {
    statusLocked = true;
    status.webserverBusy = false;
    status.webserverUp = true;
    status.updated = true;
    statusLocked = false;
  }
}

// Webserver callbacks ----------------------------------------------------->
void handleRoot()
{
  handleFile("/index.html");
}

void handleFileManager(void) {
  // Check if SD card is ready
  if (!sdInfo.ready) {
    server.send(503, "application/json", "{\"error\":\"SD card not available\"}");
    return;
  }
  
  // Serve the main index page since file manager is now integrated
  handleRoot();
}

void handleFileManagerPage(void) {
  // Redirects to handleRoot (index.html) as file manager is now integrated
  handleRoot();
}

// Handle file requests - retrieve from LittleFS and send to client
void handleFile(const char *path)
{
  // Check ethernet status
  if(eth.status() != WL_CONNECTED) {
    if (!statusLocked) {
      statusLocked = true;
      status.webserverBusy = false;
      status.webserverUp = false;
      status.updated = true;
      statusLocked = false;
    }
    return;
  }
  
  if (!statusLocked) {
    statusLocked = true;
    status.webserverBusy = true;
    statusLocked = false;
  }
  
  // Determine content type
  String contentType;
  if (strstr(path, ".html"))
    contentType = "text/html";
  else if (strstr(path, ".css"))
    contentType = "text/css";
  else if (strstr(path, ".js"))
    contentType = "application/javascript";
  else if (strstr(path, ".json"))
    contentType = "application/json";
  else if (strstr(path, ".ico"))
    contentType = "image/x-icon";
  else if (strstr(path, ".woff2"))
    contentType = "font/woff2";
  else if (strstr(path, ".woff"))
    contentType = "font/woff";
  else
    contentType = "text/plain";

  // Build file path
  String filePath = path;
  if (filePath.endsWith("/"))
    filePath += "index.html";
  if (!filePath.startsWith("/"))
    filePath = "/" + filePath;

  // CRITICAL: Check if LittleFS is mounted before accessing
  // This prevents crashes if filesystem becomes corrupted
  FSInfo fs_info;
  if (!LittleFS.info(fs_info)) {
    log(LOG_ERROR, false, "LittleFS filesystem error - attempting remount\n");
    server.send(503, "text/plain", "Filesystem error");
    
    // Try to remount LittleFS
    LittleFS.end();
    delay(100);
    if (!LittleFS.begin()) {
      log(LOG_ERROR, false, "Failed to remount LittleFS!\n");
    } else {
      log(LOG_INFO, false, "LittleFS successfully remounted\n");
    }
    
    if (!statusLocked) {
      statusLocked = true;
      status.webserverBusy = false;
      statusLocked = false;
    }
    return;
  }

  // Check if file exists
  if (LittleFS.exists(filePath))
  {
    File file = LittleFS.open(filePath, "r");
    
    // Verify file opened successfully
    if (!file) {
      log(LOG_ERROR, false, "Failed to open file: %s\n", filePath.c_str());
      server.send(500, "text/plain", "Failed to open file");
    } else {
      // Check file size is reasonable (prevent serving corrupted files)
      size_t fileSize = file.size();
      if (fileSize == 0 || fileSize > 512000) { // Max 500KB for web assets
        log(LOG_WARNING, false, "Suspicious file size for %s: %d bytes\n", filePath.c_str(), fileSize);
      }
      
      size_t sent = server.streamFile(file, contentType);
      file.close();
      
      // Verify all bytes were sent
      if (sent != fileSize) {
        log(LOG_WARNING, false, "File %s: sent %d of %d bytes\n", filePath.c_str(), sent, fileSize);
      }
    }
  }
  else
  {
    log(LOG_DEBUG, false, "File not found: %s\n", filePath.c_str());
    server.send(404, "text/plain", "File not found");
  }
  
  if (!statusLocked) {
    statusLocked = true;
    status.webserverBusy = false;
    status.webserverUp = true;
    status.updated = true;
    statusLocked = false;
  }
}

void handleSDDownloadFile(void) {
  if (sdLocked) {
    server.send(423, "application/json", "{\"error\":\"SD card is locked\"}");
    return;
  }
  
  if (!sdInfo.ready) {
    server.send(503, "application/json", "{\"error\":\"SD card not available\"}");
    return;
  }
  
  // Get the requested file path from the query parameter
  String path = server.hasArg("path") ? server.arg("path") : "";
  
  if (path.length() == 0) {
    server.send(400, "application/json", "{\"error\":\"File path not specified\"}");
    return;
  }
  
  // Make sure path starts with a forward slash
  if (!path.startsWith("/")) {
    path = "/" + path;
  }
  
  sdLocked = true;
  
  // Check if the file exists
  if (!sd.exists(path.c_str())) {
    sdLocked = false;
    server.send(404, "application/json", "{\"error\":\"File not found\"}");
    return;
  }
  
  // Open the file
  FsFile file = sd.open(path.c_str(), O_RDONLY);
  
  if (!file) {
    sdLocked = false;
    server.send(500, "application/json", "{\"error\":\"Failed to open file\"}");
    return;
  }
  
  if (file.isDirectory()) {
    file.close();
    sdLocked = false;
    server.send(400, "application/json", "{\"error\":\"Path is a directory, not a file\"}");
    return;
  }
  
  // Get file size
  size_t fileSize = file.size();
  
  // Check file size limit
  if (fileSize > MAX_DOWNLOAD_SIZE) {
    file.close();
    sdLocked = false;
    char errorMsg[128];
    snprintf(errorMsg, sizeof(errorMsg), 
             "{\"error\":\"File is too large for download (%u bytes). Maximum size is %u bytes.\"}",
             fileSize, MAX_DOWNLOAD_SIZE);
    server.send(413, "application/json", errorMsg);
    return;
  }
  
  // Get the filename from the path
  String fileName = path;
  int lastSlash = fileName.lastIndexOf('/');
  if (lastSlash >= 0) {
    fileName = fileName.substring(lastSlash + 1);
  }
  
  // Enhanced headers to force download with the correct filename
  String contentDisposition = "attachment; filename=\"" + fileName + "\"; filename*=UTF-8''" + fileName;
  
  // Use simpler header approach to avoid memory issues
  server.sendHeader("Content-Type", "application/octet-stream");
  server.sendHeader("Content-Disposition", contentDisposition);
  server.sendHeader("Cache-Control", "no-cache");
  
  // Set a watchdog timer and timeout to prevent system hangs
  uint32_t startTime = millis();
  uint32_t lastProgressTime = startTime;
  const uint32_t timeout = 30000; // 30 second timeout
  
  WiFiClient client = server.client();
  
  // Stream the file in chunks with timeout checks
  const size_t bufferSize = 1024; // Smaller buffer size for better reliability
  uint8_t buffer[bufferSize];
  size_t bytesRead;
  size_t totalBytesRead = 0;
  bool timeoutOccurred = false;
  
  server.setContentLength(fileSize);
  server.send(200, "application/octet-stream", ""); // Send headers only
  
  // Stream file with careful progress monitoring
  while (totalBytesRead < fileSize) {
    // Check for timeout
    if (millis() - lastProgressTime > timeout) {
      log(LOG_WARNING, true, "Timeout occurred during file download\n");
      timeoutOccurred = true;
      break;
    }
    
    // Read a chunk from the file
    bytesRead = file.read(buffer, min(bufferSize, fileSize - totalBytesRead));
    
    if (bytesRead == 0) {
      // End of file or error condition
      break;
    }
    
    // Write chunk to client
    if (client.write(buffer, bytesRead) != bytesRead) {
      // Client disconnected or write error
      log(LOG_WARNING, true, "Client write error during file download\n");
      break;
    }
    
    totalBytesRead += bytesRead;
    lastProgressTime = millis(); // Update progress timer
    
    // Allow other processes to run
    yield();
  }
  
  // Clean up
  file.close();
  sdLocked = false;
  
  if (timeoutOccurred) {
    log(LOG_ERROR, true, "File download timed out after %u bytes\n", totalBytesRead);
  } else if (totalBytesRead == fileSize) {
    log(LOG_INFO, true, "File download completed successfully: %s (%u bytes)\n", 
        fileName.c_str(), totalBytesRead);
  } else {
    log(LOG_WARNING, true, "File download incomplete: %u of %u bytes transferred\n", 
        totalBytesRead, fileSize);
  }
}

void handleSDViewFile(void) {
  if (sdLocked) {
    server.send(423, "application/json", "{\"error\":\"SD card is locked\"}");
    return;
  }
  
  if (!sdInfo.ready) {
    server.send(503, "application/json", "{\"error\":\"SD card not available\"}");
    return;
  }
  
  // Get the requested file path from the query parameter
  String path = server.hasArg("path") ? server.arg("path") : "";
  
  if (path.length() == 0) {
    server.send(400, "application/json", "{\"error\":\"File path not specified\"}");
    return;
  }
  
  // Make sure path starts with a forward slash
  if (!path.startsWith("/")) {
    path = "/" + path;
  }
  
  sdLocked = true;
  
  // Check if the file exists
  if (!sd.exists(path.c_str())) {
    sdLocked = false;
    server.send(404, "application/json", "{\"error\":\"File not found\"}");
    return;
  }
  
  // Open the file
  FsFile file = sd.open(path.c_str(), O_RDONLY);
  
  if (!file) {
    sdLocked = false;
    server.send(500, "application/json", "{\"error\":\"Failed to open file\"}");
    return;
  }
  
  if (file.isDirectory()) {
    file.close();
    sdLocked = false;
    server.send(400, "application/json", "{\"error\":\"Path is a directory, not a file\"}");
    return;
  }
  
  // Get file size
  size_t fileSize = file.size();
  
  // Get the filename from the path
  String fileName = path;
  int lastSlash = fileName.lastIndexOf('/');
  if (lastSlash >= 0) {
    fileName = fileName.substring(lastSlash + 1);
  }
  
  // Determine content type based on file extension
  String contentType = "text/plain";
  if (fileName.endsWith(".html") || fileName.endsWith(".htm")) contentType = "text/html";
  else if (fileName.endsWith(".css")) contentType = "text/css";
  
  // Stream the file to the client
  WiFiClient client = server.client();
  
  // Use a buffer for more efficient file transfer
  const size_t bufferSize = 2048;
  uint8_t buffer[bufferSize];
  size_t bytesRead;
  
  do {
    bytesRead = file.read(buffer, bufferSize);
    if (bytesRead > 0) {
      client.write(buffer, bytesRead);
    }
  } while (bytesRead == bufferSize);
  
  file.close();
  sdLocked = false;
}

void handleSDDeleteFile(void) {
  if (sdLocked) {
    server.send(423, "application/json", "{\"error\":\"SD card is locked\"}");
    return;
  }
  
  if (!sdInfo.ready) {
    server.send(503, "application/json", "{\"error\":\"SD card not available\"}");
    return;
  }
  
  // Get the requested file path from the query parameter
  String path = server.hasArg("path") ? server.arg("path") : "";
  
  if (path.length() == 0) {
    server.send(400, "application/json", "{\"error\":\"File path not specified\"}");
    return;
  }
  
  // Make sure path starts with a forward slash
  if (!path.startsWith("/")) {
    path = "/" + path;
  }
  
  sdLocked = true;
  
  // Check if the file exists
  if (!sd.exists(path.c_str())) {
    sdLocked = false;
    server.send(404, "application/json", "{\"error\":\"File not found\"}");
    return;
  }
  
  // Attempt to delete the file
  if (!sd.remove(path.c_str())) {
    sdLocked = false;
    server.send(500, "application/json", "{\"error\":\"Failed to delete file\"}");
    return;
  }
  
  sdLocked = false;
  
  log(LOG_INFO, false, "File deleted: %s\n", path.c_str());
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"File deleted\"}");
}

// NTP management functions ------------------------------------------------>
void ntpUpdate(void) {
  static WiFiUDP udp;
  static NTPClient timeClient(udp, networkConfig.ntpServer);
  static bool clientInitialized = false;

  
  if (!clientInitialized)
  {
    timeClient.begin();
    clientInitialized = true;
  }

  if (!eth.linkStatus()) return;

  if (!timeClient.update()) {
    log(LOG_WARNING, true, "Failed to get time from NTP server, retrying\n");
    bool updateSuccessful = false;
    for (int i = 0; i < 3; i++) {
      if (timeClient.update()) {
        updateSuccessful = true;
        break;
      }
      delay(10);
   }
    if (!updateSuccessful) {
      log(LOG_ERROR, true, "Failed to get time from NTP server, giving up\n");
      return;
    }
  }
  // Get NTP time
  time_t epochTime = timeClient.getEpochTime();

  // Apply timezone offset
  int tzHours = 0, tzMinutes = 0, tzDSToffset = 0;
  if (networkConfig.dstEnabled) {
    tzDSToffset = 3600;
  }
  sscanf(networkConfig.timezone, "%d:%d", &tzHours, &tzMinutes);
  epochTime += (tzHours * 3600 + tzMinutes * 60 + tzDSToffset);

  // Gateway does not have RTC - NTP time update disabled
  log(LOG_INFO, true, "NTP time received but no RTC to update\n");
  lastNTPUpdateTime = millis(); // Record the time of successful update
  
  /* Convert to DateTime and update using thread-safe function
  DateTime newTime = epochToDateTime(epochTime);
  if (!updateGlobalDateTime(newTime))
  {
    log(LOG_ERROR, true, "Failed to update time from NTP\n");
  }
  else
  {
    log(LOG_INFO, true, "Time updated from NTP server\n");
    lastNTPUpdateTime = millis(); // Record the time of successful update
  }
  */
}

void handleNTPUpdates(bool forceUpdate) {
  if (!networkConfig.ntpEnabled) return;
  uint32_t timeSinceLastUpdate = millis() - ntpUpdateTimestamp;

  // Check if there's an NTP update request or if it's time for a scheduled update
  if (ntpUpdateRequested || timeSinceLastUpdate > NTP_UPDATE_INTERVAL || forceUpdate)
  {
    if (timeSinceLastUpdate < NTP_MIN_SYNC_INTERVAL) {
      log(LOG_INFO, true, "Time since last NTP update: %ds - skipping\n", timeSinceLastUpdate/1000);
      return;
    }
    ntpUpdate();
    ntpUpdateTimestamp = millis();
    ntpUpdateRequested = false; // Clear the request flag
  }
}

// SD Card File Manager API functions -------------------------------------->
void handleSDListDirectory(void) {
  if (sdLocked) {
    server.send(423, "application/json", "{\"error\":\"SD card is locked\"}");
    return;
  }
  
  if (!sdInfo.ready) {
    server.send(503, "application/json", "{\"error\":\"SD card not available\"}");
    return;
  }
  
  // Get the requested directory path from the query parameter
  String path = server.hasArg("path") ? server.arg("path") : "/";
  
  // Make sure path starts with a forward slash
  if (!path.startsWith("/")) {
    path = "/" + path;
  }
  
  sdLocked = true;
  
  // Check if the path exists and is a directory
  if (!sd.exists(path.c_str())) {
    sdLocked = false;
    server.send(404, "application/json", "{\"error\":\"Directory not found\"}");
    return;
  }
  
  FsFile dir = sd.open(path.c_str());
  
  if (!dir.isDirectory()) {
    dir.close();
    sdLocked = false;
    server.send(400, "application/json", "{\"error\":\"Not a directory\"}");
    return;
  }
  
  // Create a JSON document for the response
  DynamicJsonDocument doc(16384); // Adjust size based on expected directory size
  
  doc["path"] = path;
  JsonArray files = doc.createNestedArray("files");
  JsonArray directories = doc.createNestedArray("directories");
  
  // Read all files and directories in the requested path
  dir.rewindDirectory();
  
  FsFile file;
  char filename[256];
  while (file.openNext(&dir)) {
    file.getName(filename, sizeof(filename));
    
    // Skip hidden files and . and ..
    if (filename[0] == '.') {
      file.close();
      continue;
    }
    
    // Create a JSON object for the file or directory
    if (file.isDirectory()) {
      JsonObject dirObj = directories.createNestedObject();
      dirObj["name"] = filename;
      
      // Calculate full path for this directory
      String fullPath = path;
      if (!path.endsWith("/")) fullPath += "/";
      fullPath += filename;
      dirObj["path"] = fullPath;
    } else {
      JsonObject fileObj = files.createNestedObject();
      fileObj["name"] = filename;
      fileObj["size"] = file.size();
      
      // Calculate full path for this file
      String fullPath = path;
      if (!path.endsWith("/")) fullPath += "/";
      fullPath += filename;
      fileObj["path"] = fullPath;
      
      // Add last modified date
      uint16_t fileDate, fileTime;
      file.getModifyDateTime(&fileDate, &fileTime);
      
      int year = FS_YEAR(fileDate);
      int month = FS_MONTH(fileDate);
      int day = FS_DAY(fileDate);
      int hour = FS_HOUR(fileTime);
      int minute = FS_MINUTE(fileTime);
      int second = FS_SECOND(fileTime);
      
      char dateTimeStr[32];
      snprintf(dateTimeStr, sizeof(dateTimeStr), "%04d-%02d-%02d %02d:%02d:%02d", 
               year, month, day, hour, minute, second);
      fileObj["modified"] = dateTimeStr;
    }
    
    file.close();
  }
  
  dir.close();
  
  // Add system log file info if listing root directory
  if (path == "/") {
    if (sd.exists("/logs/system.txt")) {
      FsFile logFile = sd.open("/logs/system.txt");
      if (logFile) {
        doc["system_log_size"] = (uint32_t)logFile.size();
        logFile.close();
      }
    }
  }
  
  sdLocked = false;
  
  // Send the JSON response
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// Debug functions --------------------------------------------------------->
void printNetConfig(NetworkConfig config)
{
  log(LOG_INFO, true, "Mode: %s\n", config.useDHCP ? "DHCP" : "Static");
  if (config.useDHCP) {
    log(LOG_INFO, true, "IP: %s\n", eth.localIP().toString().c_str());
    log(LOG_INFO, true, "Subnet: %s\n", eth.subnetMask().toString().c_str());
    log(LOG_INFO, true, "Gateway: %s\n", eth.gatewayIP().toString().c_str());
    log(LOG_INFO, true, "DNS: %s\n", eth.dnsIP().toString().c_str());
  } else {
    log(LOG_INFO, true, "IP: %s\n", config.ip.toString().c_str());
    log(LOG_INFO, true, "Subnet: %s\n", config.subnet.toString().c_str());
    log(LOG_INFO, true, "Gateway: %s\n", config.gateway.toString().c_str());
    log(LOG_INFO, true, "DNS: %s\n", config.dns.toString().c_str());
  }
  log(LOG_INFO, true, "Timezone: %s\n", config.timezone);
  log(LOG_INFO, true, "Hostname: %s\n", config.hostname);
  log(LOG_INFO, true, "NTP Server: %s\n", config.ntpServer);
  log(LOG_INFO, true, "NTP Enabled: %s\n", config.ntpEnabled ? "true" : "false");
  log(LOG_INFO, true, "DST Enabled: %s\n", config.dstEnabled ? "true" : "false");
}