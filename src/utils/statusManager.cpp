#include "statusManager.h"
#include "../gateway/flowCounterConfig.h"
#include "../gateway/flowCounterManager.h"

// Object definition
Adafruit_NeoPixel leds(TOTAL_LEDS, PIN_LED_DAT, NEO_GRB + NEO_KHZ800);

// Status variables
StatusVariables status;
bool statusLocked = false;
static bool blinkState = false;
static uint32_t ledTS = 0;

void init_statusManager() {
  leds.begin();
  leds.setBrightness(50);
  leds.fill(LED_COLOR_OFF, 0, TOTAL_LEDS);
  leds.setPixelColor(LED_SYSTEM_STATUS, LED_STATUS_STARTUP);
  leds.show();
  status.ledPulseTS = millis();
  log(LOG_INFO, false, "Status manager started\n");
  ledTS = millis();
}

void manageStatus(void)
{
  if (millis() - ledTS < LED_UPDATE_PERIOD) return;
  if (statusLocked) return;
  statusLocked = true;
  
  // Check for status change and update LED colours accordingly
  if (status.updated) {
    // System status LED colours
    if (!status.sdCardOK) {
      status.LEDcolour[LED_SYSTEM_STATUS] = LED_STATUS_WARNING;
    } else {
      status.LEDcolour[LED_SYSTEM_STATUS] = LED_STATUS_OK;
    }
    
    // RS485 status LED colours
    // Check if any channel has a pending Modbus request
    bool anyChannelActive = false;
    for (int i = 0; i < MAX_FLOW_COUNTERS; i++) {
      if (flowCounterData[i].modbusRequestPending) {
        anyChannelActive = true;
        break;
      }
    }
    
    if (anyChannelActive) {
      // Any channel is being queried - cyan
      status.LEDcolour[LED_RS485_STATUS] = LED_COLOR_CYAN;
    } else if (status.modbusBusy) {
      status.LEDcolour[LED_RS485_STATUS] = LED_STATUS_BUSY;
    } else if (status.modbusConnected) {
      status.LEDcolour[LED_RS485_STATUS] = LED_STATUS_OK;
    } else {
      status.LEDcolour[LED_RS485_STATUS] = LED_STATUS_OFF;
    }
    leds.setPixelColor(LED_RS485_STATUS, status.LEDcolour[LED_RS485_STATUS]);
    
    // Update channel LEDs
    updateChannelLEDs();
    
    // Reset the update flag
    status.updated = false;
  }  
  
  // Status LED blink updater  
  if (millis() - status.ledPulseTS >= LED_BLINK_PERIOD) {
    blinkState = !blinkState;
    status.ledPulseTS += LED_BLINK_PERIOD;
    if (blinkState) {
      leds.setPixelColor(LED_SYSTEM_STATUS, status.LEDcolour[LED_SYSTEM_STATUS]);
    } else {
      leds.setPixelColor(LED_SYSTEM_STATUS, LED_COLOR_OFF);
    }
    leds.show();
  }
  statusLocked = false;
}

// Update channel LEDs based on flow counter status
void updateChannelLEDs(void) {
  for (int i = 0; i < MAX_FLOW_COUNTERS; i++) {
    uint8_t ledIndex = LED_CHANNEL_1 + i;
    
    if (!gatewayConfig.ports[i].enabled) {
      // Port not configured - LED off
      leds.setPixelColor(ledIndex, LED_COLOR_OFF);
    } else if (flowCounterData[i].modbusRequestPending) {
      // Modbus request pending - cyan (highest priority for active communication)
      leds.setPixelColor(ledIndex, LED_COLOR_CYAN);
    } else if (flowCounterData[i].commError) {
      // Communication error - red
      leds.setPixelColor(ledIndex, LED_COLOR_RED);
    } else if (triggerStates[i]) {
      // Trigger active (GPIO is LOW) - blue
      leds.setPixelColor(ledIndex, LED_COLOR_BLUE);
    } else if (flowCounterData[i].dataValid) {
      // Data valid - green
      leds.setPixelColor(ledIndex, LED_COLOR_GREEN);
    } else {
      // Configured but no data yet - bright purple
      leds.setPixelColor(ledIndex, LED_COLOR_PURPLE);
    }
  }
}