#include "Arduino.h"
#include "SD_MMC.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include "WiFiClientSecure.h"
#include <WebServer.h>
#include <vector>
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"

extern "C" {
#include "config.h"
#include "src/i2c_bsp/i2c_bsp.h"
#include "src/audio/audio_bsp.h"
}

#include "src/power/board_power_bsp.h"
#include "src/display/epaper_driver_bsp.h"
#include "logo_bitmap.h"
#include "secrets.h"
#include "sounds.h"

#include <Adafruit_GFX.h>
#include <pgmspace.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>

#include "types.h"
#include "globals.h"
#include "src/app/draw.h"
#include "src/app/battery.h"
#include "src/app/rtc.h"
#include "src/app/notes.h"
#include "src/app/ui.h"
#include "src/app/buttons.h"
#include "src/app/network.h"
#include "src/app/sleep.h"
#include "src/app/record.h"
#include "src/app/config_store.h"
#include "src/app/obsidian.h"

// All pin, timing, path and threshold constants live in config.h.

// ─── Content arrays ───────────────────────────────────────────────────────
const char* DEFAULT_TAGS[]    = { "Note", "Work", "Idea", "Buy", "Private" };
const char* MENU_ITEMS[]     = { "Notes", "Tags", "Sync", "Settings" };
const char* SETTINGS_ITEMS[] = { "Sounds", "Transfer", "Device", "Reset" };

// ─── Global variable definitions ─────────────────────────────────────────
board_power_bsp_t      board(EPD_PWR_PIN, Audio_PWR_PIN, VBAT_PWR_PIN);
epaper_driver_display* display = nullptr;

std::vector<NoteEntry> noteIndex;

AppState state          = STATE_IDLE;
int      listCursor     = 0;
int      tagCursor      = 2;
int      menuCursor     = 0;
int      settingsCursor = 0;
int      activeFilter   = -1;
int      lastRecNum     = -1;

uint32_t lastActivityMs      = 0;
bool     wokeFromUltraSleep  = false;
bool     wakeToMenuRequested = false;
bool     wakeToRecRequested  = false;

uint32_t tickerLastMs = 0;
int      tickerOffset = 0;
int      tickerCursor = -1;

WebServer transferServer(80);
bool      transferServerActive = false;
String    transferUrl          = "";
DNSServer dnsServer;
bool      captivePortalActive  = false;

bool timeReady    = false;
bool audioPlaying = false;
bool stopPlayback = false;

int detailScrollPage = 0;
int detailTotalLines = 0;

uint32_t lastBatCheckMs    = 0;
bool     batLowWarned      = false;
bool     batWarnActive     = false;
uint32_t batWarnShowUntilMs = 0;

char tags[20][32];
int  tagCount = 0;

// ─── Power latch ──────────────────────────────────────────────────────────
void keepBatteryPowerOn() {
  pinMode(PWR_HOLD_PIN, OUTPUT);
  digitalWrite(PWR_HOLD_PIN, HIGH);
}

// ─── Flow functions ───────────────────────────────────────────────────────
void startRecordFlow() {
  state = STATE_RECORDING;
  showRecording();

  palaSoundSetEnabled(false);
  bool recOk = record();
  palaSoundSetEnabled(true);

  if (!recOk) {
    showError("REC FAIL");
    delay(1600);
    state = STATE_IDLE;
    showIdle();
    return;
  }

  soundSaved();

  state = STATE_SAVED;
  showSaved(lastRecNum);
  delay(900);

  tagCursor = min(2, max(tagCount - 1, 0));
  state = STATE_TAG_SELECT;
  showTagSelect(tagCursor);
}

void startSyncFlow() {
  if (!cfg::hasWifi()) {
    showError("NO WIFI CFG");
    delay(1800);
    showIdle();
    return;
  }

  const int MAX_TRIES = 20;
  showWifiConnecting(0, MAX_TRIES);

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);        // creds live in our NVS; don't wear the WiFi NVS
  WiFi.setAutoReconnect(true);   // SDK recovers (with backoff) if the link drops mid-sync
  String ssid = cfg::wifiSsid(), pass = cfg::wifiPass();
  WiFi.begin(ssid.c_str(), pass.c_str());
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < MAX_TRIES) {
    delay(500); tries++;
    showWifiConnecting(tries, MAX_TRIES);
  }

  if (WiFi.status() == WL_CONNECTED) {
    syncTimeFromNTP(6000);
    transcribeAll();
    loadIndex();
    obsidianSyncAll();        // push freshly-transcribed notes to the Obsidian vault
    WiFi.disconnect(true);
    showDone();
    soundSuccess();
    delay(1600);
  } else {
    showError("NO WIFI");
    delay(1800);
  }

  if (wakeToMenuRequested) {
    menuCursor = 0;
    state = STATE_MENU;
    showMenu(menuCursor);
  } else {
    showIdle();
  }
}

void startTransferMode() {
  state = STATE_TRANSFER;
  showTransferConnecting();

  // First-time setup: no Wi-Fi credentials yet -> host a SoftAP so the user can
  // open the portal and provision Wi-Fi + OpenAI key (saved to NVS).
  if (!cfg::hasWifi()) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    bool apOk = WiFi.softAP("ForrestNote-Setup");
    delay(200);                                   // let the AP + DHCP server come up
    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("[Transfer] SoftAP '%s' start=%d ip=%s\n",
                  "ForrestNote-Setup", apOk, apIP.toString().c_str());

    // Captive portal: resolve every DNS query to us so any URL (and the OS's
    // own connectivity probe) lands on the device and pops the setup page.
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", apIP);
    captivePortalActive = true;

    setupTransferServer();
    transferServer.begin();
    transferServerActive = true;
    transferUrl = apIP.toString();
    Serial.println("[Transfer] HTTP server started on :80");
    showTransferMode(transferUrl.c_str());
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  String ssid = cfg::wifiSsid(), pass = cfg::wifiPass();
  WiFi.begin(ssid.c_str(), pass.c_str());

  const int MAX_TRIES = 24;
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < MAX_TRIES) {
    delay(500); tries++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    showError("NO WIFI");
    delay(1600);
    state = STATE_SETTINGS;
    showSettings(settingsCursor);
    return;
  }

  syncTimeFromNTP(8000);
  setupTransferServer();
  transferServer.begin();
  transferServerActive = true;

  IPAddress ip = WiFi.localIP();
  transferUrl = ip.toString();
  showTransferMode(transferUrl.c_str());
}

// ─── Setup ────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Forrest Note " FIRMWARE_VERSION " ===");

  cfg::begin();   // load Wi-Fi / API secrets from NVS (seeded from secrets.h once)

  pinMode(BTN_REC, INPUT_PULLUP);
  pinMode(BTN_PWR, INPUT_PULLUP);

  board.VBAT_POWER_ON();

  wokeFromUltraSleep  = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1);
  delay(50);

  wakeToMenuRequested = (wokeFromUltraSleep && digitalRead(BTN_PWR) == LOW);
  wakeToRecRequested  = (wokeFromUltraSleep && digitalRead(BTN_REC) == LOW);

  resetActivity();
  keepBatteryPowerOn();
  delay(20);

  board.POWEER_EPD_ON();
  board.POWEER_Audio_ON();
  delay(200);

  custom_lcd_spi_t dispCfg = {};
  dispCfg.cs       = EPD_CS_PIN;
  dispCfg.dc       = EPD_DC_PIN;
  dispCfg.rst      = EPD_RST_PIN;
  dispCfg.busy     = EPD_BUSY_PIN;
  dispCfg.mosi     = EPD_MOSI_PIN;
  dispCfg.scl      = EPD_SCK_PIN;
  dispCfg.spi_host = EPD_SPI_NUM;
  dispCfg.buffer_len = (200*200)/8;

  display = new epaper_driver_display(200, 200, dispCfg);
  display->EPD_Init();
  display->EPD_Clear();
  display->EPD_DisplayPartBaseImage();
  display->EPD_Init_Partial();

  i2c_master_Init();
  delay(50);

  audio_bsp_init();
  audio_play_init();

  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  if (!SD_MMC.begin("/sdcard", true)) {
    showError("SD ERR");
    while (true) delay(1000);
  }
  if (!SD_MMC.exists(NOTES_DIR)) SD_MMC.mkdir(NOTES_DIR);
  loadTags();
  loadIndex();
  Serial.printf("[SD] %d notes\n", (int)noteIndex.size());

  if (wakeToMenuRequested) {
    menuCursor = 0;
    state = STATE_MENU;
    showMenu(menuCursor);
  } else if (wakeToRecRequested) {
    startRecordFlow();
  } else {
    showIdle();
  }
}

// ─── Serial provisioning ────────────────────────────────────────────────────
// Reliable setup over USB (no SoftAP/captive-portal needed). Send line commands:
//   SSID=<network>     PASS=<password>     KEY=<openai key>     SHOW     RESET
// Wi-Fi is saved when PASS= follows a SSID=. Credentials are stored in NVS.
void handleSerialConfig() {
  static String line;
  static String pendingSsid;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') {
      line += c;
      if (line.length() > 256) line = "";   // overflow guard
      continue;
    }
    line.trim();
    if (line.length() == 0) { line = ""; continue; }

    if (line.startsWith("SSID=")) {
      pendingSsid = line.substring(5);
      Serial.printf("[cfg] ssid buffered ('%s'); now send PASS=<password>\n", pendingSsid.c_str());
    } else if (line.startsWith("PASS=")) {
      if (pendingSsid.length() > 0) {
        cfg::setWifi(pendingSsid, line.substring(5));
        Serial.printf("[cfg] wifi saved for ssid '%s'\n", pendingSsid.c_str());
        pendingSsid = "";
      } else {
        Serial.println("[cfg] send SSID=<network> first");
      }
    } else if (line.startsWith("KEY=")) {
      cfg::setOpenAiKey(line.substring(4));
      Serial.println("[cfg] openai key saved");
    } else if (line.startsWith("GHTOKEN=")) {
      cfg::setGithubToken(line.substring(8));
      Serial.println("[cfg] github token saved");
    } else if (line.startsWith("GHREPO=")) {
      bool ok = cfg::setGithubRepo(line.substring(7));
      Serial.printf("[cfg] github repo %s\n", ok ? "saved" : "rejected (need owner/name)");
    } else if (line.startsWith("GHBRANCH=")) {
      cfg::setGithubBranch(line.substring(9));
      Serial.println("[cfg] github branch saved");
    } else if (line.startsWith("GHDIR=")) {
      cfg::setGithubDir(line.substring(6));
      Serial.println("[cfg] github dir saved");
    } else if (line == "GHON")  { cfg::setGithubEnabled(true);  Serial.println("[cfg] github sync ON");
    } else if (line == "GHOFF") { cfg::setGithubEnabled(false); Serial.println("[cfg] github sync OFF");
    } else if (line == "SHOW") {
      Serial.printf("[cfg] wifi=%s  openai_key=%s\n",
        cfg::hasWifi() ? cfg::wifiSsid().c_str() : "(none)",
        cfg::hasOpenAiKey() ? "set" : "(none)");
      Serial.printf("[cfg] github=%s branch=%s dir=%s token=%s enabled=%d ai=%d ready=%d\n",
        cfg::githubRepo().length() ? cfg::githubRepo().c_str() : "(none)",
        cfg::githubBranch().c_str(), cfg::githubDir().c_str(),
        cfg::githubToken().length() ? "set" : "(none)",
        cfg::githubEnabled(), cfg::githubAiEnrich(), cfg::hasGithub());
    } else if (line == "RESET") {
      cfg::factoryReset();
      Serial.println("[cfg] factory reset done");
    } else {
      Serial.println("[cfg] cmds: SSID= PASS= KEY= GHREPO= GHBRANCH= GHDIR= GHTOKEN= GHON GHOFF SHOW RESET");
    }
    line = "";
  }
}

// ─── Main loop ────────────────────────────────────────────────────────────
void loop() {
  handleSerialConfig();
  serviceDisplay();      // paint any pending (coalesced) redraw when the panel is free


  if (state != STATE_RECORDING && state != STATE_TRANSFER) {
    if (millis() - lastActivityMs > ULTRA_SLEEP_MS) {
      enterUltraSleep();
      return;
    }
  }

  if (state == STATE_NOTE_LIST && activeTickerNeedsScroll(listCursor)) {
    if (millis() - tickerLastMs > TICKER_INTERVAL_MS) {
      tickerLastMs = millis();
      tickerOffset++;
      showNoteList(listCursor);
      return;
    }
  }

  if (captivePortalActive)  dnsServer.processNextRequest();
  if (transferServerActive) transferServer.handleClient();

  // Battery warning: dismiss after 2.5 s without blocking
  if (batWarnActive && millis() >= batWarnShowUntilMs) {
    batWarnActive = false;
    switch (state) {
      case STATE_IDLE:           showIdle();                     break;
      case STATE_MENU:           showMenu(menuCursor);           break;
      case STATE_NOTE_LIST:      showNoteList(listCursor);       break;
      case STATE_NOTE_DETAIL:    showNoteDetail(listCursor);     break;
      case STATE_TAG_SELECT:     showTagSelect(tagCursor);       break;
      case STATE_TAG_BROWSER:    showTagBrowser(tagCursor);      break;
      case STATE_SETTINGS:       showSettings(settingsCursor);   break;
      case STATE_DEVICE_INFO:    showDeviceInfo();               break;
      case STATE_DELETE_CONFIRM: {
        int idx = noteAtFilteredIndex(listCursor);
        if (idx >= 0) showDeleteConfirm(noteIndex[idx].num);
        break;
      }
      default: break;
    }
  }

  // Periodic battery check
  if (state != STATE_RECORDING && !audioPlaying && !batWarnActive) {
    if (millis() - lastBatCheckMs > BAT_CHECK_INTERVAL_MS) {
      lastBatCheckMs = millis();
      int pct = readBatteryPercent();
      if (pct >= 0 && pct <= BAT_LOW_THRESHOLD && !batLowWarned) {
        batLowWarned        = true;
        batWarnActive       = true;
        batWarnShowUntilMs  = millis() + 2500;
        showBatteryLow(pct);
      } else if (pct > BAT_RECOVER_THRESHOLD) {
        batLowWarned = false;
      }
    }
  }

  // IDLE ─────────────────────────────────────────────────────────────────
  if (state == STATE_IDLE) {
    if (handleIdleRec()) return;

    ButtonEvent pwr = readButtonEvent(BTN_PWR);
    if (pwr == EV_SINGLE || pwr == EV_LONG) {
      soundSelect();
      menuCursor = 0;
      state = STATE_MENU;
      showMenu(menuCursor);
    }
  }

  // TAG SELECT after recording ──────────────────────────────────────────
  else if (state == STATE_TAG_SELECT) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (rec == EV_SINGLE || rec == EV_LONG) {
      soundSelect();
      saveTag(lastRecNum, tags[constrain(tagCursor, 0, max(tagCount - 1, 0))]);
      enterUltraSleep();
    } else if (pwr == EV_SINGLE) {
      soundNext();
      if (tagCount > 0) tagCursor = (tagCursor + 1) % tagCount;
      requestRedraw();
    }
  }

  // MENU ────────────────────────────────────────────────────────────────
  else if (state == STATE_MENU) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (pwr == EV_SINGLE) {
      soundNext();
      menuCursor = (menuCursor + 1) % MENU_COUNT;
      requestRedraw();
    } else if (rec == EV_SINGLE) {
      soundSelect();
      if (menuCursor == 0) {
        activeFilter = -1; listCursor = 0;
        state = STATE_NOTE_LIST;
        showNoteList(listCursor);
      } else if (menuCursor == 1) {
        tagCursor = 0;
        state = STATE_TAG_BROWSER;
        showTagBrowser(tagCursor);
      } else if (menuCursor == 2) {
        startSyncFlow();
      } else {
        settingsCursor = 0;
        state = STATE_SETTINGS;
        showSettings(settingsCursor);
      }
    } else if (rec == EV_LONG) {
      soundBack();
      state = STATE_IDLE;
      showIdle();
    }
  }

  // SETTINGS ────────────────────────────────────────────────────────────
  else if (state == STATE_SETTINGS) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (pwr == EV_SINGLE) {
      soundNext();
      settingsCursor = (settingsCursor + 1) % SETTINGS_COUNT;
      requestRedraw();
    } else if (rec == EV_SINGLE) {
      soundSelect();
      if (settingsCursor == 0) {
        palaSoundSetEnabled(!palaSoundIsEnabled());
        showSettings(settingsCursor);
      } else if (settingsCursor == 1) {
        startTransferMode();
      } else if (settingsCursor == 2) {
        state = STATE_DEVICE_INFO;
        showDeviceInfo();
      } else {
        state = STATE_RESET_CONFIRM;
        showResetConfirm();
      }
    } else if (rec == EV_LONG) {
      soundBack();
      state = STATE_MENU;
      showMenu(menuCursor);
    }
  }

  // RESET CONFIRM ───────────────────────────────────────────────────────
  else if (state == STATE_RESET_CONFIRM) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (rec == EV_SINGLE) {
      cfg::factoryReset();          // wipe stored Wi-Fi + OpenAI key (notes on SD are kept)
      soundDelete();
      showResetDone();
      delay(1400);
      ESP.restart();                // reboot unprovisioned -> SoftAP setup available again
    } else if (pwr == EV_SINGLE || rec == EV_LONG) {
      soundBack();
      state = STATE_SETTINGS;
      showSettings(settingsCursor);
    }
  }

  // DEVICE INFO ─────────────────────────────────────────────────────────
  else if (state == STATE_DEVICE_INFO) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (rec == EV_LONG || rec == EV_SINGLE || pwr == EV_SINGLE) {
      soundBack();
      state = STATE_SETTINGS;
      showSettings(settingsCursor);
    }
  }

  // TRANSFER MODE ───────────────────────────────────────────────────────
  else if (state == STATE_TRANSFER) {
    if (transferServerActive) transferServer.handleClient();
    ButtonEvent rec = readButtonEvent(BTN_REC);
    if (rec == EV_LONG) {
      soundBack();
      stopTransferMode();
      state = STATE_SETTINGS;
      showSettings(settingsCursor);
    }
  }

  // TAG BROWSER ─────────────────────────────────────────────────────────
  else if (state == STATE_TAG_BROWSER) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (pwr == EV_SINGLE) {
      soundNext();
      if (tagCount > 0) tagCursor = (tagCursor + 1) % tagCount;
      requestRedraw();
    } else if (rec == EV_SINGLE) {
      soundSelect();
      activeFilter = tagCursor; listCursor = 0;
      state = STATE_NOTE_LIST;
      showNoteList(listCursor);
    } else if (rec == EV_LONG) {
      soundBack();
      state = STATE_MENU;
      showMenu(menuCursor);
    }
  }

  // NOTE LIST ───────────────────────────────────────────────────────────
  else if (state == STATE_NOTE_LIST) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);
    int count = filteredCount();

    if (pwr == EV_SINGLE && count > 0) {
      soundNext();
      listCursor = (listCursor + 1) % count;
      requestRedraw();
    } else if (rec == EV_SINGLE && count > 0) {
      soundSelect();
      detailScrollPage = 0;
      state = STATE_NOTE_DETAIL;
      showNoteDetail(listCursor);
    } else if (rec == EV_LONG) {
      soundBack();
      state = STATE_MENU;
      showMenu(menuCursor);
    }
  }

  // NOTE DETAIL ─────────────────────────────────────────────────────────
  else if (state == STATE_NOTE_DETAIL) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (pwr == EV_SINGLE) {
      soundNext();
      const int linesPerPage = 7;
      int totalPages = (detailTotalLines + linesPerPage - 1) / linesPerPage;
      if (detailScrollPage + 1 < totalPages) {
        detailScrollPage++;
      } else {
        detailScrollPage = 0;
        int count = filteredCount();
        if (count > 0) listCursor = (listCursor + 1) % count;
      }
      requestRedraw();
    } else if (rec == EV_SINGLE) {
      int idx = noteAtFilteredIndex(listCursor);
      if (idx >= 0) {
        char wavPath[64];
        snprintf(wavPath, sizeof(wavPath), "%s/note_%03d.wav", NOTES_DIR, noteIndex[idx].num);
        showPlaybackOverlay();
        playWavFile(wavPath);
        showNoteDetail(listCursor);
      }
    } else if (pwr == EV_LONG) {                 // hold power = delete
      int idx = noteAtFilteredIndex(listCursor);
      if (idx >= 0) {
        state = STATE_DELETE_CONFIRM;
        showDeleteConfirm(noteIndex[idx].num);
      }
    } else if (rec == EV_LONG) {                  // hold record = back to list
      soundBack();
      detailScrollPage = 0;
      state = STATE_NOTE_LIST;
      showNoteList(listCursor);
    }
  }

  // DELETE CONFIRM ──────────────────────────────────────────────────────
  else if (state == STATE_DELETE_CONFIRM) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (rec == EV_SINGLE) {
      int idx = noteAtFilteredIndex(listCursor);
      if (idx >= 0) {
        deleteNote(noteIndex[idx].num);
        soundDelete();
      }
      detailScrollPage = 0;
      listCursor = constrain(listCursor, 0, max(filteredCount() - 1, 0));
      state = STATE_NOTE_LIST;
      showNoteList(listCursor);
    } else if (pwr == EV_SINGLE || rec == EV_LONG) {
      soundBack();
      state = STATE_NOTE_DETAIL;
      showNoteDetail(listCursor);
    }
  }

  delay(LOOP_DELAY_MS);
}
