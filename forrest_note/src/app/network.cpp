#include "Arduino.h"
#include "../../config.h"
#include "../../globals.h"
#include "../../types.h"
#include "network.h"
#include "notes.h"
#include "rtc.h"
#include "ui.h"
#include "config_store.h"
#include "WiFi.h"
#include "WiFiClientSecure.h"
#include <WebServer.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include "SD_MMC.h"
#include "esp_heap_caps.h"
#include "../../secrets.h"

// IDF built-in Mozilla CA root bundle (libmbedtls.a). Auto-maintained with the
// esp32 core, so server certs validate without shipping/rotating a pinned PEM.
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_bundle_end[]   asm("_binary_x509_crt_bundle_end");

static bool transcribeOnce(const String& wavPath, int noteNum) {
  String oaiKey = cfg::openaiKey();
  if (oaiKey.length() == 0) { Serial.println("[Whisper] no API key set"); return false; }

  File f = SD_MMC.open(wavPath.c_str());
  if (!f) return false;
  size_t fileSize = f.size();

  String bnd = "----PalaBoundary";
  String pre = "--" + bnd + "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-1\r\n"
               "--" + bnd + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"note.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
  String post = "\r\n--" + bnd + "--\r\n";
  size_t totalLen = pre.length() + fileSize + post.length();

  WiFiClientSecure client;
  client.setCACertBundle(x509_crt_bundle_start,
                         (size_t)(x509_crt_bundle_end - x509_crt_bundle_start));
  client.setHandshakeTimeout(15);  // seconds

  if (!client.connect("api.openai.com", 443, 15000 /* ms */)) { f.close(); return false; }

  client.printf("POST /v1/audio/transcriptions HTTP/1.1\r\n"
                "Host: api.openai.com\r\n"
                "Authorization: Bearer %s\r\n"
                "Content-Type: multipart/form-data; boundary=%s\r\n"
                "Content-Length: %u\r\n"
                "Connection: close\r\n\r\n",
                oaiKey.c_str(), bnd.c_str(), (unsigned)totalLen);
  client.print(pre);

  uint8_t* chunk = (uint8_t*)heap_caps_malloc(4096, MALLOC_CAP_8BIT);
  if (!chunk) { f.close(); client.stop(); return false; }
  while (f.available()) {
    int n = f.read(chunk, 4096);
    if (n <= 0) break;
    client.write(chunk, n);
  }
  heap_caps_free(chunk);
  f.close();
  client.print(post);

  uint32_t deadline = millis() + 90000;
  while (!client.available() && millis() < deadline) delay(20);

  String resp = "";
  bool inBody = false;
  while (client.available() || (client.connected() && millis() < deadline)) {
    if (!client.available()) { delay(10); continue; }
    String line = client.readStringUntil('\n');
    if (!inBody) {
      if (line == "\r" || line == "") inBody = true;
      if (line.startsWith("HTTP/") && line.indexOf(" 200 ") < 0) {
        Serial.printf("[Whisper] %s\n", line.c_str());
        client.stop(); return false;
      }
    } else {
      resp += line;
      if (resp.length() > 131072) break;   // safety bound on body size
    }
  }
  client.stop();

  // Robust JSON parse of {"text":"..."} — handles \uXXXX, escapes, and long
  // transcripts that the old hand-rolled scanner would corrupt or truncate.
  DynamicJsonDocument doc(resp.length() + 1024);
  DeserializationError jerr = deserializeJson(doc, resp);
  if (jerr) { Serial.printf("[Whisper] json: %s\n", jerr.c_str()); return false; }
  String text = doc["text"] | "";
  if (text.length() == 0) { Serial.println("[Whisper] empty response"); return false; }

  String tp = wavPath; tp.replace(".wav", ".txt");
  File tf = SD_MMC.open(tp.c_str(), FILE_WRITE);
  if (tf) { tf.print(text); tf.close(); }

  updateIndexHasText(noteNum);
  return true;
}

bool transcribe(const String& wavPath, int noteNum) {
  for (int attempt = 0; attempt < 3; attempt++) {
    if (WiFi.status() != WL_CONNECTED) return false;   // offline: keep note queued, don't burn retries
    if (transcribeOnce(wavPath, noteNum)) return true;
    if (attempt < 2) { Serial.printf("[Whisper] retry %d/2\n", attempt + 1); delay(3000); }
  }
  return false;
}

// Offline-first queue: notes with hasText==false are the pending work. This drains
// them while online; any that fail (no Wi-Fi, API error) simply stay pending and
// their WAV is preserved for the next sync. Nothing is ever lost on failure.
void transcribeAll() {
  if (!cfg::hasOpenAiKey()) { Serial.println("[Whisper] no API key; skipping sync"); return; }

  int pending = 0;
  for (int i=0; i<(int)noteIndex.size(); i++) if(!noteIndex[i].hasText) pending++;
  int done = 0;
  for (int i=0; i<(int)noteIndex.size(); i++) {
    if (noteIndex[i].hasText) continue;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.printf("[Whisper] wifi lost; %d note(s) stay pending\n", pending - done);
      break;
    }
    showTranscribing(done, pending);
    char wp[64]; snprintf(wp, sizeof(wp), "%s/note_%03d.wav", NOTES_DIR, noteIndex[i].num);
    if (transcribe(String(wp), noteIndex[i].num)) done++;
  }
  Serial.printf("[Whisper] synced %d/%d pending\n", done, pending);
}

// ─── Portal helpers ────────────────────────────────────────────────────────

String htmlEscape(const String& s) {
  String out = s;
  out.replace("&", "&amp;"); out.replace("<", "&lt;");
  out.replace(">", "&gt;"); out.replace("\"", "&quot;");
  return out;
}

String readSmallFile(const char* path, size_t maxLen) {
  File f = SD_MMC.open(path);
  if (!f) return "";
  String out;
  while (f.available() && out.length() < maxLen) out += (char)f.read();
  f.close();
  return out;
}

String urlDecodeSimple(String s) {
  s.replace("+", " ");
  String out = "";
  for (int i = 0; i < (int)s.length(); i++) {
    if (s[i] == '%' && i + 2 < (int)s.length()) {
      String hex = s.substring(i + 1, i + 3);
      out += (char)strtol(hex.c_str(), nullptr, 16);
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

String portalCss() {
  return String(
    "<style>"
    ":root{font-family:-apple-system,BlinkMacSystemFont,'Inter','Segoe UI',sans-serif;color:#111;background:#f3f0e9;}"
    "body{margin:0;padding:24px;background:#f3f0e9;}"
    ".wrap{max-width:780px;margin:0 auto;}"
    ".top{display:flex;align-items:flex-end;justify-content:space-between;gap:16px;margin-bottom:24px;}"
    "h1{font-size:44px;letter-spacing:-.06em;line-height:.9;margin:0;font-weight:800;}"
    ".sub{font-size:13px;text-transform:uppercase;letter-spacing:.12em;color:#6a665f;margin-top:10px;}"
    ".pill{display:inline-flex;border:1px solid #111;border-radius:999px;padding:8px 12px;font-size:13px;background:#fffaf1;}"
    ".grid{display:grid;grid-template-columns:1fr;gap:14px;}"
    ".card{background:#fffaf1;border:1.5px solid #111;border-radius:24px;padding:18px;box-shadow:4px 4px 0 #111;}"
    ".row{display:flex;justify-content:space-between;gap:16px;align-items:flex-start;}"
    ".num{font-size:13px;letter-spacing:.08em;text-transform:uppercase;color:#6a665f;margin-bottom:8px;}"
    ".date{font-size:13px;color:#6a665f;margin:-4px 0 12px;}"
    ".title{font-size:24px;line-height:1.05;letter-spacing:-.04em;font-weight:750;margin:0 0 12px;}"
    ".tag{border:1px solid #111;border-radius:999px;padding:5px 9px;font-size:12px;white-space:nowrap;background:#111;color:#fff;}"
    ".text{font-size:15px;line-height:1.45;color:#222;margin:0 0 14px;white-space:pre-wrap;}"
    ".actions{display:flex;flex-wrap:wrap;gap:8px;margin-top:14px;}"
    "a.btn{color:#111;text-decoration:none;border:1px solid #111;border-radius:999px;padding:8px 12px;background:#f3f0e9;font-size:13px;}"
    "a.btn.primary{background:#111;color:#fff;}"
    ".empty{border:1.5px dashed #111;border-radius:24px;padding:34px;text-align:center;color:#6a665f;}"
    "audio{width:100%;margin-top:8px;}"
    "@media(max-width:520px){body{padding:16px}h1{font-size:36px}.card{border-radius:20px}.title{font-size:21px}}"
    "</style>"
  );
}

// ─── Portal handlers ───────────────────────────────────────────────────────

void handlePortalRoot() {
  loadIndex();

  Serial.println("[HTTP] GET /");
  String filter = "All";
  if (transferServer.hasArg("tag")) filter = transferServer.arg("tag");

  // Stream the page in bounded chunks so RAM use stays flat regardless of how
  // many notes exist (a single accumulated String would grow unboundedly).
  transferServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  transferServer.send(200, "text/html", "");

  String html = "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Forrest Portal</title>" + portalCss() + "</head><body><div class='wrap'>";

  html += "<div class='top'><div><h1>forrest<br>portal</h1>"
          "<div class='sub'>local note transfer · <a href=\"/tags\" style=\"color:inherit\">tags</a> · <a href=\"/provision\" style=\"color:inherit\">setup</a> · <a href=\"/ota\" style=\"color:inherit\">update</a></div></div>"
          "<div class='pill'>" + String((int)noteIndex.size()) + " notes</div></div>";

  html += "<div class='actions' style='margin-bottom:18px'>";
  html += "<a class='btn " + String(filter == "All" ? "primary" : "") + "' href='/'>All</a>";
  for (int t = 0; t < tagCount; t++) {
    String tag = String(tags[t]);
    html += "<a class='btn " + String(filter == tag ? "primary" : "") + "' href='/?tag=" + tag + "'>" + htmlEscape(tag) + "</a>";
  }
  html += "</div>";

  html += "<div class='actions' style='margin-bottom:24px'>";
  html += "<a class='btn primary' href='/export.txt'>Download all TXT</a>";
  if (filter != "All")
    html += "<a class='btn' href='/export.txt?tag=" + filter + "'>Download " + htmlEscape(filter) + " TXT</a>";
  html += "</div>";

  int visibleCount = 0;
  for (int i = 0; i < (int)noteIndex.size(); i++)
    if (filter == "All" || filter == String(noteIndex[i].tag)) visibleCount++;

  if (visibleCount <= 0) {
    html += "<div class='empty'>No notes for this filter.</div>";
  } else {
    html += "<div class='grid'>";
    for (int v = 0; v < (int)noteIndex.size(); v++) {
      int i = (int)noteIndex.size() - 1 - v;
      if (!(filter == "All" || filter == String(noteIndex[i].tag))) continue;
      int num = noteIndex[i].num;

      char txtPath[64], wavPath[64];
      snprintf(txtPath, sizeof(txtPath), "%s/note_%03d.txt", NOTES_DIR, num);
      snprintf(wavPath, sizeof(wavPath), "%s/note_%03d.wav", NOTES_DIR, num);

      String transcript = readSmallFile(txtPath, 1200);
      if (transcript.length() == 0)
        transcript = noteIndex[i].hasText ? "(empty transcript)" : "Not transcribed yet.";

      String title = transcript; title.replace("\n", " "); title.trim();
      if (title.length() > 58) title = title.substring(0, 58) + "...";
      if (title.length() == 0 || title == "Not transcribed yet.")
        title = String("Voice note ") + String(num);

      html += "<div class='card'>";
      html += "<div class='row'><div><div class='num'>#" + String(num) + "</div>";
      html += "<h2 class='title'>" + htmlEscape(title) + "</h2>";
      String createdUtc = noteCreatedUtc(num);
      if (createdUtc.length() > 0)
        html += "<div class='date' data-utc='" + createdUtc + "'>" + createdUtc + "</div>";
      else
        html += "<div class='date'>time not set</div>";
      html += "</div>";
      html += "<div class='tag'>" + htmlEscape(String(noteIndex[i].tag)) + "</div></div>";
      html += "<p class='text'>" + htmlEscape(transcript) + "</p>";
      if (SD_MMC.exists(wavPath))
        html += "<audio controls src='/audio?num=" + String(num) + "'></audio>";
      html += "<div class='actions'>";
      html += "<a class='btn primary' href='/txt?num=" + String(num) + "'>Download TXT</a>";
      if (SD_MMC.exists(wavPath))
        html += "<a class='btn' href='/wav?num=" + String(num) + "'>Download WAV</a>";
      html += "<a class='btn' style='margin-left:auto;color:#c0392b;border-color:#c0392b' "
              "href='/note/delete?num=" + String(num) + "' "
              "onclick=\"return confirm('Delete note #" + String(num) + "? This cannot be undone.')\">Delete</a>";
      html += "</div></div>";
      if (html.length() > 2048) { transferServer.sendContent(html); html = ""; }
    }
    html += "</div>";
  }

  html += "<script>"
          "document.querySelectorAll('[data-utc]').forEach(function(el){"
          "var d=new Date(el.dataset.utc);"
          "if(!isNaN(d)){el.textContent=d.toLocaleString([],{year:'numeric',month:'short',day:'2-digit',hour:'2-digit',minute:'2-digit'});}"
          "});"
          "</script>";
  html += "</div></body></html>";
  transferServer.sendContent(html);
  transferServer.sendContent("");   // terminate chunked response
}

void handlePortalJson() {
  loadIndex();
  String json = "[";
  for (int v = 0; v < (int)noteIndex.size(); v++) {
    int i = (int)noteIndex.size() - 1 - v;
    if (v > 0) json += ",";
    json += "{";
    json += "\"num\":" + String(noteIndex[i].num) + ",";
    json += "\"tag\":\"" + String(noteIndex[i].tag) + "\",";
    json += "\"hasText\":" + String(noteIndex[i].hasText ? "true" : "false");
    json += "}";
  }
  json += "]";
  transferServer.send(200, "application/json", json);
}

void handleExportTxt() {
  loadIndex();
  String filter = "All";
  if (transferServer.hasArg("tag")) filter = transferServer.arg("tag");

  String filename = "forrest_notes_export";
  if (filter != "All") filename += "_" + filter;
  filename += ".txt";

  // Stream chunked so the full export never has to fit in RAM at once (the old
  // path capped at 55 KB and truncated). No cap now — all notes are exported.
  transferServer.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  transferServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  transferServer.send(200, "text/plain", "");

  String chunk = "Forrest Note Export\nFilter: " + filter + "\n------------------------------\n\n";

  for (int v = 0; v < (int)noteIndex.size(); v++) {
    int i = (int)noteIndex.size() - 1 - v;
    if (!(filter == "All" || filter == String(noteIndex[i].tag))) continue;
    int num = noteIndex[i].num;
    char txtPath[64]; snprintf(txtPath, sizeof(txtPath), "%s/note_%03d.txt", NOTES_DIR, num);
    String transcript = readSmallFile(txtPath, 4000);
    if (transcript.length() == 0)
      transcript = noteIndex[i].hasText ? "(empty transcript)" : "Not transcribed yet.";
    chunk += "#";
    if (num < 100) chunk += "0";
    if (num < 10)  chunk += "0";
    chunk += String(num) + " · " + String(noteIndex[i].tag) + "\n";
    String createdUtc = noteCreatedUtc(num);
    if (createdUtc.length() > 0) chunk += createdUtc + "\n";
    chunk += "\n" + transcript + "\n\n------------------------------\n\n";
    if (chunk.length() > 2048) { transferServer.sendContent(chunk); chunk = ""; }
  }

  transferServer.sendContent(chunk);
  transferServer.sendContent("");   // terminate chunked response
}

void sendFileByNum(const char* ext, const char* mime, bool attachment) {
  if (!transferServer.hasArg("num")) { transferServer.send(400, "text/plain", "Missing num"); return; }
  int num = transferServer.arg("num").toInt();
  if (num <= 0) { transferServer.send(400, "text/plain", "Invalid num"); return; }
  char path[64]; snprintf(path, sizeof(path), "%s/note_%03d.%s", NOTES_DIR, num, ext);
  File f = SD_MMC.open(path);
  if (!f) { transferServer.send(404, "text/plain", "File not found"); return; }
  if (attachment) {
    String filename = String("note_") + String(num) + "." + String(ext);
    transferServer.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  }
  transferServer.streamFile(f, mime);
  f.close();
}

void handleTagAdd() {
  if (!transferServer.hasArg("name")) {
    transferServer.sendHeader("Location", "/tags?msg=missing");
    transferServer.send(303); return;
  }
  String name = urlDecodeSimple(transferServer.arg("name"));
  bool ok = addCustomTag(name.c_str());
  transferServer.sendHeader("Location", ok ? "/tags?msg=added" : "/tags?msg=exists");
  transferServer.send(303);
}

void handleTagDelete() {
  if (!transferServer.hasArg("name")) {
    transferServer.sendHeader("Location", "/tags?msg=missing");
    transferServer.send(303); return;
  }
  String name = urlDecodeSimple(transferServer.arg("name"));
  bool hadNotes = tagHasNotes(name.c_str());
  bool ok = deleteTag(name.c_str());
  if (ok && hadNotes) transferServer.sendHeader("Location", "/tags?msg=moved");
  else                transferServer.sendHeader("Location", ok ? "/tags?msg=deleted" : "/tags?msg=protected");
  transferServer.send(303);
}

void handleTagsPage() {
  loadTags();
  loadIndex();
  activeFilter = -1;

  String html = "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Forrest Tags</title>"
                "<style>"
                "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;margin:0;padding:24px;background:#f3f0e9;color:#111}"
                ".wrap{max-width:720px;margin:0 auto}"
                "h1{font-size:42px;line-height:.9;letter-spacing:-.05em;margin:0 0 22px;font-weight:800}"
                ".card{background:#fffaf1;border:1.5px solid #111;border-radius:24px;padding:18px;margin:14px 0;box-shadow:4px 4px 0 #111}"
                ".row{display:flex;justify-content:space-between;align-items:center;gap:12px;border-top:1px solid #ddd;padding:12px 0}"
                ".row:first-child{border-top:0}"
                ".tag{font-size:20px;font-weight:700}"
                ".meta{font-size:13px;color:#666;margin-top:4px}"
                "input{font:inherit;padding:12px;border:1.5px solid #111;border-radius:999px;background:#fff;width:100%;box-sizing:border-box}"
                "button,.btn{font:inherit;border:1.5px solid #111;border-radius:999px;padding:10px 14px;background:#111;color:#fff;text-decoration:none;white-space:nowrap}"
                ".danger{background:#fffaf1;color:#111}"
                ".msg{border:1.5px solid #111;border-radius:18px;padding:12px 14px;background:#fff;margin:12px 0}"
                ".hint{font-size:13px;color:#666;line-height:1.4}"
                "form.add{display:flex;gap:10px}"
                "</style></head><body><div class='wrap'>";

  html += "<h1>forrest<br>tags</h1>";
  html += "<a class='btn' href='/'>Back to notes</a>";

  if (transferServer.hasArg("msg")) {
    String msg = transferServer.arg("msg");
    html += "<div class='msg'>";
    if (msg == "added") html += "Tag added.";
    else if (msg == "exists")    html += "Tag already exists or cannot be added.";
    else if (msg == "deleted")   html += "Tag deleted.";
    else if (msg == "moved")     html += "Tag deleted. Existing notes were moved to Untagged.";
    else if (msg == "protected") html += "This tag cannot be deleted.";
    else html += "Please enter a tag name.";
    html += "</div>";
  }

  html += "<div class='card'><form class='add' action='/tag/add' method='get'>"
          "<input name='name' maxlength='31' placeholder='New tag name'>"
          "<button type='submit'>Add</button></form>"
          "<p class='hint'>Tags appear on the device after recording. Keep them short for the e-paper UI.</p></div>";

  html += "<div class='card'>";
  for (int i = 0; i < tagCount; i++) {
    int cnt = 0;
    for (int n = 0; n < (int)noteIndex.size(); n++)
      if (strcmp(noteIndex[n].tag, tags[i]) == 0) cnt++;
    html += "<div class='row'><div><div class='tag'>" + htmlEscape(String(tags[i])) + "</div>";
    html += "<div class='meta'>" + String(cnt) + (cnt == 1 ? " note" : " notes");
    if (cnt > 0) html += " · deleting moves them to Untagged";
    html += "</div></div>";
    if (strcasecmp(tags[i], "Untagged") != 0) {
      html += "<a class='btn danger' href='/tag/delete?name=" + htmlEscape(String(tags[i])) + "' "
              "onclick=\"return confirm('Delete this tag? Notes will not be deleted. Existing notes will move to Untagged.');\">Delete</a>";
    }
    html += "</div>";
  }
  html += "</div></div></body></html>";
  transferServer.send(200, "text/html", html);
}

void handleNoteDelete() {
  if (!transferServer.hasArg("num")) { transferServer.send(400, "text/plain", "Missing num"); return; }
  int num = transferServer.arg("num").toInt();
  if (num <= 0) { transferServer.send(400, "text/plain", "Invalid num"); return; }
  deleteNote(num);
  transferServer.sendHeader("Location", "/");
  transferServer.send(303);
}

void handleProvisionPage() {
  Serial.println("[HTTP] GET /provision");
  String html = "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Forrest Setup</title>" + portalCss() + "</head><body><div class='wrap'>";
  html += "<div class='top'><div><h1>forrest<br>setup</h1>"
          "<div class='sub'>device provisioning</div></div></div>";
  html += "<div class='card'>";
  html += "<p class='hint'>Wi-Fi: " + String(cfg::hasWifi() ? "configured" : "not set") +
          " &middot; OpenAI key: " + String(cfg::hasOpenAiKey() ? "configured" : "not set") +
          " &middot; GitHub: " + String(cfg::hasGithub() ? "on" : (cfg::githubRepo().length() ? "set, off" : "not set")) + "</p>";
  html += "<form action='/provision/save' method='post'>";
  html += "<p><input name='ssid' placeholder='Wi-Fi network (SSID)'></p>";
  html += "<p><input name='pass' type='password' placeholder='Wi-Fi password'></p>";
  html += "<p><input name='openai' type='password' placeholder='OpenAI API key (sk-...)'></p>";
  html += "<hr><p class='hint'><b>Obsidian / GitHub vault</b></p>";
  html += "<p><input name='gh_repo' placeholder='GitHub repo (owner/name)' value='" + htmlEscape(cfg::githubRepo()) + "'></p>";
  html += "<p><input name='gh_branch' placeholder='Branch (default main)' value='" + htmlEscape(cfg::githubBranch()) + "'></p>";
  html += "<p><input name='gh_dir' placeholder='Vault folder (default VoiceNotes)' value='" + htmlEscape(cfg::githubDir()) + "'></p>";
  html += "<p><input name='gh_token' type='password' placeholder='GitHub token (github_pat_...)'></p>";
  html += "<p><label><input type='checkbox' name='gh_on' value='1'" + String(cfg::githubEnabled() ? " checked" : "") + "> Enable GitHub sync</label></p>";
  html += "<p><label><input type='checkbox' name='gh_ai' value='1'" + String(cfg::githubAiEnrich() ? " checked" : "") + "> AI titles + topic links</label></p>";
  html += "<p class='hint'>Leave a text field blank to keep its current value.</p>";
  html += "<button type='submit'>Save</button></form></div>";
  html += "<a class='btn' href='/'>Back to notes</a>";
  html += "</div></body></html>";
  transferServer.send(200, "text/html", html);
}

void handleProvisionSave() {
  String ssid = transferServer.hasArg("ssid")   ? transferServer.arg("ssid")   : "";
  String pass = transferServer.hasArg("pass")   ? transferServer.arg("pass")   : "";
  String key  = transferServer.hasArg("openai") ? transferServer.arg("openai") : "";
  ssid.trim(); key.trim();
  bool changed = false;
  if (ssid.length() > 0) { cfg::setWifi(ssid, pass); changed = true; }
  if (key.length()  > 0) { cfg::setOpenAiKey(key);   changed = true; }

  // GitHub vault fields
  if (transferServer.hasArg("gh_repo")) {
    String r = transferServer.arg("gh_repo"); r.trim();
    if (r.length() > 0) { cfg::setGithubRepo(r); changed = true; }
  }
  if (transferServer.hasArg("gh_branch")) {
    String b = transferServer.arg("gh_branch"); b.trim();
    if (b.length() > 0) { cfg::setGithubBranch(b); changed = true; }
  }
  if (transferServer.hasArg("gh_dir")) {
    String d = transferServer.arg("gh_dir"); d.trim();
    if (d.length() > 0) { cfg::setGithubDir(d); changed = true; }
  }
  if (transferServer.hasArg("gh_token")) {
    String t = transferServer.arg("gh_token"); t.trim();
    if (t.length() > 0) { cfg::setGithubToken(t); changed = true; }
  }
  // checkboxes only POST when checked → presence = on, absence = off
  cfg::setGithubEnabled(transferServer.hasArg("gh_on"));
  cfg::setGithubAiEnrich(transferServer.hasArg("gh_ai"));
  changed = true;

  String html = "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Forrest Setup</title>" + portalCss() + "</head><body><div class='wrap'>";
  html += "<div class='card'><h1>" + String(changed ? "saved" : "no change") + "</h1>";
  html += "<p class='hint'>" + String(changed
            ? "Settings stored to the device. Re-open Transfer or Sync to use them."
            : "Nothing was submitted.") + "</p>";
  html += "<a class='btn' href='/provision'>Back to setup</a></div></div></body></html>";
  transferServer.send(200, "text/html", html);
}

void handleOtaPage() {
  String html = "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Forrest OTA</title>" + portalCss() + "</head><body><div class='wrap'>";
  html += "<div class='top'><div><h1>forrest<br>update</h1>"
          "<div class='sub'>firmware " FW_VERSION "</div></div></div>";
  html += "<div class='card'>";
  html += "<p class='hint'>Paste an HTTPS URL to a compiled firmware .bin. "
          "The device verifies the server certificate, flashes the inactive OTA slot, "
          "and reboots into it (rolling back automatically if it fails to boot).</p>";
  html += "<form action='/ota/run' method='post'>"
          "<p><input name='url' placeholder='https://host/forrest-note.bin'></p>"
          "<button type='submit'>Update firmware</button></form></div>";
  html += "<a class='btn' href='/'>Back to notes</a>";
  html += "</div></body></html>";
  transferServer.send(200, "text/html", html);
}

void handleOtaRun() {
  if (!transferServer.hasArg("url") || transferServer.arg("url").length() == 0) {
    transferServer.send(400, "text/plain", "Missing url");
    return;
  }
  String url = transferServer.arg("url");
  transferServer.send(200, "text/html",
    "<!doctype html><meta charset='utf-8'><h1>Updating&hellip;</h1>"
    "<p>Flashing firmware. The device reboots automatically if the update succeeds. "
    "If it fails it stays on the current version &mdash; reopen Transfer and retry.</p>");
  delay(250);

  WiFiClientSecure client;
  client.setCACertBundle(x509_crt_bundle_start,
                         (size_t)(x509_crt_bundle_end - x509_crt_bundle_start));
  httpUpdate.rebootOnUpdate(true);
  t_httpUpdate_return r = httpUpdate.update(client, url, FW_VERSION);
  if (r == HTTP_UPDATE_FAILED)
    Serial.printf("[OTA] failed (%d): %s\n",
                  httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
  else if (r == HTTP_UPDATE_NO_UPDATES)
    Serial.println("[OTA] no update available");
}

void setupTransferServer() {
  transferServer.on("/", HTTP_GET, handlePortalRoot);
  transferServer.on("/provision", HTTP_GET, handleProvisionPage);
  transferServer.on("/provision/save", HTTP_POST, handleProvisionSave);
  transferServer.on("/ota", HTTP_GET, handleOtaPage);
  transferServer.on("/ota/run", HTTP_POST, handleOtaRun);
  transferServer.on("/tags", HTTP_GET, handleTagsPage);
  transferServer.on("/tag/add", HTTP_GET, handleTagAdd);
  transferServer.on("/tag/delete", HTTP_GET, handleTagDelete);
  transferServer.on("/note/delete", HTTP_GET, handleNoteDelete);
  transferServer.on("/api/notes", HTTP_GET, handlePortalJson);
  transferServer.on("/export.txt", HTTP_GET, handleExportTxt);
  transferServer.on("/txt",   HTTP_GET, [](){ sendFileByNum("txt", "text/plain", true); });
  transferServer.on("/wav",   HTTP_GET, [](){ sendFileByNum("wav", "audio/wav",  true); });
  transferServer.on("/audio", HTTP_GET, [](){ sendFileByNum("wav", "audio/wav",  false); });
  transferServer.onNotFound([](){
    Serial.printf("[HTTP] miss: %s\n", transferServer.uri().c_str());
    if (captivePortalActive) {
      // Captive portal: bounce any unknown URL (incl. the OS connectivity probe)
      // to the setup page so it opens automatically.
      transferServer.sendHeader("Location", "http://" + transferUrl + "/provision", true);
      transferServer.send(302, "text/plain", "");
    } else {
      transferServer.send(404, "text/plain", "Not found");
    }
  });
}

void stopTransferMode() {
  if (transferServerActive) {
    transferServer.stop();
    transferServerActive = false;
  }
  if (captivePortalActive) {
    dnsServer.stop();
    captivePortalActive = false;
  }
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  transferUrl = "";
}
