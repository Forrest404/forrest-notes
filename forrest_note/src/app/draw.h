#pragma once
#include <Adafruit_GFX.h>
#include <pgmspace.h>
#include "../display/epaper_driver_bsp.h"

// Screen dimensions and color aliases — shared by all drawing and UI code.
#define W     200
#define H     200
#define BLACK DRIVER_COLOR_BLACK
#define WHITE DRIVER_COLOR_WHITE

static const int BPR = 25;

void px(int x, int y, uint8_t c);
void fillRect(int x, int y, int w, int h, uint8_t c);
void hline(int x, int y, int w, uint8_t c);
void vline(int x, int y, int h, uint8_t c);
void strokeRect(int x, int y, int w, int h, int t, uint8_t c);
void fillCircle(int cx, int cy, int r, uint8_t c);
void strokeCircle(int cx, int cy, int r, int t, uint8_t c);
void line(int x0, int y0, int x1, int y1, uint8_t c);
void thickLine(int x0, int y0, int x1, int y1, int t, uint8_t c);
void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t c);
void fillRoundRect(int x, int y, int w, int h, int r, uint8_t c);
void strokeRoundRect(int x, int y, int w, int h, int r, int t, uint8_t c);
void drawBitmap1BPP(int x0, int y0, const uint8_t* bits, int bw, int bh, uint8_t color);

void clearWhite();
void refresh();                  // synchronous: blocks until the panel finishes
void forceFullRefresh();         // immediate full-waveform pass to clear ghosting
void refreshAsyncFromBuffer();   // non-blocking partial update of current buffer
bool displayBusy();              // true while a refresh is in progress
void beginBufferDraw();          // suppress refresh() while drawing buffer for async
void endBufferDraw();
void requestRedraw();            // mark screen dirty (coalesced async redraw)
bool displayDirty();
void clearDisplayDirty();

void drawBigDigit(int x, int y, int w, int h, char d, uint8_t c);
int  bigStrW(int h, const char* s);
int  drawBigStr(int x, int y, int h, const char* s, uint8_t c);
void drawBigStrC(int cx, int y, int h, const char* s, uint8_t c);

int  fontIdx(char c);
void drawCharLegacy(int x, int y, char c, int scale, uint8_t color);

const GFXfont* uiFontForScale(int scale);
int            uiFontHeight(int scale);
void           textBoundsFont(const char* s, int scale, int* minX, int* minY, int* maxX, int* maxY, int* advOut = nullptr);
int            textW(const char* s, int scale);
void           drawGlyphFont(int x, int baseline, char ch, const GFXfont* font, uint8_t color, int* adv);
void           drawStr(int x, int y, const char* s, int scale, uint8_t color);
void           drawStrC(int cx, int y, const char* s, int scale, uint8_t color);
void           drawStrFit(int x, int y, int maxW, const char* s, int scale, uint8_t color);
void           drawStrInBox(int x, int y, int w, int h, const char* s, int scale, uint8_t color);

void uppercaseCopy(char* dst, const char* src, int maxLen);

String normalizeForDisplay(const String& in);
int    drawWrappedText(int x, int y, int maxW, int lineH, int maxLines,
                       const String& rawText, uint8_t color, int skipLines = 0);
