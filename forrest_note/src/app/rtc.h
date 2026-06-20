#pragma once
#include <time.h>

#define PCF85063_REG_SECONDS  0x04
#define PCF85063_REG_MINUTES  0x05
#define PCF85063_REG_HOURS    0x06
#define PCF85063_REG_DAYS     0x07
#define PCF85063_REG_WEEKDAYS 0x08
#define PCF85063_REG_MONTHS   0x09
#define PCF85063_REG_YEARS    0x0A

uint8_t bcdToDec(uint8_t v);
uint8_t decToBcd(uint8_t v);

bool rtcReadRegs(uint8_t reg, uint8_t* buf, uint8_t len);
bool rtcWriteRegs(uint8_t reg, uint8_t* buf, uint8_t len);

bool    rtcReadUtcTm(struct tm* out);
bool    rtcWriteUtcTm(const struct tm& utc);
time_t  utcTmToEpoch(struct tm utc);

bool    rtcSyncSystemFromChip();
bool    rtcSyncChipFromSystem();
String  rtcUtcIso();

bool    syncTimeFromNTP(uint32_t timeoutMs = 8000);
