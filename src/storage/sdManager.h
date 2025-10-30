#pragma once

#include "../sys_init.h"

#define DISABLE_FS_H_WARNING
#include <SdFat.h>

#define SD_FAT_TYPE 3
#define SPI_SPEED SD_SCK_MHZ(4)
#define SDIO_CONFIG SdioConfig(PIN_SDIO_CLK, PIN_SDIO_CMD, PIN_SDIO_D0)

#define SD_LOG_MAX_SIZE 1000000        //1MB max size
#define SD_SENSOR_MAX_SIZE 1000000     //1MB max size

#define SD_MANAGE_INTERVAL 1000

void init_sdManager(void);
void manageSD(void);
void mountSD(void);
void maintainSD(void);
void printSDInfo(void);
uint64_t getFileSize(const char* path);
void dateTimeCallback(uint16_t* date, uint16_t* time);
bool writeLog(const char *message);
bool writeSensorData(const char* data, const char* fileName, bool isHeader);

struct sdInfo_t {
  bool inserted;
  bool ready;
  uint64_t cardSizeBytes;
  uint64_t cardFreeBytes;
  uint64_t logSizeBytes;
  uint64_t sensorSizeBytes;
};

extern SdFs sd;
extern volatile bool sdLocked;
extern sdInfo_t sdInfo;
extern uint32_t sdTS;
