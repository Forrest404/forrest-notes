#pragma once
#include <Arduino.h>

void   loadIndex();
void   saveIndex();
void   addToIndex(int num, const char* tag, bool hasText);
void   updateIndexHasText(int num);
void   deleteNote(int num);
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
