#include "terminalManager.h"

bool terminalReady = false;

void init_terminalManager(void) {
  while (!serialReady) {
    delay(10);
  }
  terminalReady = true;
  log(LOG_INFO, false, "Terminal task started\n");
}

void manageTerminal(void)
{
  if (serialLocked || !terminalReady) return;
  serialLocked = true;
  if (Serial.available())
  {
    char serialString[10];  // Buffer for incoming serial data
    memset(serialString, 0, sizeof(serialString));
    int bytesRead = Serial.readBytesUntil('\n', serialString, sizeof(serialString) - 1); // Leave room for null terminator
    serialLocked = false;
    if (bytesRead > 0 ) {
      serialString[bytesRead] = '\0'; // Add null terminator
      log(LOG_INFO, true,"Received:  %s\n", serialString);

      // Reboot ---------------------------------------------->
      if (strcmp(serialString, "reboot") == 0) {
        log(LOG_INFO, true, "Rebooting now...\n");
        rp2040.restart();
      }

      // IP Address ------------------------------------------>
      else if (strcmp(serialString, "ip") == 0) {
        printNetConfig(networkConfig);
      }

      // IP Static Assign Temp-------------------------------->
      else if (strcmp(serialString, "ipstatic") == 0) {
        log(LOG_INFO, false, "Assigning static IP address...\n");
        setStaticIPcmd = true;
      }

      // IP Static Assign Temp-------------------------------->
      else if (strcmp(serialString, "ipdhcp") == 0) {
        log(LOG_INFO, false, "Assigning DHCP...\n");
        setDHCPcmd = true;
      }

      // SD Card --------------------------------------------->
      else if (strcmp(serialString, "sd") == 0) {
        log(LOG_INFO, false, "Getting SD card info...\n");
        printSDInfo();
      }

      // Status ---------------------------------------------->
      else if (strcmp(serialString, "status") == 0) {
        log(LOG_INFO, false, "Getting status...\n");
        if (statusLocked) {
          log(LOG_INFO, false, "Status is locked\n");
        } else {
          statusLocked = true;
          log(LOG_INFO, false, "SD Card status: %s\n", status.sdCardOK ? "OK" : "ERROR");
          log(LOG_INFO, false, "Modbus status: %s\n", status.modbusConnected ? "CONNECTED" : "DOWN");
          log(LOG_INFO, false, "Webserver status: %s\n", status.webserverUp ? "OK" : "DOWN");
          statusLocked = false;
        }
      }

      // Gateway configuration ---------------------------------->
      else if (strcmp(serialString, "config") == 0) {
        log(LOG_INFO, false, "Gateway configuration\n");
        log(LOG_INFO, false, "RS485: %d baud, timeout %dms\n", gatewayConfig.rs485.baudRate, gatewayConfig.rs485.responseTimeout);
        for (int i = 0; i < MAX_FLOW_COUNTERS; i++) {
          if (gatewayConfig.ports[i].enabled) {
            log(LOG_INFO, false, "Port %d: %s (Slave ID: %d)\n", i+1, gatewayConfig.ports[i].portName, gatewayConfig.ports[i].slaveId);
          }
        }
      }
      else {
        log(LOG_INFO, false, "Unknown command: %s\n", serialString);
        log(LOG_INFO, false, "Available commands: \n\t- ip \t\t(print IP address)\n\t- ipstatic \t(assign 192.168.1.100)\n\t- ipdhcp \t(assign DHCP)\n\t- sd \t\t(print SD card info)\n\t- status \t(print system status)\n\t- config \t(print gateway configuration)\n\t- reboot \t(reboot system)\n");
      }
    }
    // Clear the serial buffer each loop.
    serialLocked = true;
    while(Serial.available()) Serial.read();
    serialLocked = false;
  }
  serialLocked = false;
}
