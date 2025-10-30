#include "sys_init.h"

// Object definitions

bool core0setupComplete = false;
bool core1setupComplete = false;

// Ensure both cores have an 8k stack size (default is to split 8k accross both cores)
bool core1_separate_stack = true;

bool debug = true;

// Millis rollover tracking (millis() rolls over every ~49.7 days)
volatile uint32_t millisRolloverCount = 0;
static uint32_t lastMillis = 0;

void init_core0(void);

void init_core1(void);

void init_core0(void) {
    init_logger();
    init_gatewayConfig();
    init_network();
    setupWebServer(); // Setup the web server routes but don't start it yet
}

void init_core1(void) {
    init_statusManager();
    init_terminalManager();
    while (!core0setupComplete) delay(100); // Wait for core0 setup to complete
    init_sdManager();
    init_flowCounterManager();
    startWebServer();       // Now start the web server after all APIs are registered
}

void manage_core0(void) {
    manageNetwork();
}

void manage_core1(void) {
    // Track millis rollover
    uint32_t currentMillis = millis();
    if (currentMillis < lastMillis) {
        millisRolloverCount++;
    }
    lastMillis = currentMillis;
    
    manageStatus();
    manageTerminal();
    manageSD();
    manage_flowCounterManager();
}
