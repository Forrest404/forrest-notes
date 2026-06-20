#pragma once

void  batteryInit();
float readBatteryVoltage();
int   batteryPercentFromVoltage(float v);
int   readBatteryPercent();
bool  isBatteryCharging();
void  drawThickArcDot(int cx, int cy, int r, int deg, int thickness, uint8_t color);
void  drawBatteryRing(int percent);
