#include "Arduino.h"
#include "../../config.h"
#include "../../globals.h"
#include "../../types.h"
#include "notes.h"
#include "rtc.h"
#include "draw.h"
#include "SD_MMC.h"

// ─── Index ────────────────────────────────────────────────────────────────

void loadIndex() {
  noteIndex.clear();
  File f = SD_MMC.open(INDEX_FILE);
  if (!f) return;
  while (f.available()) {
    String ln = f.readStringUntil('\n'); ln.trim();
    if (!ln.length()) continue;
    int c1=ln.indexOf(','), c2=ln.indexOf(',',c1+1);
    if (c1<0||c2<0) continue;
    NoteEntry e;
    e.num = ln.substring(0,c1).toInt();
    strncpy(e.tag, ln.substring(c1+1,c2).c_str(), 31);
    e.tag[31]='\0';
    e.hasText = (ln.substring(c2+1).toInt()==1);
    noteIndex.push_back(e);
  }
  f.close();
}

void saveIndex() {
  const char* tmp = "/notes/index.tmp";
  if (SD_MMC.exists(tmp)) SD_MMC.remove(tmp);
  File f = SD_MMC.open(tmp, FILE_WRITE);
  if (!f) return;
  for (int i=0; i<(int)noteIndex.size(); i++)
    f.printf("%d,%s,%d\n", noteIndex[i].num, noteIndex[i].tag, noteIndex[i].hasText?1:0);
  f.close();
  if (SD_MMC.exists(INDEX_FILE)) SD_MMC.remove(INDEX_FILE);
  SD_MMC.rename(tmp, INDEX_FILE);
}

void addToIndex(int num, const char* tag, bool hasText) {
  NoteEntry e;
  e.num = num;
  strncpy(e.tag, tag, 31);
  e.tag[31]='\0';
  e.hasText = hasText;
  noteIndex.push_back(e);
  saveIndex();
}

void updateIndexHasText(int num) {
  const char* foundTag = "";
  for (int i=0; i<(int)noteIndex.size(); i++) {
    if (noteIndex[i].num==num) {
      noteIndex[i].hasText=true;
      foundTag = noteIndex[i].tag;
      break;
    }
  }
  saveIndex();
  writeNoteMeta(num, foundTag);
}

void deleteNote(int num) {
  const char* exts[] = {"wav", "txt", "meta", nullptr};
  char path[64];
  for (int e = 0; exts[e]; e++) {
    snprintf(path, sizeof(path), "%s/note_%03d.%s", NOTES_DIR, num, exts[e]);
    if (SD_MMC.exists(path)) SD_MMC.remove(path);
  }
  for (int i = 0; i < (int)noteIndex.size(); i++) {
    if (noteIndex[i].num == num) {
      noteIndex.erase(noteIndex.begin() + i);
      break;
    }
  }
  saveIndex();
}

int nextNoteNumber() {
  int maxNum = 0;
  for (int i=0; i<(int)noteIndex.size(); i++) if (noteIndex[i].num > maxNum) maxNum = noteIndex[i].num;
  return maxNum + 1;
}

void saveTag(int num, const char* tag) {
  writeNoteMeta(num, tag);
  addToIndex(num, tag, false);
}

// ─── Tags ─────────────────────────────────────────────────────────────────

void saveTagsToFile() {
  const char* tmp = "/notes/tags.tmp";
  if (SD_MMC.exists(tmp)) SD_MMC.remove(tmp);
  File f = SD_MMC.open(tmp, FILE_WRITE);
  if (!f) return;
  for (int i = 0; i < tagCount; i++) if (strlen(tags[i]) > 0) f.println(tags[i]);
  f.close();
  if (SD_MMC.exists(TAG_FILE)) SD_MMC.remove(TAG_FILE);
  SD_MMC.rename(tmp, TAG_FILE);
}

void createDefaultTags() {
  tagCount = 0;
  for (int i = 0; i < DEFAULT_TAG_COUNT && tagCount < MAX_TAGS; i++) {
    strncpy(tags[tagCount], DEFAULT_TAGS[i], 31);
    tags[tagCount][31] = 0;
    tagCount++;
  }
  saveTagsToFile();
}

void loadTags() {
  tagCount = 0;
  if (!SD_MMC.exists(TAG_FILE)) { createDefaultTags(); return; }
  File f = SD_MMC.open(TAG_FILE);
  if (!f) { createDefaultTags(); return; }
  while (f.available() && tagCount < MAX_TAGS) {
    String line = f.readStringUntil('\n'); line.trim();
    if (!line.length()) continue;
    bool exists = false;
    for (int i = 0; i < tagCount; i++) {
      if (strcasecmp(tags[i], line.c_str()) == 0) { exists = true; break; }
    }
    if (exists) continue;
    strncpy(tags[tagCount], line.c_str(), 31);
    tags[tagCount][31] = 0;
    tagCount++;
  }
  f.close();
  if (tagCount == 0) createDefaultTags();
}

bool addCustomTag(const char* newTag) {
  if (!newTag) return false;
  if (tagCount >= MAX_TAGS) return false;
  String clean = String(newTag); clean.trim();
  clean.replace(",", " "); clean.replace("\n", " "); clean.replace("\r", " ");
  while (clean.indexOf("  ") >= 0) clean.replace("  ", " ");
  if (clean.length() < 1) return false;
  if (clean.length() > 31) clean = clean.substring(0, 31);
  for (int i = 0; i < tagCount; i++)
    if (strcasecmp(tags[i], clean.c_str()) == 0) return false;
  strncpy(tags[tagCount], clean.c_str(), 31);
  tags[tagCount][31] = 0;
  tagCount++;
  saveTagsToFile();
  return true;
}

bool tagHasNotes(const char* tag) {
  if (!tag) return false;
  for (int i = 0; i < (int)noteIndex.size(); i++)
    if (strcmp(noteIndex[i].tag, tag) == 0) return true;
  return false;
}

void replaceTagOnNotes(const char* oldTag, const char* newTag) {
  if (!oldTag || !newTag) return;
  for (int i = 0; i < (int)noteIndex.size(); i++) {
    if (strcmp(noteIndex[i].tag, oldTag) == 0) {
      strncpy(noteIndex[i].tag, newTag, 31);
      noteIndex[i].tag[31] = 0;
      markNoteObsidianPushed(noteIndex[i].num, false);  // re-sync with corrected tag
    }
  }
  saveIndex();
}

bool deleteTag(const char* tagName) {
  if (!tagName) return false;
  if (strcasecmp(tagName, "Untagged") == 0) return false;

  bool hadNotes = tagHasNotes(tagName);
  if (hadNotes) {
    bool hasUntagged = false;
    for (int i = 0; i < tagCount; i++)
      if (strcasecmp(tags[i], "Untagged") == 0) { hasUntagged = true; break; }
    if (!hasUntagged && tagCount < MAX_TAGS) {
      strncpy(tags[tagCount], "Untagged", 31);
      tags[tagCount][31] = 0;
      tagCount++;
    }
    replaceTagOnNotes(tagName, "Untagged");
  }

  int removeIdx = -1;
  for (int i = 0; i < tagCount; i++)
    if (strcmp(tags[i], tagName) == 0) { removeIdx = i; break; }
  if (removeIdx < 0) return false;

  for (int i = removeIdx; i < tagCount - 1; i++) strcpy(tags[i], tags[i + 1]);
  tagCount--;

  if (tagCount <= 0) createDefaultTags();
  else               saveTagsToFile();
  return true;
}

// ─── Meta ─────────────────────────────────────────────────────────────────

String noteMetaPath(int num) {
  char path[64];
  snprintf(path, sizeof(path), "%s/note_%03d.meta", NOTES_DIR, num);
  return String(path);
}

String readNoteMetaValue(int num, const char* key) {
  String path = noteMetaPath(num);
  File f = SD_MMC.open(path.c_str());
  if (!f) return "";
  String prefix = String(key) + "=";
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.startsWith(prefix)) { f.close(); return line.substring(prefix.length()); }
  }
  f.close();
  return "";
}

void writeNoteMeta(int num, const char* tag) {
  String path = noteMetaPath(num);
  String existingCreated = readNoteMetaValue(num, "created_utc");
  String created = existingCreated.length() ? existingCreated : currentUtcIso();
  String existingObs = readNoteMetaValue(num, "obsidian");   // preserve push state
  File f = SD_MMC.open(path.c_str(), FILE_WRITE);
  if (!f) return;
  f.print("created_utc="); f.println(created);
  f.print("tag="); f.println(tag ? tag : "");
  f.print("synced=");
  bool hasText = false;
  for (int i = 0; i < (int)noteIndex.size(); i++)
    if (noteIndex[i].num == num) { hasText = noteIndex[i].hasText; break; }
  f.println(hasText ? "1" : "0");
  f.print("obsidian="); f.println(existingObs == "1" ? "1" : "0");
  f.close();
}

// ── Obsidian/GitHub push state (stored in .meta) ────────────────────────────
bool noteObsidianPushed(int num) {
  return readNoteMetaValue(num, "obsidian") == "1";
}

void markNoteObsidianPushed(int num, bool pushed) {
  // Rewrite .meta preserving the other fields (FILE_WRITE truncates on ESP32 FS).
  String created = readNoteMetaValue(num, "created_utc");
  String tag     = readNoteMetaValue(num, "tag");
  String synced  = readNoteMetaValue(num, "synced");
  File f = SD_MMC.open(noteMetaPath(num).c_str(), FILE_WRITE);
  if (!f) return;
  f.print("created_utc="); f.println(created);
  f.print("tag=");         f.println(tag);
  f.print("synced=");      f.println(synced.length() ? synced : "0");
  f.print("obsidian=");    f.println(pushed ? "1" : "0");
  f.close();
}

// ─── Time helpers ─────────────────────────────────────────────────────────

String noteCreatedUtc(int num) {
  return readNoteMetaValue(num, "created_utc");
}

String utcToLocalDeviceLabel(const String& utcIso) {
  if (utcIso.length() < 16) return "time not set";
  struct tm utc;
  memset(&utc, 0, sizeof(struct tm));
  utc.tm_year = utcIso.substring(0, 4).toInt() - 1900;
  utc.tm_mon  = utcIso.substring(5, 7).toInt() - 1;
  utc.tm_mday = utcIso.substring(8, 10).toInt();
  utc.tm_hour = utcIso.substring(11, 13).toInt();
  utc.tm_min  = utcIso.substring(14, 16).toInt();
  utc.tm_sec  = 0;
  time_t epoch = utcTmToEpoch(utc);
  epoch += (LOCAL_TIME_OFFSET_MIN * 60);
  struct tm localTm;
  gmtime_r(&epoch, &localTm);
  char buf[22];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &localTm);
  return String(buf);
}

String noteCreatedDeviceLabel(int num) {
  String utc = noteCreatedUtc(num);
  if (utc.length() < 16) return "time not set";
  return utcToLocalDeviceLabel(utc);
}

String currentUtcIso() {
  String fromRtc = rtcUtcIso();
  if (fromRtc.length() > 0) { timeReady = true; return fromRtc; }
  time_t now = time(nullptr);
  if (!timeReady || now < 1700000000) return "";
  struct tm utc;
  gmtime_r(&now, &utc);
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return String(buf);
}

// ─── Ticker + preview ─────────────────────────────────────────────────────

String notePreviewText(int num, size_t maxLen) {
  char txtPath[64];
  snprintf(txtPath, sizeof(txtPath), "%s/note_%03d.txt", NOTES_DIR, num);
  File f = SD_MMC.open(txtPath);
  if (!f) return "";
  String out = "";
  while (f.available() && out.length() < maxLen) {
    char c = (char)f.read();
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    out += c;
  }
  f.close();
  return normalizeForDisplay(out);
}

String noteTickerText(int idx) {
  if (idx < 0) return "";
  String dt      = noteCreatedDeviceLabel(noteIndex[idx].num);
  String preview = notePreviewText(noteIndex[idx].num, 130);
  if (preview.length() == 0)
    preview = noteIndex[idx].hasText ? "empty note" : "not synced";
  String ticker = dt;
  if (ticker == "time not set") ticker = preview;
  else { ticker += "  -  "; ticker += preview; }
  return normalizeForDisplay(ticker);
}

bool activeTickerNeedsScroll(int cursor) {
  int count = filteredCount();
  if (count <= 0) return false;
  int idx = noteAtFilteredIndex(cursor);
  if (idx < 0) return false;
  String ticker = noteTickerText(idx);
  return textW(ticker.c_str(), 1) > 145;
}

void drawTickerText(int x, int y, int maxW, const String& rawText, bool active, uint8_t color) {
  String text = normalizeForDisplay(rawText);
  if (textW(text.c_str(), 1) <= maxW || !active) {
    drawStrFit(x, y, maxW, text.c_str(), 1, color);
    return;
  }
  String spacer = "     ";
  String loopText = text + spacer + text;
  int cycleLen = text.length() + spacer.length();
  int start = tickerOffset % max(1, cycleLen);
  String window = "";
  for (int i = start; i < (int)loopText.length(); i++) {
    String candidate = window + loopText[i];
    if (textW(candidate.c_str(), 1) > maxW) break;
    window = candidate;
  }
  if (window.length() == 0) drawStrFit(x, y, maxW, text.c_str(), 1, color);
  else                      drawStr(x, y, window.c_str(), 1, color);
}

// ─── Filtering ────────────────────────────────────────────────────────────

int filteredCount() {
  if (activeFilter < 0) return (int)noteIndex.size();
  int c = 0;
  for (int i=0; i<(int)noteIndex.size(); i++)
    if (strcmp(noteIndex[i].tag, tags[activeFilter])==0) c++;
  return c;
}

int noteAtFilteredIndex(int visIdx) {
  std::vector<int> matches;
  for (int i=0; i<(int)noteIndex.size(); i++)
    if (activeFilter<0 || strcmp(noteIndex[i].tag, tags[activeFilter])==0)
      matches.push_back(i);
  int rev = (int)matches.size() - 1 - visIdx;
  if (rev < 0 || rev >= (int)matches.size()) return -1;
  return matches[rev];
}
