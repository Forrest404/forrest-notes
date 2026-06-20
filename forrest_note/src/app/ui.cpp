#include "Arduino.h"
#include "../../config.h"
#include "../../globals.h"
#include "../../types.h"
#include "ui.h"
#include "draw.h"
#include "notes.h"
#include "battery.h"
#include "rtc.h"
#include "../../logo_bitmap.h"
#include "../../sounds.h"
#include "SD_MMC.h"

#define W   200
#define H   200

// ─── Icons ────────────────────────────────────────────────────────────────

void iconMicWhite(int cx, int cy) {
  fillRect(cx-13, cy-36, 26, 44, WHITE);
  fillCircle(cx, cy-36, 13, WHITE);
  fillCircle(cx, cy+8,  13, WHITE);
  strokeCircle(cx, cy-4, 40, 5, WHITE);
  fillRect(cx-50, cy-50, 100, 50, BLACK);
  fillRect(cx-3,  cy+38, 6,  18, WHITE);
  fillRect(cx-24, cy+54, 48,  5, WHITE);
}

void iconRecordBig(int cx, int cy) {
  fillCircle(cx, cy, 36, WHITE);
  strokeCircle(cx, cy, 52, 5, WHITE);
  strokeCircle(cx, cy, 68, 2, WHITE);
}

void iconCheck(int cx, int cy, bool filled) {
  if (filled) {
    fillCircle(cx, cy, 44, BLACK);
    for (int t=-3;t<=3;t++) {
      line(cx-22, cy-2+t, cx-6, cy+17+t, WHITE);
      line(cx-6,  cy+17+t, cx+30, cy-22+t, WHITE);
    }
  } else {
    strokeCircle(cx, cy, 44, 3, BLACK);
    for (int t=-2;t<=2;t++) {
      line(cx-22, cy-2+t, cx-6, cy+17+t, BLACK);
      line(cx-6,  cy+17+t, cx+30, cy-22+t, BLACK);
    }
  }
}

void iconError(int cx, int cy) {
  strokeCircle(cx, cy, 44, 3, BLACK);
  for (int t=-3;t<=3;t++) {
    line(cx-22, cy-22+t, cx+22, cy+22+t, BLACK);
    line(cx+22, cy-22+t, cx-22, cy+22+t, BLACK);
  }
}

void iconThinking(int cx, int cy) {
  fillCircle(cx-28, cy, 8, BLACK);
  fillCircle(cx,    cy, 8, BLACK);
  fillCircle(cx+28, cy, 8, BLACK);
}

void iconTag(int cx, int cy) {
  const int pts[5][2] = {
    {cx-26, cy+4}, {cx-4, cy-18}, {cx+32, cy-18},
    {cx+32, cy+12}, {cx+4,  cy+36}
  };
  for(int i=0;i<4;i++) thickLine(pts[i][0],pts[i][1],pts[i+1][0],pts[i+1][1],4,BLACK);
  thickLine(pts[4][0],pts[4][1],pts[0][0],pts[0][1],4,BLACK);
  fillCircle(cx-2, cy-4, 5, BLACK);
}

void iconSync(int cx, int cy) {
  strokeCircle(cx, cy, 40, 4, BLACK);
  fillRect(cx+16, cy-46, 20, 20, WHITE);
  thickLine(cx+16, cy-36, cx+36, cy-36, 3, BLACK);
  thickLine(cx+36, cy-36, cx+26, cy-46, 3, BLACK);
  thickLine(cx+36, cy-36, cx+26, cy-26, 3, BLACK);
  fillRect(cx-36, cy+26, 20, 20, WHITE);
  thickLine(cx-36, cy+36, cx-16, cy+36, 3, BLACK);
  thickLine(cx-16, cy+36, cx-26, cy+26, 3, BLACK);
  thickLine(cx-16, cy+36, cx-26, cy+46, 3, BLACK);
}

void iconWifi(int cx, int cy) {
  int base = cy + 26;
  strokeCircle(cx, base, 50, 5, BLACK);
  strokeCircle(cx, base, 32, 5, BLACK);
  strokeCircle(cx, base, 14, 5, BLACK);
  fillRect(0, base, W, H - base, WHITE);
  fillCircle(cx, base, 5, BLACK);
}

void iconNoteLines(int cx, int cy) {
  fillRect(cx-32, cy-12, 64, 6, BLACK);
  fillRect(cx-32, cy+2,  64, 6, BLACK);
  fillRect(cx-32, cy+16, 44, 6, BLACK);
}

// ─── Layout helpers ────────────────────────────────────────────────────────

void drawHeader(const char* title, const char* rightInfo) {
  fillRect(0, 0, W, 28, BLACK);
  drawStrC(W/2, 10, title, 1, WHITE);
  if (rightInfo) {
    int rw = textW(rightInfo, 1);
    drawStr(W - 8 - rw, 10, rightInfo, 1, WHITE);
  }
}

void drawHints(const char* recLabel, const char* pwrLabel) {
  hline(0, 179, W, BLACK);
  fillRect(0, 180, W, 20, WHITE);
  drawStr(8, 186, recLabel, 1, BLACK);
  int rw = textW(pwrLabel, 1);
  drawStr(W - 8 - rw, 186, pwrLabel, 1, BLACK);
}

void drawBadge(int cx, int cy, const char* text, bool filled) {
  char up[32]; uppercaseCopy(up, text, sizeof(up));
  int tw = textW(up, 1);
  int bw = tw + 20, bh = 20;
  int bx = cx - bw/2, by = cy - bh/2;
  if (filled) {
    fillRoundRect(bx, by, bw, bh, 9, BLACK);
    drawStrC(cx, by + 6, up, 1, WHITE);
  } else {
    strokeRoundRect(bx, by, bw, bh, 9, 2, BLACK);
    drawStrC(cx, by + 6, up, 1, BLACK);
  }
}

void drawPageDots(int cur, int total) {
  if (total <= 1) return;
  int n = min(total, 7);
  int gap = 16;
  int startX = W/2 - ((n-1)*gap)/2;
  for (int i = 0; i < n; i++) {
    int x = startX + i*gap, y = 168;
    if (i == cur % n) fillCircle(x, y, 5, BLACK);
    else              strokeCircle(x, y, 4, 1, BLACK);
  }
}

void drawChevronRight(int x, int cy, uint8_t c) {
  thickLine(x,   cy-8, x+8, cy,   2, c);
  thickLine(x+8, cy,   x,   cy+8, 2, c);
}

void drawTinyHint(const char* left, const char* right) {
  (void)left; (void)right;
}

void drawKicker(const char* txt, int y) {
  char up[40]; uppercaseCopy(up, txt, sizeof(up));
  drawStrC(W/2, y, up, 1, BLACK);
}

void drawSoftFrame() {
  strokeRoundRect(12, 12, W-24, H-24, 10, 1, BLACK);
}

void drawProductWordmark(int cx, int y, uint8_t color) {
  drawStr(cx - textW("forrest", 2) / 2, y,      "forrest", 2, color);
  drawStr(cx - textW("note", 2) / 2,    y + 22, "note",    2, color);
}

void drawModernPill(int x, int y, int w, int h, const char* label, bool active) {
  if (active) {
    fillRoundRect(x, y, w, h, h/2, BLACK);
    drawStrInBox(x, y, w, h, label, 1, WHITE);
  } else {
    strokeRoundRect(x, y, w, h, h/2, 1, BLACK);
    drawStrInBox(x, y, w, h, label, 1, BLACK);
  }
}

void drawDotSelector(int cur, int total, int y) {
  int gap = 17, startX = W/2 - ((total-1)*gap)/2;
  for (int i=0; i<total; i++) {
    int x = startX + i*gap;
    if (i == cur) fillCircle(x, y, 4, BLACK);
    else          strokeCircle(x, y, 4, 1, BLACK);
  }
}

void drawCheckSmall(int cx, int cy, uint8_t color) {
  strokeCircle(cx, cy, 13, 1, color);
  thickLine(cx-6, cy, cx-1, cy+5, 2, color);
  thickLine(cx-1, cy+5, cx+8, cy-6, 2, color);
}

void drawMinimalDocIcon(int cx, int cy, uint8_t color) {
  strokeRoundRect(cx-13, cy-16, 26, 32, 3, 2, color);
  hline(cx-7, cy-5, 14, color);
  hline(cx-7, cy+4, 14, color);
  hline(cx-7, cy+13, 9, color);
}

void drawMinimalTagIcon(int cx, int cy, uint8_t color) {
  thickLine(cx-13, cy, cx-2, cy-13, 2, color);
  thickLine(cx-2, cy-13, cx+14, cy-13, 2, color);
  thickLine(cx+14, cy-13, cx+14, cy+2, 2, color);
  thickLine(cx+14, cy+2, cx+2, cy+15, 2, color);
  thickLine(cx+2, cy+15, cx-13, cy, 2, color);
  fillCircle(cx+4, cy-5, 3, color);
}

void drawMinimalCloudIcon(int cx, int cy, uint8_t color) {
  strokeCircle(cx-8, cy+2, 10, 2, color);
  strokeCircle(cx+4, cy-4, 13, 2, color);
  strokeCircle(cx+15, cy+4, 9, 2, color);
  fillRect(cx-22, cy+4, 47, 16, WHITE);
  hline(cx-21, cy+10, 44, color);
}

void drawMenuTile(int x, int y, int w, int h, const char* label, int icon, bool active) {
  if (active) fillRoundRect(x, y, w, h, 12, BLACK);
  else        strokeRoundRect(x, y, w, h, 12, 1, BLACK);
  uint8_t col = active ? WHITE : BLACK;
  int cx = x + w/2;
  fillCircle(cx, y + 17, 4, col);
  drawStrInBox(x + 4, y + 29, w - 8, 18, label, 1, col);
}

void drawNoteCard(int y, int idx, bool active) {
  const int x = 16, w = 168, h = 39;
  if (active) fillRoundRect(x, y, w, h, 8, BLACK);
  else        strokeRoundRect(x, y, w, h, 8, 1, BLACK);
  uint8_t col = active ? WHITE : BLACK;

  char n[8]; snprintf(n, sizeof(n), "#%03d", noteIndex[idx].num);
  String tagLabel = normalizeForDisplay(String(noteIndex[idx].tag));
  drawStr(x + 10, y + 5, n, 1, col);
  drawStrFit(x + 66, y + 5, 88, tagLabel.c_str(), 1, col);
  String ticker = noteTickerText(idx);
  drawTickerText(x + 10, y + 22, 145, ticker, active, col);
}

void drawListMenuCard(int y, const char* title, const char* meta, bool active) {
  const int x = 16, w = 168, h = 32;
  if (active) fillRoundRect(x, y, w, h, 8, BLACK);
  else        strokeRoundRect(x, y, w, h, 8, 1, BLACK);
  uint8_t col = active ? WHITE : BLACK;
  drawStrFit(x + 10, y + 8, meta ? 92 : 140, title, 1, col);
  if (meta && strlen(meta) > 0) {
    int mw = min(textW(meta, 1), 56);
    drawStrFit(x + w - 10 - mw, y + 8, 56, meta, 1, col);
  }
}

// ─── Screens ──────────────────────────────────────────────────────────────

static void drawBolt(int x, int y) {          // small ~10x18 lightning bolt, top-left origin
  fillTriangle(x+7, y,    x+1, y+9,  x+6, y+9,  BLACK);
  fillTriangle(x+5, y+8,  x+10, y+8, x+3, y+18, BLACK);
}

void showIdle() {
  clearWhite();
  int  batt     = readBatteryPercent();
  bool charging = isBatteryCharging();
  drawBatteryRing(batt);
  drawProductWordmark(100, 58, BLACK);

  // numeric battery % (+ charging bolt), centered below the wordmark
  char b[8];
  if (batt < 0) snprintf(b, sizeof(b), "--");
  else          snprintf(b, sizeof(b), "%d%%", batt);
  int tw    = textW(b, 1);
  int boltW = charging ? 14 : 0;
  int x     = 100 - (tw + boltW) / 2;
  if (charging) { drawBolt(x, 132); x += boltW; }
  drawStr(x, 144, b, 1, BLACK);
  refresh();
}

void showBatteryLow(int pct) {
  fillRect(0, 0, W, H, BLACK);
  fillRect(95, 48, 10, 50, WHITE);
  fillRect(95, 108, 10, 10, WHITE);
  char buf[8]; snprintf(buf, sizeof(buf), "%d%%", pct);
  drawStrC(100, 132, buf,       2, WHITE);
  drawStrC(100, 160, "battery", 1, WHITE);
  drawStrC(100, 176, "low",     1, WHITE);
  refresh();
}

static void drawRecordingScreen(uint32_t elapsedMs, int level) {
  clearWhite();
  // REC indicator (filled dot + label)
  fillCircle(60, 32, 7, BLACK);
  drawStr(74, 24, "REC", 2, BLACK);
  // elapsed time, large
  uint32_t s = elapsedMs / 1000;
  char t[12]; snprintf(t, sizeof(t), "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
  drawStrC(100, 92, t, 3, BLACK);
  // audio level meter
  const int barX = 22, barY = 150, barW = 156, barH = 22;
  strokeRoundRect(barX, barY, barW, barH, 5, 1, BLACK);
  int fill = level; if (fill < 0) fill = 0; if (fill > barW - 4) fill = barW - 4;
  if (fill > 0) fillRoundRect(barX + 2, barY + 2, fill, barH - 4, 3, BLACK);
  drawStrC(100, 184, "release to stop", 1, BLACK);
}

void showRecording() {                         // initial frame (synchronous)
  drawRecordingScreen(0, 0);
  refresh();
}

// Periodic, non-blocking update during recording — keeps audio capture / SD
// writes flowing while the panel repaints in the background.
void showRecordingLive(uint32_t elapsedMs, int level) {
  if (displayBusy()) return;                   // skip a frame if the panel is still painting
  drawRecordingScreen(elapsedMs, level);
  display->EPD_DisplayPartTrigger();
}

void showSaved(int num) {
  clearWhite();
  drawCheckSmall(100, 46, BLACK);
  drawStrC(100, 76, "saved", 1, BLACK);
  char b[8]; snprintf(b, sizeof(b), "#%03d", num);
  drawStrC(100, 105, b, 2, BLACK);
  refresh();
}

void showTagSelect(int cursor) {
  clearWhite();
  if (tagCount <= 0) {
    drawKicker("no tags", 34);
    drawStrC(100, 100, "open portal", 1, BLACK);
    refresh();
    return;
  }
  drawKicker("choose tag", 17);
  const int x = 36, w = 128, h = 21, gap = 7;
  int y0 = 40;
  cursor = constrain(cursor, 0, max(tagCount - 1, 0));
  for (int i=0; i<tagCount; i++) {
    int y = y0 + i*(h+gap);
    drawModernPill(x, y, w, h, tags[i], i == cursor);
  }
  refresh();
}

void showMenu(int cursor) {
  clearWhite();
  drawStr(16, 14, "menu", 1, BLACK);
  hline(16, 32, W-32, BLACK);
  const int y0 = 42, step = 36;
  for (int row = 0; row < MENU_COUNT; row++) {
    bool active = row == cursor;
    int y = y0 + row * step;
    if (active) fillRoundRect(16, y, 168, 31, 8, BLACK);
    else        strokeRoundRect(16, y, 168, 31, 8, 1, BLACK);
    uint8_t col = active ? WHITE : BLACK;
    drawStrInBox(16, y, 168, 31, MENU_ITEMS[row], 1, col);
  }
  refresh();
}

void showTagBrowser(int cursor) {
  clearWhite();
  if (tagCount <= 0) {
    drawKicker("tags", 16);
    drawStrC(100, 100, "no tags", 1, BLACK);
    refresh();
    return;
  }
  drawKicker("tags", 16);
  fillRoundRect(28, 56, 144, 54, 17, BLACK);
  cursor = constrain(cursor, 0, max(tagCount - 1, 0));
  drawStrInBox(28, 56, 144, 54, tags[cursor], 2, WHITE);
  int cnt = 0;
  for (int i=0; i<(int)noteIndex.size(); i++)
    if (strcmp(noteIndex[i].tag, tags[cursor])==0) cnt++;
  char cb[20]; snprintf(cb, sizeof(cb), "%d notes", cnt);
  drawStrC(100, 130, cb, 1, BLACK);
  refresh();
}

void showNoteList(int cursor) {
  if (tickerCursor != cursor) {
    tickerCursor = cursor;
    tickerOffset = 0;
    tickerLastMs = millis();
  }
  clearWhite();
  int count = filteredCount();
  char cb[16]; snprintf(cb, sizeof(cb), "%d notes", count);
  drawStr(16, 14, "notes", 1, BLACK);
  int cw = textW(cb, 1);
  drawStr(W-16-cw, 14, cb, 1, BLACK);
  if (count <= 0) {
    drawMinimalDocIcon(100, 76, BLACK);
    drawStrC(100, 116, "no notes yet", 1, BLACK);
    refresh();
    return;
  }
  const int pageSize = 3;
  int pageStart = (cursor / pageSize) * pageSize;
  int activeRow = cursor - pageStart;
  const int y0 = 43, step = 47;
  int shown = min(pageSize, count - pageStart);
  for (int row=0; row<shown; row++) {
    int vis = pageStart + row;
    int idx = noteAtFilteredIndex(vis);
    if (idx >= 0) drawNoteCard(y0 + row*step, idx, row == activeRow);
  }
  refresh();
}

void showNoteDetail(int cursor) {
  clearWhite();
  int idx = noteAtFilteredIndex(cursor);
  if (idx < 0) {
    drawStrC(100, 96, "not found", 1, BLACK);
    refresh();
    return;
  }
  char n[8]; snprintf(n, sizeof(n), "#%03d", noteIndex[idx].num);
  drawStr(16, 14, n, 1, BLACK);
  String tagLabel = normalizeForDisplay(String(noteIndex[idx].tag));
  int tw = textW(tagLabel.c_str(), 1);
  drawStrFit(W-16-min(tw, 82), 14, 82, tagLabel.c_str(), 1, BLACK);
  hline(16, 32, W-32, BLACK);

  if (noteIndex[idx].hasText) {
    char txtPath[64];
    snprintf(txtPath, sizeof(txtPath), "%s/note_%03d.txt", NOTES_DIR, noteIndex[idx].num);
    File f = SD_MMC.open(txtPath);
    char text[2048] = {0};
    if (f) { f.read((uint8_t*)text, 2047); f.close(); }
    String bodyText = normalizeForDisplay(String(text));
    const int linesPerPage = 7;
    int skip = detailScrollPage * linesPerPage;
    detailTotalLines = drawWrappedText(18, 48, 164, 18, linesPerPage, bodyText, BLACK, skip);
    int totalPages = (detailTotalLines + linesPerPage - 1) / linesPerPage;
    if (totalPages > 1) {
      char pageLabel[12];
      snprintf(pageLabel, sizeof(pageLabel), "%d/%d", detailScrollPage + 1, totalPages);
      int lw = textW(pageLabel, 1);
      drawStr(W - 8 - lw, 186, pageLabel, 1, BLACK);
      hline(0, 179, W, BLACK);
    }
  } else {
    iconThinking(100, 82);
    drawStrC(100, 122, "not synced", 1, BLACK);
  }
  refresh();
}

void showDeleteConfirm(int noteNum) {
  clearWhite();
  fillRect(0, 0, W, 28, BLACK);
  drawStrC(W/2, 10, "DELETE", 1, WHITE);
  char label[16]; snprintf(label, sizeof(label), "#%03d", noteNum);
  drawStrC(W/2, 52, label, 2, BLACK);
  drawStrC(W/2, 88, "Delete this note?", 1, BLACK);
  drawStrC(W/2, 108, "WAV + TXT + meta", 1, BLACK);
  hline(0, 179, W, BLACK);
  fillRect(0, 180, W, 20, WHITE);
  drawStr(8, 186, "confirm", 1, BLACK);
  int rw = textW("cancel", 1);
  drawStr(W - 8 - rw, 186, "cancel", 1, BLACK);
  refresh();
}

void showObsidianSync(int done, int total) {
  clearWhite();
  drawKicker("vault", 20);
  iconSync(100, 76);
  int barW = 144, barH = 10, barX = 28, barY = 116;
  strokeRoundRect(barX, barY, barW, barH, 5, 1, BLACK);
  if (total > 0) {
    int fill = (done * (barW - 4)) / max(total, 1);
    if (fill > 0) fillRoundRect(barX+2, barY+2, fill, barH-4, 3, BLACK);
    char b[20]; snprintf(b, sizeof(b), "%d / %d", done, total);
    drawStrC(100, 142, b, 1, BLACK);
  } else {
    drawStrC(100, 142, "please wait", 1, BLACK);
  }
  refresh();
}

void showTranscribing(int done, int total) {
  clearWhite();
  drawKicker("syncing", 20);
  iconThinking(100, 76);
  int barW = 144, barH = 10, barX = 28, barY = 116;
  strokeRoundRect(barX, barY, barW, barH, 5, 1, BLACK);
  if (total > 0) {
    int fill = (done * (barW - 4)) / max(total, 1);
    if (fill > 0) fillRoundRect(barX+2, barY+2, fill, barH-4, 3, BLACK);
    char b[20]; snprintf(b, sizeof(b), "%d / %d", done, total);
    drawStrC(100, 142, b, 1, BLACK);
  } else {
    drawStrC(100, 142, "please wait", 1, BLACK);
  }
  refresh();
}

void showWifiConnecting(int attempt, int maxA) {
  clearWhite();
  drawKicker("wifi", 20);
  iconWifi(100, 84);
  int barW = 130, barH = 10, barX = 35, barY = 140;
  strokeRoundRect(barX, barY, barW, barH, 5, 1, BLACK);
  int fill = (attempt * (barW - 4)) / max(maxA, 1);
  if (fill > 0) fillRoundRect(barX+2, barY+2, fill, barH-4, 3, BLACK);
  char b[20]; snprintf(b, sizeof(b), "%d / %d", attempt, maxA);
  drawStrC(100, 164, b, 1, BLACK);
  refresh();
}

void showDone() {
  clearWhite();
  drawCheckSmall(100, 70, BLACK);
  drawStrC(100, 105, "all done", 1, BLACK);
  refresh();
}

void showError(const char* msg) {
  clearWhite();
  iconError(100, 70);
  if (msg && strlen(msg) > 0) drawStrC(100, 118, msg, 1, BLACK);
  else drawStrC(100, 118, "error", 1, BLACK);
  refresh();
}

void showUltraSleepScreen() {
  clearWhite();
  #ifdef LOGO_WIDTH
    drawBitmap1BPP((W - LOGO_WIDTH) / 2, (H - LOGO_HEIGHT) / 2,
                   logo_bitmap, LOGO_WIDTH, LOGO_HEIGHT, BLACK);
  #else
    drawProductWordmark(100, 70, BLACK);
  #endif
  forceFullRefresh();   // this image persists through deep sleep; keep it crisp
}

void showPlaybackOverlay() {
  fillRoundRect(75, 145, 50, 34, 11, BLACK);
  fillTriangle(95, 154, 95, 170, 110, 162, WHITE);
  refresh();
}

void showTransferConnecting() {
  clearWhite();
  drawKicker("transfer", 18);
  iconWifi(100, 82);
  drawStrC(100, 138, "connecting", 1, BLACK);
  refresh();
}

void showTransferMode(const char* ip) {
  clearWhite();
  drawKicker("transfer", 16);
  fillRoundRect(26, 48, 148, 58, 16, BLACK);
  drawStrInBox(26, 48, 148, 24, "forrest portal", 1, WHITE);
  drawStrInBox(26, 74, 148, 24, "active", 1, WHITE);
  drawStrC(100, 124, "open browser", 1, BLACK);
  drawStrC(100, 146, ip, 1, BLACK);
  drawStrC(100, 169, "double rec to exit", 1, BLACK);
  refresh();
}

void showSettings(int cursor) {
  clearWhite();
  drawStr(16, 14, "settings", 1, BLACK);
  hline(16, 32, W-32, BLACK);
  const int y0 = 40, step = 38, boxH = 34;
  for (int row = 0; row < SETTINGS_COUNT; row++) {
    bool active = row == cursor;
    int y = y0 + row * step;
    if (active) fillRoundRect(16, y, 168, boxH, 8, BLACK);
    else        strokeRoundRect(16, y, 168, boxH, 8, 1, BLACK);
    uint8_t col = active ? WHITE : BLACK;
    if (row == 0) {
      drawStr(28, y + 8, "sounds", 1, col);
      drawStr(W - 70, y + 8, palaSoundIsEnabled() ? "on" : "off", 1, col);
    } else if (row == 1) {
      drawStr(28, y + 8, "transfer", 1, col);
    } else if (row == 2) {
      drawStr(28, y + 8, "device", 1, col);
    } else {
      drawStr(28, y + 8, "reset", 1, col);
    }
  }
  refresh();
}

void showDeviceInfo() {
  clearWhite();
  drawStr(16, 14, "device", 1, BLACK);
  hline(16, 32, W-32, BLACK);
  drawStr(18, 50, "firmware", 1, BLACK);
  drawStrFit(18, 68, 160, FIRMWARE_VERSION, 1, BLACK);
  drawStr(18, 94, "board", 1, BLACK);
  drawStrFit(18, 112, 160, "ESP32-S3 ePaper 1.54", 1, BLACK);
  char b[24]; snprintf(b, sizeof(b), "%d notes", (int)noteIndex.size());
  drawStr(18, 138, b, 1, BLACK);
  drawStr(18, 160, palaSoundIsEnabled() ? "sounds on" : "sounds off", 1, BLACK);
  drawStr(18, 178, rtcUtcIso().length() ? "rtc set" : "rtc not set", 1, BLACK);
  refresh();
}

void showResetConfirm() {
  clearWhite();
  drawKicker("factory reset", 18);
  drawStrC(100, 64,  "erase wifi & key?", 1, BLACK);
  drawStrC(100, 86,  "notes are kept", 1, BLACK);
  hline(20, 110, W - 40, BLACK);
  drawStrC(100, 134, "rec = erase", 1, BLACK);
  drawStrC(100, 156, "pwr = cancel", 1, BLACK);
  refresh();
}

void showResetDone() {
  clearWhite();
  drawCheckSmall(100, 70, BLACK);
  drawStrC(100, 110, "reset done", 1, BLACK);
  drawStrC(100, 132, "restarting", 1, BLACK);
  forceFullRefresh();
}

// ─── Coalesced async redraw ─────────────────────────────────────────────────
// Navigation (cursor moves) just calls requestRedraw() — instant, non-blocking.
// serviceDisplay() (run each loop) repaints the *current* state asynchronously
// whenever the panel is free, so rapid presses coalesce to the final position
// instead of getting eaten by a blocking ~400 ms refresh.
void redrawCurrentScreen() {
  switch (state) {
    case STATE_MENU:        showMenu(menuCursor);         break;
    case STATE_SETTINGS:    showSettings(settingsCursor); break;
    case STATE_NOTE_LIST:   showNoteList(listCursor);     break;
    case STATE_TAG_BROWSER: showTagBrowser(tagCursor);    break;
    case STATE_TAG_SELECT:  showTagSelect(tagCursor);     break;
    case STATE_NOTE_DETAIL: showNoteDetail(listCursor);   break;
    default: break;
  }
}

void serviceDisplay() {
  if (!displayDirty()) return;
  if (displayBusy()) return;          // a previous async update is still painting
  clearDisplayDirty();
  beginBufferDraw();                  // suppress the internal refresh()
  redrawCurrentScreen();              // draw the latest state into the buffer
  endBufferDraw();
  refreshAsyncFromBuffer();           // start the partial update; returns immediately
}
