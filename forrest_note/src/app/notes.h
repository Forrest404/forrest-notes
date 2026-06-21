#pragma once
#include <Arduino.h>

void   loadIndex();
void   saveIndex();
void   addToIndex(int num, const char* tag, bool hasText);
void   updateIndexHasText(int num);
void   deleteNote(int num);
int    deleteAllNotes(bool alsoVault = false);   // wipe all notes off SD; alsoVault also queues vault deletes. returns count
int    nextNoteNumber();
void   saveTag(int num, const char* tag);

void   loadTags();
void   saveTagsToFile();
void   createDefaultTags();
bool   addCustomTag(const char* newTag);
bool   deleteTag(const char* tagName);
bool   tagHasNotes(const char* tag);
void   replaceTagOnNotes(const char* oldTag, const char* newTag);

String noteMetaPath(int num);
String readNoteMetaValue(int num, const char* key);
void   writeNoteMeta(int num, const char* tag);
bool   noteObsidianPushed(int num);
void   markNoteObsidianPushed(int num, bool pushed);

// Vault identity: each note is pushed under a unique, never-reused filename stem
// keyed on its creation date+time, so two notes can share a title yet link
// separately and a reused note number can never re-link to a deleted note.
String noteUid(int num);                                            // vault filename stem (no .md)
String noteTitle(int num);                                          // AI title frozen at push (for [[uid|title]] display)
void   freezeVaultMeta(int num, const String& uid, const String& title);

String noteCreatedUtc(int num);
String utcToLocalDeviceLabel(const String& utcIso);
String noteCreatedDeviceLabel(int num);
String currentUtcIso();

String notePreviewText(int num, size_t maxLen = 90);
String noteTickerText(int idx);
bool   activeTickerNeedsScroll(int cursor);
void   drawTickerText(int x, int y, int maxW, const String& rawText, bool active, uint8_t color);

int  filteredCount();
int  noteAtFilteredIndex(int visIdx);
