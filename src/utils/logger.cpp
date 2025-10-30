#include "logger.h"

// Critical section for controlling access to Serial
bool serialBusy = false;
bool serialReady = false;
bool serialLocked = false;

// Log entry types
const char *logType[] = {"INFO", "WARNING", "ERROR", "DEBUG"};

void init_logger(void) {
    Serial.begin(115200);
    uint32_t terminalTimout = millis() + 5000;
    while (!Serial) {
        if (millis() > terminalTimout) {
            break;
        }
    }
    serialReady = true;
    log(LOG_INFO, false, "Modbus IO Control System v%s\n", VERSION);
    log(LOG_INFO, false, "Starting system...\n");
}

void log(uint8_t logLevel, bool logToSD, const char* format, ...) {
    // Create a buffer to store the output
    static char buffer[DEBUG_PRINTF_BUFFER_SIZE];
      
    // Prepare the log level string.
    const char* logLevelStr = (logLevel < sizeof(logType) / sizeof(logType[0])) ? logType[logLevel] : "UNKNOWN";
    
    // Format the string into the buffer.
    va_list args;
    va_start(args, format);
    int len = snprintf(buffer, DEBUG_PRINTF_BUFFER_SIZE, "[%s] ", logLevelStr); // Add log level pretext to buffer.

    if (len < DEBUG_PRINTF_BUFFER_SIZE) {
        len += vsnprintf(buffer + len, DEBUG_PRINTF_BUFFER_SIZE - len, format, args);
    } else {
        len = DEBUG_PRINTF_BUFFER_SIZE; //Ensure we don't write past buffer
    }

    va_end(args);
    
    if(len > 0) {
        if (logToSD) writeLog(buffer);
        if (!serialLocked) {
            serialLocked = true;
            Serial.print(buffer);
            serialLocked = false;
        }
    }
}