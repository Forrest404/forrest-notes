#pragma once

#include <Arduino.h>
#include <math.h>
#include "esp_heap_caps.h"

#ifndef SAMPLE_RATE
#define SAMPLE_RATE 16000
#endif

extern "C" {
#include "src/audio/audio_bsp.h"
}

// Global sound switch.
// Used by the .ino to disable sounds during recording.
static bool palaSoundEnabled = true;

inline void palaSoundSetEnabled(bool enabled) {
  palaSoundEnabled = enabled;
  if (!enabled) {
    audio_playback_set_vol(0);
  }
}

inline bool palaSoundIsEnabled() {
  return palaSoundEnabled;
}

inline void soundEnable() {
  if (!palaSoundEnabled) return;
  audio_playback_set_vol(75);
}

inline void soundDisable() {
  delay(20);
  audio_playback_set_vol(0);
}

inline void playToneUI(float freq, int durationMs, float volume) {
  if (!palaSoundEnabled) return;

  const int sampleRate = SAMPLE_RATE;
  const int channels = 2;

  int frames = max(1, (sampleRate * durationMs) / 1000);
  size_t bytes = frames * channels * sizeof(int16_t);

  int16_t* buffer = (int16_t*)heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
  if (!buffer) return;

  for (int i = 0; i < frames; i++) {
    float phase = (2.0f * PI * freq * (float)i) / (float)sampleRate;

    // Hard attack + quick decay for a clickier UI sound
    float env = 1.0f - ((float)i / (float)frames);
    if (env < 0.0f) env = 0.0f;

    float wave = sinf(phase);
    float noise = ((float)random(-1000, 1000) / 1000.0f) * 0.35f;

    int16_t sample = (int16_t)(
      (wave * 0.65f + noise) *
      32767.0f *
      volume *
      env
    );

    buffer[i * 2 + 0] = sample;
    buffer[i * 2 + 1] = sample;
  }

  audio_playback_write((void*)buffer, bytes);
  heap_caps_free(buffer);
}

inline void soundNext() {
  if (!palaSoundEnabled) return;
  soundEnable();
  playToneUI(1100.0f, 10, 0.11f);
  soundDisable();
}

inline void soundSelect() {
  if (!palaSoundEnabled) return;
  soundEnable();
  playToneUI(1450.0f, 14, 0.13f);
  soundDisable();
}

inline void soundBack() {
  if (!palaSoundEnabled) return;
  soundEnable();
  playToneUI(650.0f, 18, 0.10f);
  soundDisable();
}



inline void soundRecordingStart() {
  if (!palaSoundEnabled) return;
  soundEnable();
  // Short upward tactile cue before recording starts.
  playToneUI(760.0f, 18, 0.12f);
  delay(8);
  playToneUI(1180.0f, 24, 0.10f);
  soundDisable();
}

inline void soundSaved() {
  if (!palaSoundEnabled) return;
  soundEnable();
  playToneUI(1040.0f, 45, 0.11f);
  delay(18);
  playToneUI(1560.0f, 65, 0.08f);
  soundDisable();
}

inline void soundSuccess() {
  if (!palaSoundEnabled) return;
  soundEnable();
  playToneUI(880.0f, 35, 0.10f);
  delay(12);
  playToneUI(1320.0f, 50, 0.08f);
  delay(12);
  playToneUI(1760.0f, 60, 0.055f);
  soundDisable();
}

inline void soundDelete() {
  if (!palaSoundEnabled) return;
  soundEnable();
  playToneUI(520.0f, 30, 0.11f);
  delay(10);
  playToneUI(260.0f, 55, 0.08f);
  soundDisable();
}
