#pragma once
#include <Arduino.h>

// Push transcribed notes to the user's Obsidian vault (a GitHub repo) as Markdown,
// with AI-extracted [[topic]] links and per-tag MOC index notes. Mirrors the
// offline-first transcription queue: only notes with a transcript that haven't been
// pushed yet are sent; failures stay pending and retry next sync. Safe no-op unless
// cfg::hasGithub(). Call after transcribeAll() while Wi-Fi is connected.
void obsidianSyncAll();

// Drain the pending-delete queue (/notes/tombs.csv): remove each note's .md from
// the vault and rebuild affected tag MOCs. Self-guards: no-op unless cfg::hasGithub()
// and Wi-Fi is connected. Failures stay queued and retry next call. Called from
// deleteNote() and at the top of obsidianSyncAll().
void obsidianFlushDeletes();
