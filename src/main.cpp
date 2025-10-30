#include "sys_init.h"

// Timing variables for cooperative multitasking
unsigned long lastSDManagerTime = 0;
unsigned long lastLEDManagerTime = 0;
unsigned long lastTimeManagerTime = 0;
unsigned long lastPowerManagerTime = 0;
unsigned long lastTerminalTime = 0;
unsigned long lastNetworkTime = 0;
unsigned long lastIpcManagerTime = 0;

void setup() // Eth interface (keep hardware-specific initialization on core 0)
{
  init_core0(); // All core 0 initialisation in this function

  log(LOG_INFO, false, "Core 0 setup complete\n");
  core0setupComplete = true;
  while (!core1setupComplete) delay(100);
}

void setup1()
{
  while (!serialReady) delay(100);
  log(LOG_INFO, false, "Core 1 setup started\n");
  init_core1(); // All core 1 initialisation in this function
  log(LOG_INFO, false, "Core 1 setup complete\n");
  core1setupComplete = true;
  while (!core0setupComplete) delay(100);
  if (!sdInfo.inserted) return;
  while (!sdInfo.ready || sdLocked) delay(100);
  log(LOG_INFO, true, "---------> System started successfully <---------\n");
}

// Core 0 - network and coordination
void loop()
{
  manage_core0();
}

// Core 1 - all other subsystems
void loop1() {
  manage_core1();
}