#include "sdManager.h"

SdFs sd;
FsFile file;

sdInfo_t sdInfo;
uint32_t sdTS;
volatile bool sdLocked = false;

void init_sdManager(void) {
    SPI1.setMISO(PIN_SD_MISO);
    SPI1.setMOSI(PIN_SD_MOSI);
    SPI1.setSCK(PIN_SD_SCK);
    
    FsDateTime::setCallback(dateTimeCallback);
    
    sdTS = millis();
    log(LOG_INFO, false, "SD card manager initialised\n");
}

void manageSD(void) {
    if (millis() - sdTS < SD_MANAGE_INTERVAL) return;
    sdTS = millis();
    
    if (!sdInfo.ready && !digitalRead(PIN_SD_CD)) {
        mountSD();
    } else {
        maintainSD();
    }
    
    // Every 10 minutes, update SD info for the status display
    static uint32_t sdInfoTS = 0;
    if (sdInfo.ready && (millis() - sdInfoTS > 600000)) {
        sdInfoTS = millis();
        printSDInfo();
    }
}

void mountSD(void) {
    // Check if SD card is inserted
    if (digitalRead(PIN_SD_CD)) {
        log(LOG_WARNING, false,"SD card not inserted\n");
        if (sdLocked) return;
        sdLocked = true;
        sdInfo.inserted = false;
        sdInfo.ready = false;
        if (!statusLocked) {
            statusLocked = true;
            status.sdCardOK = false;
            status.updated = true;
            statusLocked = false;
        }
        sdLocked = false;
        return;
    }
    // Mount SD card
    bool sdSPIinitialised = false;
    bool sdSDIOinitialised = false;
    if (sdLocked) return;
    sdLocked = true;
    sdInfo.inserted = true;
    log(LOG_INFO, false, "SD card inserted, mounting FS\n");
    if (!sd.begin(SDIO_CONFIG)) {
        log(LOG_ERROR, false, "Attempt 1 failed, retrying\n");
        delay(100);
        if (!sd.begin(SDIO_CONFIG)) {
            log(LOG_ERROR, false, "SD card initialisation with SDIO config failed, attempting SPI config\n");
            if (!sd.begin(SdSpiConfig(PIN_SD_CS, DEDICATED_SPI, SD_SCK_MHZ(40), &SPI1))) {
                if (sd.card()->errorCode()) {
                    log(LOG_ERROR, false, "SD card initialisation failed with error code %d\n", sd.card()->errorCode());
                }
            } else sdSPIinitialised = true;
        }
    } else sdSDIOinitialised = true;
    if (sdSPIinitialised || sdSDIOinitialised) {
        log(LOG_INFO, true, "SD card initialisation successful, using %s\n", sdSPIinitialised ? "SPI" : "SDIO");
        // Check for correct folder structure and create if missing
        if (!sd.exists("/sensors")) sd.mkdir("/sensors");
        if (!sd.exists("/logs")) sd.mkdir("/logs");
        // Check for log files and create if missing
        if (!sd.exists("/logs/system.txt")) {
            FsFile logFile = sd.open("/logs/system.txt", O_CREAT | O_WRITE);
            logFile.close();
        }
        sdInfo.ready = true;
    }
    if (sdInfo.ready) log(LOG_INFO, true, "SD card mounted and ready\n");
    if (!statusLocked) {
        statusLocked = true;
        status.sdCardOK = true;
        status.updated = true;
        statusLocked = false;
    }
    sdLocked = false;
    printSDInfo();
}

void maintainSD(void) {
    // Just check if the SD card is still inserted
    if (sdLocked) return;
    sdLocked = true;
    if (digitalRead(PIN_SD_CD) && sdInfo.inserted) {
        log(LOG_WARNING, false, "SD card removed\n");
        sdInfo.inserted = false;
        sdInfo.ready = false;
        if (!statusLocked) {
            statusLocked = true;
            status.sdCardOK = false;
            status.updated = true;
            statusLocked = false;
        }
    }
    sdLocked = false;
}

uint64_t getFileSize(const char* path) {
    FsFile file;
    uint64_t size = 0;
    if (sdLocked) return 0;
    sdLocked = true;
    if (sd.exists(path)) {
        if (file.open(path, O_RDONLY)) {
            size = file.fileSize();
            file.close();
        }
    }
    sdLocked = false;
    return size;
}

void printSDInfo(void) {
    if (sdLocked) return;
    sdLocked = true;
    if (!sdInfo.ready) {
        if (digitalRead(PIN_SD_CD)) log(LOG_INFO, false, "SD card not inserted\n");
        else log(LOG_INFO, false, "SD card not ready\n");
        sdLocked = false;
        return;
    }

    sdInfo.cardSizeBytes = (uint64_t)sd.card()->sectorCount() * 512;
    sdInfo.cardFreeBytes = (uint64_t)sd.vol()->bytesPerCluster() * (uint64_t)sd.freeClusterCount();
    sdLocked = false;
    uint64_t logFileSize = getFileSize("/logs/system.txt");
    uint64_t sensorFileSize = getFileSize("/sensors/sensors.csv");
    sdLocked = true;
    sdInfo.logSizeBytes = logFileSize;
    sdInfo.sensorSizeBytes = sensorFileSize;
    
    log(LOG_INFO, false, "SD card size: %0.1f GB\n", sdInfo.cardSizeBytes * 0.000000001);
    log(LOG_INFO, false, "Free space: %0.1f GB\n", sdInfo.cardFreeBytes * 0.000000001);
    log(LOG_INFO, false, "Volume is FAT%d\n", sd.vol()->fatType());
    log(LOG_INFO, false, "Log file size: %0.1f kbytes\n", 0.001 * (float)logFileSize);
    
    sdLocked = false;
}

void dateTimeCallback(uint16_t* date, uint16_t* time) {
    // Gateway has no RTC - use default timestamp
    // Files will have creation time based on system start
    *date = FS_DATE(2025, 1, 1);
    *time = FS_TIME(0, 0, 0);
}

bool writeLog(const char *message) {
    if (sdLocked) return false;
    sdLocked = true;
    if (!sdInfo.ready) {
        sdLocked = false;
        return false;
    }
    sdLocked = false;
    
    // Use uptime instead of RTC timestamp
    uint32_t uptime = millis() / 1000;
    char dateTimeStr[20];
    snprintf(dateTimeStr, sizeof(dateTimeStr), "[%lu]", uptime);

    char buf[strlen(dateTimeStr) + strlen(message) + 10];
    snprintf(buf, sizeof(buf), "[%s]\t\t%s", dateTimeStr, message);

    // Log file size check
    uint64_t logFileSize = getFileSize("/logs/system.txt");
    sdLocked = true;
    sdInfo.logSizeBytes = logFileSize;
    if (logFileSize > SD_LOG_MAX_SIZE) {
        // Rename the existing log file and create a new one
        uint32_t uptime = millis() / 1000;
        char fNameBuf[50];
        snprintf(fNameBuf, sizeof(fNameBuf), "/logs/system-log-archive-%lu", uptime);
        
        if (!sd.exists(fNameBuf)) {
            for (int i = 0; i < 100; i++) {
                char tempBuf[50];
                snprintf(tempBuf, sizeof(tempBuf), "%s-%d.txt", fNameBuf, i);
                if (!sd.exists(tempBuf)) {
                        strcpy(fNameBuf, tempBuf);
                        break;
                    }
                }
            }
            if (sd.exists("/logs/system.txt")) {
                sd.rename("/logs/system.txt", fNameBuf);
            }
            file = sd.open("/logs/system.txt", O_CREAT | O_RDWR | O_APPEND);
            if (file) {
                file.print(buf);
                file.close();
            }
        sdLocked = false;
        return true;
    }
    // Otherwise just write to the existing log file
    file = sd.open("/logs/system.txt", O_CREAT | O_RDWR | O_APPEND);
    if (file) {
        file.print(buf);
        file.close();
    }
    sdLocked = false;
    return true;
}

bool writeSensorData(const char* data, const char* fileName, bool isHeader) {
    if (sdLocked) return false;
    sdLocked = true;
    if (!sdInfo.ready) {
        sdLocked = false;
        return false;
    }
    sdLocked = false;

    // Note: Flow counter provides timestamp, so we don't prepend our own
    char buf[500];

    // Check if data is header
    if (isHeader) snprintf(buf, sizeof(buf), "%s", data);        
    else snprintf(buf, sizeof(buf), "%s", data);  // Data already has timestamp from flow counter

    char fileNameBuf[100];
    snprintf(fileNameBuf, sizeof(fileNameBuf), "%s", fileName);  // fileName already has full path

    // Log file size check
    uint64_t fileSize = getFileSize(fileNameBuf);
    sdLocked = true;
    sdInfo.sensorSizeBytes = fileSize;
    if (fileSize > SD_LOG_MAX_SIZE) {
        // Rename the existing sensor file and create a new one
        uint32_t uptime = millis() / 1000;
        char fNameBuf[100];
        snprintf(fNameBuf, sizeof(fNameBuf), "%s-archive-%lu", fileName, uptime);
        
        if (!sd.exists(fNameBuf)) {
            for (int i = 0; i < 100; i++) {
                char tempBuf[100];
                snprintf(tempBuf, sizeof(tempBuf), "%s-%d.csv", fNameBuf, i);
                if (!sd.exists(tempBuf)) {
                        strcpy(fNameBuf, tempBuf);
                        break;
                    }
                }
            }
            if (sd.exists(fileNameBuf)) {
                sd.rename(fileNameBuf, fNameBuf);
            }
            file = sd.open(fileNameBuf, O_CREAT | O_RDWR | O_APPEND);
            if (file) {
                file.print(buf);
                file.close();
            }
        sdLocked = false;
        return true;
    }
    // Otherwise just write to the existing log file
    file = sd.open(fileNameBuf, O_CREAT | O_RDWR | O_APPEND);
    if (file) {
        file.print(buf);
        file.close();
    }
    sdLocked = false;
    return true;
}