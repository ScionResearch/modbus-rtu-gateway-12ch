#pragma once

#include "../sys_init.h"

// Buffer sizes
#define DEBUG_PRINTF_BUFFER_SIZE 500

// Log entry types
#define LOG_INFO 0
#define LOG_WARNING 1
#define LOG_ERROR 2
#define LOG_DEBUG 3

void init_logger(void);

// Debug functions
void log(uint8_t logLevel, bool logToSD,const char* format, ...);

// Serial port mutex

extern bool serialReady;
extern bool serialLocked;
