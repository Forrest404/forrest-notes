#include "Arduino.h"
#include "../../config.h"
#include "../../globals.h"
#include "../../types.h"
#include "record.h"
#include "SD_MMC.h"
#include "esp_heap_caps.h"
#include "notes.h"
#include "ui.h"
#include "../../sounds.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

extern "C" {
#include "../../src/audio/audio_bsp.h"
}

// Capture (producer) and SD-write (consumer) run on separate cores connected by
// a PSRAM ring buffer. The producer keeps draining the I2S DMA at line rate so a
// slow SD write only grows the ring instead of dropping samples.
struct RecCtx {
  RingbufHandle_t   ring;
  volatile bool     running;    // consumer -> producer: keep capturing
  volatile bool     finished;   // producer -> consumer: capture loop exited
};

static void recProducerTask(void* arg) {
  RecCtx* ctx = (RecCtx*)arg;
  int16_t* sbuf = (int16_t*)heap_caps_malloc(REC_BUF,   MALLOC_CAP_8BIT);
  int16_t* mbuf = (int16_t*)heap_caps_malloc(REC_BUF/2, MALLOC_CAP_8BIT);
  const int monoSamples = REC_BUF / 4;   // stereo int16 in -> mono int16 out

  if (sbuf && mbuf) {
    while (ctx->running) {
      audio_playback_read((void*)sbuf, REC_BUF);   // blocking read from codec DMA
      for (int i = 0; i < monoSamples; i++) mbuf[i] = sbuf[i * 2];  // left channel
      // Block briefly if the ring is full (SD catching up); never silently drop.
      xRingbufferSend(ctx->ring, mbuf, monoSamples * 2, pdMS_TO_TICKS(1000));
    }
  }

  if (sbuf) heap_caps_free(sbuf);
  if (mbuf) heap_caps_free(mbuf);
  ctx->finished = true;
  vTaskDelete(NULL);
}

bool record() {
  int num = nextNoteNumber();
  char path[64]; snprintf(path, sizeof(path), "%s/note_%03d.wav", NOTES_DIR, num);
  Serial.printf("[Rec] %s\n", path);

  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) return false;

  uint8_t header[44]={}; f.write(header, 44);

  RecCtx ctx;
  ctx.ring = xRingbufferCreateWithCaps(REC_RING_LEN, RINGBUF_TYPE_BYTEBUF, MALLOC_CAP_SPIRAM);
  if (!ctx.ring) { f.close(); return false; }
  ctx.running  = true;
  ctx.finished = false;

  TaskHandle_t producer = NULL;
  if (xTaskCreatePinnedToCore(recProducerTask, "recprod", 4096, &ctx, 6, &producer, 0) != pdPASS) {
    vRingbufferDeleteWithCaps(ctx.ring);
    f.close();
    return false;
  }

  uint32_t totalMono = 0, t0 = millis();
  int      recPeak = 0;   // peak |sample| since the last UI update

  auto drain = [&](TickType_t wait) -> bool {
    size_t got = 0;
    void* item = xRingbufferReceive(ctx.ring, &got, wait);
    if (!item) return false;
    int16_t* sp = (int16_t*)item;
    int ns = got / 2;
    for (int i = 0; i < ns; i++) { int a = abs(sp[i]); if (a > recPeak) recPeak = a; }
    size_t written = f.write((uint8_t*)item, got);
    vRingbufferReturnItem(ctx.ring, item);
    totalMono += written;
    return true;
  };

  // Record while held (min 500 ms), up to the hard duration cap. Drive the live
  // timer + level meter as fast as the panel will take it (~10x/sec); the partial
  // refresh is non-blocking and showRecordingLive() drops frames while it paints,
  // so a tight cadence just means the meter tracks the latest peak instead of lagging.
  uint32_t lastUi = 0;
  while ((digitalRead(BTN_REC) == LOW || millis() - t0 < 500) &&
         (millis() - t0 < MAX_REC_MS)) {
    drain(pdMS_TO_TICKS(40));
    uint32_t now = millis();
    if (now - lastUi >= 100) {
      lastUi = now;
      int lvl = (int)((long)recPeak * 152L * 3L / 32767L);   // ×3 boost for speech
      if (lvl > 152) lvl = 152;
      showRecordingLive(now - t0, lvl);
      recPeak = 0;
    }
  }

  // Stop the producer and flush everything still buffered.
  ctx.running = false;
  while (!ctx.finished) drain(pdMS_TO_TICKS(50));
  while (drain(0)) { /* final drain */ }

  vRingbufferDeleteWithCaps(ctx.ring);

  f.seek(0);
  uint32_t dB=totalMono, fS=dB+36, bR=SAMPLE_RATE*2;
  uint16_t bA=2,aF=1,ch=1,bps=16; uint32_t fL=16,sr=SAMPLE_RATE;
  f.write((uint8_t*)"RIFF",4); f.write((uint8_t*)&fS,4);
  f.write((uint8_t*)"WAVE",4); f.write((uint8_t*)"fmt ",4);
  f.write((uint8_t*)&fL,4);   f.write((uint8_t*)&aF,2);
  f.write((uint8_t*)&ch,2);   f.write((uint8_t*)&sr,4);
  f.write((uint8_t*)&bR,4);   f.write((uint8_t*)&bA,2);
  f.write((uint8_t*)&bps,2);
  f.write((uint8_t*)"data",4); f.write((uint8_t*)&dB,4);
  f.close();

  lastRecNum = num;
  Serial.printf("[Rec] done: %lu bytes\n", (unsigned long)totalMono);
  return totalMono > 1000;
}

bool playWavFile(const char* path) {
  File f = SD_MMC.open(path);
  if (!f) return false;
  if (f.size() <= 44) { f.close(); return false; }

  f.seek(44);

  const int monoBytes = 1024;
  uint8_t* monoBuf   = (uint8_t*)heap_caps_malloc(monoBytes,     MALLOC_CAP_8BIT);
  int16_t* stereoBuf = (int16_t*)heap_caps_malloc(monoBytes * 2, MALLOC_CAP_8BIT);

  if (!monoBuf || !stereoBuf) {
    if (monoBuf)   heap_caps_free(monoBuf);
    if (stereoBuf) heap_caps_free(stereoBuf);
    f.close();
    return false;
  }

  audioPlaying  = true;
  stopPlayback  = false;

  palaSoundSetEnabled(false);
  audio_playback_set_vol(85);

  while (f.available() && !stopPlayback) {
    int readBytes = f.read(monoBuf, monoBytes);
    if (readBytes <= 0) break;
    if (readBytes & 1) readBytes--;

    int samples = readBytes / 2;
    int16_t* mono = (int16_t*)monoBuf;
    for (int i = 0; i < samples; i++) {
      int16_t s = mono[i];
      stereoBuf[i * 2 + 0] = s;
      stereoBuf[i * 2 + 1] = s;
    }
    audio_playback_write((void*)stereoBuf, (uint32_t)(samples * 2 * sizeof(int16_t)));

    if (digitalRead(BTN_REC) == LOW) {
      delay(20);
      if (digitalRead(BTN_REC) == LOW) {
        while (digitalRead(BTN_REC) == LOW) delay(5);
        stopPlayback = true;
      }
    }
  }

  audio_playback_set_vol(0);
  palaSoundSetEnabled(true);

  heap_caps_free(monoBuf);
  heap_caps_free(stereoBuf);
  f.close();

  audioPlaying = false;
  stopPlayback = false;
  return true;
}
