#include "Arduino.h"
#include "../../globals.h"
#include "../../types.h"
#include "rtc.h"
#include "WiFi.h"
#include <time.h>
#include <sys/time.h>

extern "C" {
#include "../../src/i2c_bsp/i2c_bsp.h"
}

uint8_t bcdToDec(uint8_t v) { return ((v >> 4) * 10) + (v & 0x0F); }
uint8_t decToBcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

bool rtcReadRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
  if (!rtc_dev_handle) return false;
  return i2c_read_buff(rtc_dev_handle, reg, buf, len) == 0;
}

bool rtcWriteRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
  if (!rtc_dev_handle) return false;
  return i2c_write_buff(rtc_dev_handle, reg, buf, len) == 0;
}

bool rtcReadUtcTm(struct tm* out) {
  if (!out) return false;
  uint8_t data[7] = {0};
  if (!rtcReadRegs(PCF85063_REG_SECONDS, data, 7)) return false;

  // OS flag: oscillator stopped
  if (data[0] & 0x80) return false;

  int sec   = bcdToDec(data[0] & 0x7F);
  int min   = bcdToDec(data[1] & 0x7F);
  int hour  = bcdToDec(data[2] & 0x3F);
  int day   = bcdToDec(data[3] & 0x3F);
  int wday  = bcdToDec(data[4] & 0x07);
  int month = bcdToDec(data[5] & 0x1F);
  int year  = bcdToDec(data[6]) + 2000;

  if (year < 2024 || year > 2099) return false;
  if (month < 1 || month > 12)    return false;
  if (day < 1   || day > 31)      return false;
  if (hour > 23 || min > 59 || sec > 59) return false;

  memset(out, 0, sizeof(struct tm));
  out->tm_year = year - 1900;
  out->tm_mon  = month - 1;
  out->tm_mday = day;
  out->tm_hour = hour;
  out->tm_min  = min;
  out->tm_sec  = sec;
  out->tm_wday = wday;
  return true;
}

bool rtcWriteUtcTm(const struct tm& utc) {
  uint8_t data[7];
  int year = utc.tm_year + 1900;
  if (year < 2000 || year > 2099) return false;
  data[0] = decToBcd(utc.tm_sec);
  data[1] = decToBcd(utc.tm_min);
  data[2] = decToBcd(utc.tm_hour);
  data[3] = decToBcd(utc.tm_mday);
  data[4] = decToBcd(utc.tm_wday);
  data[5] = decToBcd(utc.tm_mon + 1);
  data[6] = decToBcd(year - 2000);
  return rtcWriteRegs(PCF85063_REG_SECONDS, data, 7);
}

time_t utcTmToEpoch(struct tm utc) {
  setenv("TZ", "UTC0", 1);
  tzset();
  return mktime(&utc);
}

bool rtcSyncSystemFromChip() {
  struct tm utc;
  if (!rtcReadUtcTm(&utc)) { timeReady = false; return false; }
  time_t epoch = utcTmToEpoch(utc);
  if (epoch < 1700000000) { timeReady = false; return false; }
  timeval tv; tv.tv_sec = epoch; tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  timeReady = true;
  return true;
}

bool rtcSyncChipFromSystem() {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;
  struct tm utc;
  gmtime_r(&now, &utc);
  bool ok = rtcWriteUtcTm(utc);
  if (ok) timeReady = true;
  return ok;
}

String rtcUtcIso() {
  struct tm utc;
  if (!rtcReadUtcTm(&utc)) return "";
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return String(buf);
}

bool syncTimeFromNTP(uint32_t timeoutMs) {
  if (WiFi.status() != WL_CONNECTED) return false;
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  uint32_t start = millis();
  struct tm timeinfo;
  while (millis() - start < timeoutMs) {
    if (getLocalTime(&timeinfo, 500)) {
      time_t now = time(nullptr);
      if (now > 1700000000) {
        timeReady = true;
        rtcSyncChipFromSystem();
        return true;
      }
    }
    delay(100);
  }
  return false;
}
