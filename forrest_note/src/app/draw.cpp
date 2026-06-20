#include "Arduino.h"
#include "draw.h"
#include "../../globals.h"
#include <Adafruit_GFX.h>
#include <pgmspace.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <math.h>

// W and H are defined in the .ino. Pull them in via globals.
#define W 200
#define H 200

inline void px(int x, int y, uint8_t c) {
  if ((unsigned)x >= W || (unsigned)y >= H) return;
  uint8_t* buf = display->getBuffer();
  int      idx = y * BPR + (x >> 3);
  uint8_t  bit = 1 << (7 - (x & 7));
  if (c == BLACK) buf[idx] &= ~bit; else buf[idx] |= bit;
}

void fillRect(int x, int y, int w, int h, uint8_t c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x >= W || y >= H || w <= 0 || h <= 0) return;
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  uint8_t* buf  = display->getBuffer();
  int x2    = x + w - 1;
  int byteL = x  >> 3, byteR = x2 >> 3;
  uint8_t mL = 0xFF >> (x  & 7);
  uint8_t mR = 0xFF << (7 - (x2 & 7));
  for (int r = 0; r < h; r++) {
    uint8_t* row = buf + (y + r) * BPR;
    if (byteL == byteR) {
      uint8_t m = mL & mR;
      if (c == BLACK) row[byteL] &= ~m; else row[byteL] |= m;
    } else {
      if (c == BLACK) {
        row[byteL] &= ~mL;
        memset(row + byteL + 1, 0x00, byteR - byteL - 1);
        row[byteR] &= ~mR;
      } else {
        row[byteL] |= mL;
        memset(row + byteL + 1, 0xFF, byteR - byteL - 1);
        row[byteR] |= mR;
      }
    }
  }
}

void hline(int x, int y, int w, uint8_t c) { fillRect(x, y, w, 1, c); }

void vline(int x, int y, int h, uint8_t c) {
  if ((unsigned)x >= W) return;
  if (y < 0) { h += y; y = 0; }
  if (y + h > H) h = H - y;
  if (h <= 0) return;
  uint8_t* buf = display->getBuffer();
  int idx = y * BPR + (x >> 3);
  uint8_t bit = 1 << (7 - (x & 7));
  if (c == BLACK) { for (int i=0;i<h;i++,idx+=BPR) buf[idx]&=~bit; }
  else            { for (int i=0;i<h;i++,idx+=BPR) buf[idx]|= bit; }
}

void strokeRect(int x, int y, int w, int h, int t, uint8_t c) {
  for (int i=0;i<t;i++) {
    hline(x+i,y+i,w-2*i,c); hline(x+i,y+h-1-i,w-2*i,c);
    vline(x+i,y+i,h-2*i,c); vline(x+w-1-i,y+i,h-2*i,c);
  }
}

void fillCircle(int cx, int cy, int r, uint8_t c) {
  for (int dy=-r; dy<=r; dy++) {
    int dx = (int)sqrtf((float)(r*r - dy*dy));
    fillRect(cx-dx, cy+dy, 2*dx+1, 1, c);
  }
}

void strokeCircle(int cx, int cy, int r, int t, uint8_t c) {
  for (int i=0;i<t;i++) {
    int rr=r-i, x=rr, y=0, err=0;
    while (x>=y) {
      px(cx+x,cy+y,c);px(cx+y,cy+x,c);px(cx-y,cy+x,c);px(cx-x,cy+y,c);
      px(cx-x,cy-y,c);px(cx-y,cy-x,c);px(cx+y,cy-x,c);px(cx+x,cy-y,c);
      y++;err+=1+2*y;
      if(2*(err-x)+1>0){x--;err+=1-2*x;}
    }
  }
}

void line(int x0, int y0, int x1, int y1, uint8_t c) {
  int dx=abs(x1-x0),dy=abs(y1-y0),sx=x0<x1?1:-1,sy=y0<y1?1:-1,err=dx-dy;
  while(true){px(x0,y0,c);if(x0==x1&&y0==y1)break;int e2=2*err;if(e2>-dy){err-=dy;x0+=sx;}if(e2<dx){err+=dx;y0+=sy;}}
}

void thickLine(int x0, int y0, int x1, int y1, int t, uint8_t c) {
  for(int i=-t/2;i<=t/2;i++){if(abs(x1-x0)>abs(y1-y0))line(x0,y0+i,x1,y1+i,c);else line(x0+i,y0,x1+i,y1,c);}
}

void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t c) {
  int minX=min(x0,min(x1,x2));
  int maxX=max(x0,max(x1,x2));
  int minY=min(y0,min(y1,y2));
  int maxY=max(y0,max(y1,y2));
  for(int y=minY;y<=maxY;y++) {
    for(int x=minX;x<=maxX;x++) {
      int w1=(x1-x0)*(y-y0)-(y1-y0)*(x-x0);
      int w2=(x2-x1)*(y-y1)-(y2-y1)*(x-x1);
      int w3=(x0-x2)*(y-y2)-(y0-y2)*(x-x2);
      if((w1>=0&&w2>=0&&w3>=0)||(w1<=0&&w2<=0&&w3<=0)) px(x,y,c);
    }
  }
}

void fillRoundRect(int x, int y, int w, int h, int r, uint8_t c) {
  if (r <= 0) { fillRect(x,y,w,h,c); return; }
  r = min(r, min(w,h)/2);
  fillRect(x+r,   y,   w-2*r, h,     c);
  fillRect(x,     y+r, r,     h-2*r, c);
  fillRect(x+w-r, y+r, r,     h-2*r, c);
  fillCircle(x+r,     y+r,     r, c);
  fillCircle(x+w-r-1, y+r,     r, c);
  fillCircle(x+r,     y+h-r-1, r, c);
  fillCircle(x+w-r-1, y+h-r-1, r, c);
}

void strokeRoundRect(int x, int y, int w, int h, int r, int t, uint8_t c) {
  fillRoundRect(x, y, w, h, r, c);
  uint8_t bg = (c==BLACK) ? WHITE : BLACK;
  if (w>2*t && h>2*t) fillRoundRect(x+t, y+t, w-2*t, h-2*t, max(r-t,0), bg);
}

void drawBitmap1BPP(int x0, int y0, const uint8_t* bits, int bw, int bh, uint8_t color) {
  int rb=(bw+7)/8;
  for(int y=0;y<bh;y++) for(int x=0;x<bw;x++) {
    if((pgm_read_byte(&bits[y*rb+x/8])>>(7-(x%8)))&1) px(x0+x,y0+y,color);
  }
}

void clearWhite() { display->EPD_Clear(); }

// E-paper accumulates ghosting after many partial updates. Periodically run a
// full-waveform pass (and re-establish the partial base image) to keep contrast
// clean. ~30 partials between full refreshes balances cleanliness vs. the ~2 s
// flash of a full refresh.
static uint16_t partialRefreshCount = 0;
static const uint16_t FULL_REFRESH_EVERY = 40;

// When true, refresh() is a no-op — used while drawing into the buffer for an
// asynchronous (non-blocking) partial update.
static bool gSuppressRefresh = false;
void beginBufferDraw() { gSuppressRefresh = true; }
void endBufferDraw()   { gSuppressRefresh = false; }

// Coalesced-redraw dirty flag. Set by navigation, consumed by serviceDisplay().
static bool gDirty = false;
void requestRedraw()      { gDirty = true; }
bool displayDirty()       { return gDirty; }
void clearDisplayDirty()  { gDirty = false; }

static void waitPanelIdle() {
  while (display->EPD_IsBusy()) vTaskDelay(pdMS_TO_TICKS(1));
}
bool displayBusy() { return display->EPD_IsBusy(); }

void forceFullRefresh() {
  waitPanelIdle();                     // don't stomp an in-flight async update
  display->EPD_Init();                 // full-mode init (full LUT) + panel reset
  display->EPD_DisplayPartBaseImage(); // full-waveform refresh of current buffer
  display->EPD_Init_Partial();         // resume partial-update mode
  partialRefreshCount = 0;
}

// Synchronous refresh (blocks until the panel finishes). Used by every screen
// that is immediately followed by a blocking step (record, playback, delay, sleep).
void refresh() {
  if (gSuppressRefresh) return;
  gDirty = false;            // a sync refresh supersedes any pending coalesced redraw
  waitPanelIdle();
  if (++partialRefreshCount >= FULL_REFRESH_EVERY) forceFullRefresh();
  else display->EPD_DisplayPart();
}

// Start an async partial update of whatever is already in the buffer. Returns
// immediately (panel repaints in the background). Periodically promotes to a
// full refresh to clear ghosting.
void refreshAsyncFromBuffer() {
  if (++partialRefreshCount >= FULL_REFRESH_EVERY) forceFullRefresh();   // sync, but rare
  else display->EPD_DisplayPartTrigger();
}

// ─── Large digit renderer ──────────────────────────────────────────────────

void drawBigDigit(int x, int y, int w, int h, char d, uint8_t c) {
  int t  = max(3, w / 5);
  int r  = t / 2;
  int hh = h / 2;

  auto hBar = [&](int sy) {
    fillRoundRect(x + t + 2, sy - t/2, w - 2*(t+2), t + 1, r, c);
  };
  auto vL = [&](int sy, int ey) {
    int a = sy + t, b = ey - t;
    if (b > a) fillRoundRect(x, a, t, b - a, r, c);
  };
  auto vR = [&](int sy, int ey) {
    int a = sy + t, b = ey - t;
    if (b > a) fillRoundRect(x + w - t, a, t, b - a, r, c);
  };

  switch (d) {
    case '0': hBar(y); hBar(y+h);                    vL(y,y+h); vR(y,y+h); break;
    case '1':                                                    vR(y,y+h); break;
    case '2': hBar(y); hBar(y+hh); hBar(y+h);        vR(y,y+hh); vL(y+hh,y+h); break;
    case '3': hBar(y); hBar(y+hh); hBar(y+h);        vR(y,y+h); break;
    case '4': hBar(y+hh);          vL(y,y+hh);       vR(y,y+h); break;
    case '5': hBar(y); hBar(y+hh); hBar(y+h); vL(y,y+hh);       vR(y+hh,y+h); break;
    case '6': hBar(y); hBar(y+hh); hBar(y+h); vL(y,y+h);        vR(y+hh,y+h); break;
    case '7': hBar(y);                                            vR(y,y+h); break;
    case '8': hBar(y); hBar(y+hh); hBar(y+h); vL(y,y+h);        vR(y,y+h); break;
    case '9': hBar(y); hBar(y+hh); hBar(y+h); vL(y,y+hh);       vR(y,y+h); break;
    case '#': {
      int bx1 = x + w/3 - t/2, bx2 = x + 2*w/3 - t/2;
      fillRect(bx1, y, t, h, c);
      fillRect(bx2, y, t, h, c);
      fillRect(x, y + hh/2 - t/2, w, t + 1, c);
      fillRect(x, y + h - hh/2 - t/2, w, t + 1, c);
      break;
    }
    case '-':
      fillRoundRect(x + t, y + hh - t/2, w - 2*t, t + 1, r, c);
      break;
    default: break;
  }
}

static int _bigCharW(int h, char d) {
  int dw = (h * 6) / 10;
  if (d == ' ')  return dw / 2;
  if (d == '-')  return dw * 2 / 3;
  if (d == '#')  return dw * 3 / 4;
  return dw;
}

int bigStrW(int h, const char* s) {
  int gap = max(3, h / 10), total = 0;
  bool first = true;
  while (*s) {
    if (!first) total += gap;
    total += _bigCharW(h, *s);
    first = false; s++;
  }
  return total;
}

int drawBigStr(int x, int y, int h, const char* s, uint8_t c) {
  int gap = max(3, h / 10), ox = x;
  bool first = true;
  while (*s) {
    if (!first) x += gap;
    first = false;
    int cw = _bigCharW(h, *s);
    if (*s != ' ') drawBigDigit(x, y, cw, h, *s, c);
    x += cw; s++;
  }
  return x - ox;
}

void drawBigStrC(int cx, int y, int h, const char* s, uint8_t c) {
  drawBigStr(cx - bigStrW(h, s) / 2, y, h, s, c);
}

// ─── Font 8×8 ─────────────────────────────────────────────────────────────
#define FONT_ADV 9
static const uint8_t F[][8] = {
  {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0},  // A
  {0x3E,0x66,0x66,0x3E,0x66,0x66,0x3E,0},  // B
  {0x3C,0x66,0x06,0x06,0x06,0x66,0x3C,0},  // C
  {0x1E,0x36,0x66,0x66,0x66,0x36,0x1E,0},  // D
  {0x7E,0x06,0x06,0x3E,0x06,0x06,0x7E,0},  // E
  {0x7E,0x06,0x06,0x3E,0x06,0x06,0x06,0},  // F
  {0x3C,0x66,0x06,0x76,0x66,0x66,0x3C,0},  // G
  {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0},  // H
  {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0},  // I
  {0x78,0x30,0x30,0x30,0x30,0x36,0x1C,0},  // J
  {0x66,0x36,0x1E,0x0E,0x1E,0x36,0x66,0},  // K
  {0x06,0x06,0x06,0x06,0x06,0x06,0x7E,0},  // L
  {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0},  // M
  {0x66,0x6E,0x7E,0x76,0x66,0x66,0x66,0},  // N
  {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0},  // O
  {0x3E,0x66,0x66,0x3E,0x06,0x06,0x06,0},  // P
  {0x3C,0x66,0x66,0x66,0x76,0x36,0x5C,0},  // Q
  {0x3E,0x66,0x66,0x3E,0x36,0x66,0x66,0},  // R
  {0x3C,0x66,0x06,0x3C,0x60,0x66,0x3C,0},  // S
  {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0},  // T
  {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0},  // U
  {0x66,0x66,0x66,0x66,0x3C,0x3C,0x18,0},  // V
  {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0},  // W
  {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0},  // X
  {0x66,0x66,0x3C,0x18,0x18,0x18,0x18,0},  // Y
  {0x7E,0x60,0x30,0x18,0x0C,0x06,0x7E,0},  // Z
  {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0},  // 0
  {0x18,0x18,0x38,0x18,0x18,0x18,0x7E,0},  // 1
  {0x3C,0x66,0x60,0x30,0x18,0x0C,0x7E,0},  // 2
  {0x3C,0x66,0x60,0x38,0x60,0x66,0x3C,0},  // 3
  {0x30,0x38,0x3C,0x36,0x7E,0x30,0x30,0},  // 4
  {0x7E,0x06,0x3E,0x60,0x60,0x66,0x3C,0},  // 5
  {0x38,0x0C,0x06,0x3E,0x66,0x66,0x3C,0},  // 6
  {0x7E,0x60,0x30,0x18,0x0C,0x0C,0x0C,0},  // 7
  {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0},  // 8
  {0x3C,0x66,0x66,0x7C,0x60,0x30,0x1C,0},  // 9
  {0,0,0,0,0,0x18,0x18,0},                  // .
  {0,0,0,0x7E,0,0,0,0},                     // -
  {0x60,0x30,0x18,0x0C,0x06,0x03,0,0},      // /
  {0,0x18,0x18,0,0x18,0x18,0,0},            // :
  {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0},   // #
  {0,0,0,0,0,0,0,0},                        // space
};

int fontIdx(char c) {
  if(c>='A'&&c<='Z') return c-'A'; if(c>='a'&&c<='z') return c-'a';
  if(c>='0'&&c<='9') return 26+c-'0';
  if(c=='.') return 36; if(c=='-') return 37; if(c=='/') return 38;
  if(c==':') return 39; if(c=='#') return 40; return 41;
}

void drawCharLegacy(int x, int y, char c, int scale, uint8_t color) {
  const uint8_t* g = F[fontIdx(c)];
  for(int row=0;row<8;row++) { uint8_t bits=g[row];
    for(int col=0;col<8;col++) if(bits&(1<<col)) fillRect(x+col*scale,y+row*scale,scale,scale,color); }
}

// ─── Modern proportional font renderer ────────────────────────────────────

const GFXfont* uiFontForScale(int scale) {
  if (scale <= 1) return &FreeSans9pt7b;
  if (scale == 2) return &FreeSansBold12pt7b;
  return &FreeSansBold18pt7b;
}

int uiFontHeight(int scale) {
  if (scale <= 1) return 14;
  if (scale == 2) return 22;
  return 31;
}

void textBoundsFont(const char* s, int scale, int* minX, int* minY, int* maxX, int* maxY, int* advOut) {
  const GFXfont* font = uiFontForScale(scale);
  uint8_t first = pgm_read_byte(&font->first);
  uint8_t last  = pgm_read_byte(&font->last);
  int x = 0;
  *minX =  32767; *minY =  32767;
  *maxX = -32768; *maxY = -32768;

  while (*s) {
    uint8_t c = (uint8_t)*s++;
    if (c >= first && c <= last) {
      GFXglyph* glyph = &(((GFXglyph*)pgm_read_ptr(&font->glyph))[c - first]);
      uint8_t gw = pgm_read_byte(&glyph->width);
      uint8_t gh = pgm_read_byte(&glyph->height);
      int8_t xo  = pgm_read_byte(&glyph->xOffset);
      int8_t yo  = pgm_read_byte(&glyph->yOffset);
      uint8_t xa = pgm_read_byte(&glyph->xAdvance);
      if (gw && gh) {
        int gx1 = x + xo, gy1 = yo;
        int gx2 = gx1 + gw, gy2 = gy1 + gh;
        if (gx1 < *minX) *minX = gx1;
        if (gy1 < *minY) *minY = gy1;
        if (gx2 > *maxX) *maxX = gx2;
        if (gy2 > *maxY) *maxY = gy2;
      }
      x += xa;
    }
  }
  if (*minX == 32767) { *minX = 0; *minY = 0; *maxX = 0; *maxY = 0; }
  if (advOut) *advOut = x;
}

int textW(const char* s, int scale) {
  int minX,minY,maxX,maxY,adv;
  textBoundsFont(s, scale, &minX, &minY, &maxX, &maxY, &adv);
  return max(maxX - minX, adv);
}

void drawGlyphFont(int x, int baseline, char ch, const GFXfont* font, uint8_t color, int* adv) {
  uint8_t first = pgm_read_byte(&font->first);
  uint8_t last  = pgm_read_byte(&font->last);
  if ((uint8_t)ch < first || (uint8_t)ch > last) { if (adv) *adv = 0; return; }

  GFXglyph* glyph = &(((GFXglyph*)pgm_read_ptr(&font->glyph))[(uint8_t)ch - first]);
  uint16_t bo = pgm_read_word(&glyph->bitmapOffset);
  uint8_t  w  = pgm_read_byte(&glyph->width);
  uint8_t  h  = pgm_read_byte(&glyph->height);
  uint8_t  xa = pgm_read_byte(&glyph->xAdvance);
  int8_t   xo = pgm_read_byte(&glyph->xOffset);
  int8_t   yo = pgm_read_byte(&glyph->yOffset);
  uint8_t* bitmap = (uint8_t*)pgm_read_ptr(&font->bitmap);

  uint8_t bits = 0, bit = 0;
  for (int yy=0; yy<h; yy++) {
    for (int xx=0; xx<w; xx++) {
      if (!(bit++ & 7)) bits = pgm_read_byte(&bitmap[bo++]);
      if (bits & 0x80) px(x + xo + xx, baseline + yo + yy, color);
      bits <<= 1;
    }
  }
  if (adv) *adv = xa;
}

void drawStr(int x, int y, const char* s, int scale, uint8_t color) {
  const GFXfont* font = uiFontForScale(scale);
  int minX,minY,maxX,maxY,advTotal;
  textBoundsFont(s, scale, &minX, &minY, &maxX, &maxY, &advTotal);
  int cursor   = x - minX;
  int baseline = y - minY;
  while (*s) {
    int adv = 0;
    drawGlyphFont(cursor, baseline, *s, font, color, &adv);
    cursor += adv;
    s++;
  }
}

void drawStrC(int cx, int y, const char* s, int scale, uint8_t color) {
  drawStr(cx - textW(s, scale) / 2, y, s, scale, color);
}

void drawStrFit(int x, int y, int maxW, const char* s, int scale, uint8_t color) {
  char buf[80];
  strncpy(buf, s ? s : "", sizeof(buf)-1);
  buf[sizeof(buf)-1] = 0;

  if (textW(buf, scale) <= maxW) { drawStr(x, y, buf, scale, color); return; }

  int len = strlen(buf);
  while (len > 0 && textW(buf, scale) > maxW) {
    len--;
    buf[len] = 0;
    if (len > 3) {
      buf[len-3] = '.'; buf[len-2] = '.'; buf[len-1] = '.'; buf[len] = 0;
    }
  }
  drawStr(x, y, buf, scale, color);
}

void drawStrInBox(int x, int y, int w, int h, const char* s, int scale, uint8_t color) {
  int fh = uiFontHeight(scale);
  drawStrC(x + w/2, y + (h - fh)/2, s, scale, color);
}

void uppercaseCopy(char* dst, const char* src, int maxLen) {
  int i=0; for(;i<maxLen-1&&src[i];i++){char c=src[i];if(c>='a'&&c<='z')c-=32;dst[i]=c;} dst[i]=0;
}

// ─── Text normalization + wrapped layout ──────────────────────────────────

String normalizeForDisplay(const String& in) {
  String s = in;
  s.replace("ä", "ae"); s.replace("ö", "oe"); s.replace("ü", "ue");
  s.replace("Ä", "Ae"); s.replace("Ö", "Oe"); s.replace("Ü", "Ue");
  s.replace("ß", "ss");
  s.replace("é", "e"); s.replace("è", "e"); s.replace("ê", "e");
  s.replace("á", "a"); s.replace("à", "a"); s.replace("â", "a");
  s.replace("ó", "o"); s.replace("ò", "o"); s.replace("ô", "o");
  s.replace("í", "i"); s.replace("ì", "i"); s.replace("î", "i");
  s.replace("ú", "u"); s.replace("ù", "u"); s.replace("û", "u");
  s.replace("ñ", "n"); s.replace("ç", "c");
  s.replace("\xe2\x80\x9c", "\""); s.replace("\xe2\x80\x9d", "\"");
  s.replace("\xe2\x80\x9e", "\"");
  s.replace("\xe2\x80\x98", "'");  s.replace("\xe2\x80\x99", "'");
  s.replace("\xe2\x80\x93", "-");  s.replace("\xe2\x80\x94", "-");
  s.replace("\xe2\x80\xa6", "...");
  String out = "";
  for (int i = 0; i < (int)s.length(); i++) {
    unsigned char c = (unsigned char)s[i];
    if (c >= 32 && c <= 126) out += (char)c;
    else if (c == '\n' || c == '\r' || c == '\t') out += ' ';
  }
  while (out.indexOf("  ") >= 0) out.replace("  ", " ");
  out.trim();
  return out;
}

int drawWrappedText(int x, int y, int maxW, int lineH, int maxLines,
                    const String& rawText, uint8_t color, int skipLines) {
  String text = normalizeForDisplay(rawText);
  text.replace("\n", " "); text.replace("\r", " ");

  int logical = 0, drawn = 0;
  String line = "";
  int pos = 0;

  while (pos < (int)text.length()) {
    while (pos < (int)text.length() && text[pos] == ' ') pos++;
    if (pos >= (int)text.length()) break;

    int nextSpace = text.indexOf(' ', pos);
    String word;
    if (nextSpace < 0) { word = text.substring(pos); pos = text.length(); }
    else               { word = text.substring(pos, nextSpace); pos = nextSpace + 1; }

    String candidate = line.length() ? line + " " + word : word;

    if (textW(candidate.c_str(), 1) <= maxW) { line = candidate; continue; }

    if (line.length()) {
      if (logical >= skipLines && drawn < maxLines) {
        String toDraw = line;
        if (drawn == maxLines - 1) {
          while (textW((toDraw + "...").c_str(), 1) > maxW && toDraw.length() > 0)
            toDraw.remove(toDraw.length() - 1);
          toDraw += "...";
        }
        drawStr(x, y + drawn * lineH, toDraw.c_str(), 1, color);
        drawn++;
      }
      logical++;
      line = word;
    } else {
      String part = word;
      while (textW((part + "-").c_str(), 1) > maxW && part.length() > 1)
        part.remove(part.length() - 1);

      if (part.length() <= 1) {
        if (logical >= skipLines && drawn < maxLines) {
          drawStrFit(x, y + drawn * lineH, maxW, word.c_str(), 1, color);
          drawn++;
        }
        logical++;
        line = "";
      } else {
        if (logical >= skipLines && drawn < maxLines) {
          drawStr(x, y + drawn * lineH, (part + "-").c_str(), 1, color);
          drawn++;
        }
        logical++;
        word = word.substring(part.length());
        line = word;
      }
    }
  }

  if (line.length()) {
    if (logical >= skipLines && drawn < maxLines)
      drawStr(x, y + drawn * lineH, line.c_str(), 1, color);
    logical++;
  }

  return logical;
}
