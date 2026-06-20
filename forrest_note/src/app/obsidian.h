#pragma once
#include <Arduino.h>

// Push transcribed notes to the user's Obsidian vault (a GitHub repo) as Markdown,
// with AI-extracted [[topic]] links and per-tag MOC index notes. Mirrors the
// offline-first transcription queue: only notes with a transcript that haven't been
// pushed yet are sent; failures stay pending and retry next sync. Safe no-op unless
// cfg::hasGithub(). Call after transcribeAll() while Wi-Fi is connected.
void obsidianSyncAll();
