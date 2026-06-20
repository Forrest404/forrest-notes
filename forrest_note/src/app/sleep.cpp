#include "Arduino.h"
#include "../../config.h"
#include "../../globals.h"
#include "../../types.h"
#include "sleep.h"
#include "ui.h"
#include "network.h"
#include "../../sounds.h"
#include "WiFi.h"

extern "C" {
#include "../../src/audio/audio_bsp.h"
}

void resetActivity() {
  lastActivityMs = millis();
}

void enterUltraSleep() {
  showUltraSleepScreen();   // final image; refresh completes (read_busy) before we continue
  delay(120);

  stopTransferMode();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  audio_playback_set_vol(0);
  palaSoundSetEnabled(false);

  // Deep-sleep the panel controller (image is retained on e-paper), then cut the
  // display and audio power rails so they draw nothing during MCU deep sleep.
  if (display) display->EPD_Sleep();
  board.POWEER_Audio_OFF();
  board.POWEER_EPD_OFF();

  // Keep the battery power latch engaged so the RTC domain stays alive to wake
  // us on a button press.
  board.VBAT_POWER_ON();

  uint64_t wakeMask = (1ULL << BTN_REC) | (1ULL << BTN_PWR);
  esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);

  delay(50);
  esp_deep_sleep_start();
}
