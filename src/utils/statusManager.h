/* Description: Holds the global status struct and LED manager functions
 * Call manageStatus() in the main loop frequently to keep the LEDs updated
 * Use the status struct to update the status of the system from other functions,
 * ensure that the status struct is only accessed after checking the statusLocked flag.
 * Set statusLocked to true before updating the status struct and set it to false after updating.
 * Set status.updated to true after updating the status struct if LED colours need to change.
 */

#pragma once

#include "../sys_init.h"

// LED colours
#define LED_COLOR_GREEN 0x00FF00
#define LED_COLOR_YELLOW 0xFFFF00
#define LED_COLOR_RED 0xFF0000
#define LED_COLOR_BLUE 0x0000FF
#define LED_COLOR_WHITE 0xFFFFFF
#define LED_COLOR_OFF 0x000000
#define LED_COLOR_PURPLE 0x8800FF
#define LED_COLOR_CYAN 0x00FFFF
#define LED_COLOR_ORANGE 0xFFA500
#define LED_COLOR_PINK 0xFFC0CB
#define LED_COLOR_MAGENTA 0xFF00FF

// LED indexes
#define LED_SYSTEM_STATUS 0
#define LED_RS485_STATUS 1
#define LED_CHANNEL_1 2
#define LED_CHANNEL_12 13
#define TOTAL_LEDS 14

// LED status numbers
#define STATUS_STARTUP 0
#define STATUS_OK 1
#define STATUS_ERROR 2
#define STATUS_WARNING 3
#define STATUS_BUSY 4

// LED status colors
#define LED_STATUS_STARTUP LED_COLOR_YELLOW
#define LED_STATUS_OK LED_COLOR_GREEN
#define LED_STATUS_ERROR LED_COLOR_RED
#define LED_STATUS_WARNING LED_COLOR_ORANGE
#define LED_STATUS_BUSY LED_COLOR_BLUE
#define LED_STATUS_OFF LED_COLOR_OFF

#define LED_UPDATE_PERIOD 100
#define LED_BLINK_PERIOD 500

void init_statusManager(void);
void manageStatus(void);
void updateChannelLEDs(void);

struct StatusVariables
{
    bool updated;
    // System status variables
    uint32_t ledPulseTS;
    uint32_t LEDcolour[2]; // 0 = System, 1 = RS485 bus
    bool sdCardOK;

    // Modbus status variables
    bool modbusConnected;
    bool modbusBusy;

    // Webserver status variables
    bool webserverUp;
    bool webserverBusy;
};

// Object definition
extern Adafruit_NeoPixel leds;

// Status variables
extern StatusVariables status;
extern bool statusLocked;