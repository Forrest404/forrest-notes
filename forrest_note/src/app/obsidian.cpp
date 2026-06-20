#include "Arduino.h"
#include "obsidian.h"
#include "../../config.h"
#include "../../globals.h"
#include "../../types.h"
#include "config_store.h"
#include "notes.h"
#include "ui.h"
#include "WiFi.h"
#include "WiFiClientSecure.h"
#include "SD_MMC.h"
#include <ArduinoJson.h>
#include <vector>
#include "mbedtls/base64.h"

// Same Mozilla CA root bundle used by network.cpp (resolves to the same symbols).
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_bundle_end[]   asm("_binary_x509_crt_bundle_end");

enum GhResult { GH_OK, GH_AUTH, GH_RATELIMIT, GH_NET, GH_OTHER };

// Decode an HTTP/1.1 chunked-transfer body: repeated "<hexsize>\r\n<data>\r\n"
// until a zero-size chunk. OpenAI's chat endpoint replies chunked (GitHub doesn't),
// and without this the raw body keeps its chunk markers and fails to JSON-parse.
static String dechunkBody(const String& in) {
  String out; int i = 0, n = in.length();
  while (i < n) {
    int eol = in.indexOf('\n', i);
    if (eol < 0) break;
    String sizeLine = in.substring(i, eol);
    int semi = sizeLine.indexOf(';');                 // ignore chunk extensions
    if (semi >= 0) sizeLine = sizeLine.substring(0, semi);
    sizeLine.trim();
    long sz = strtol(sizeLine.c_str(), nullptr, 16);
    i = eol + 1;
    if (sz <= 0) break;                               // terminating 0-chunk
    if (i + sz > n) sz = n - i;                        // guard against truncation
    out += in.substring(i, i + sz);
    i += sz;
    while (i < n && (in[i] == '\r' || in[i] == '\n')) i++;  // skip CRLF after data
  }
  return out;
}

// ── Generic HTTPS request (reuses the transcribeOnce pattern) ───────────────
static bool httpsSend(const char* host, const String& method, const String& path,
                      const std::vector<String>& headers, const String& body,
                      int& outStatus, String& outBody) {
  WiFiClientSecure client;
  client.setCACertBundle(x509_crt_bundle_start,
                         (size_t)(x509_crt_bundle_end - x509_crt_bundle_start));
  client.setHandshakeTimeout(15);
  if (!client.connect(host, 443, 15000)) return false;

  String req = method + " " + path + " HTTP/1.1\r\n";
  req += "Host: " + String(host) + "\r\n";
  for (size_t i = 0; i < headers.size(); i++) req += headers[i] + "\r\n";
  req += "Content-Length: " + String(body.length()) + "\r\n";
  req += "Connection: close\r\n\r\n";
  client.print(req);
  if (body.length()) client.print(body);

  uint32_t deadline = millis() + 30000;
  while (!client.available() && millis() < deadline) delay(10);

  outStatus = 0; outBody = "";
  bool inBody = false, statusParsed = false, chunked = false;
  while (client.available() || (client.connected() && millis() < deadline)) {
    if (!client.available()) { delay(5); continue; }
    if (!inBody) {
      String line = client.readStringUntil('\n');
      if (!statusParsed && line.startsWith("HTTP/")) {
        int sp = line.indexOf(' ');
        if (sp > 0) outStatus = line.substring(sp + 1, sp + 4).toInt();
        statusParsed = true;
      }
      String low = line; low.toLowerCase();
      if (low.startsWith("transfer-encoding:") && low.indexOf("chunked") >= 0) chunked = true;
      if (line == "\r" || line == "") inBody = true;
    } else {
      while (client.available() && outBody.length() < 131072) outBody += (char)client.read();
    }
  }
  client.stop();
  if (chunked) outBody = dechunkBody(outBody);        // OpenAI chat replies chunked
  return statusParsed;
}

// ── Small string helpers ────────────────────────────────────────────────────
static String yamlEsc(const String& s) {
  String o = s; o.replace("\\", "\\\\"); o.replace("\"", "\\\"");
  o.replace("\r", " "); o.replace("\n", " "); return o;
}

static String linkSafe(const String& s) {          // strip Obsidian-illegal link chars
  String o; for (size_t i = 0; i < s.length(); i++) {
    char c = s[i]; if (strchr("[]#|^/\\:", c)) continue; o += c;
  } o.trim(); return o;
}

static String tagSlug(const String& s) {            // safe file/link slug
  String o; for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    o += (isalnum((unsigned char)c) || c == ' ' || c == '_' || c == '-') ? c : '-';
  }
  while (o.indexOf("--") >= 0) o.replace("--", "-");
  o.trim();
  if (!o.length()) o = "Untagged";
  if (o.length() > 40) o = o.substring(0, 40);
  return o;
}

static String firstWords(const String& s, int maxWords) {
  String t = s; t.replace("\n", " "); t.replace("\r", " "); t.replace("\t", " "); t.trim();
  int words = 0, i = 0;
  for (; i < (int)t.length(); i++) if (t[i] == ' ' && ++words >= maxWords) break;
  String out = t.substring(0, i);
  if (out.length() > 60) out = out.substring(0, 60);
  return out;
}

static String urlEncodePath(const String& p) {
  String o; for (size_t i = 0; i < p.length(); i++) {
    char c = p[i];
    if (isalnum((unsigned char)c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') o += c;
    else { char b[4]; snprintf(b, sizeof(b), "%%%02X", (unsigned char)c); o += b; }
  } return o;
}

static String base64Encode(const String& in) {
  size_t olen = 0;
  mbedtls_base64_encode(NULL, 0, &olen, (const unsigned char*)in.c_str(), in.length());
  std::vector<unsigned char> out(olen + 1, 0);
  if (mbedtls_base64_encode(out.data(), out.size(), &olen,
                            (const unsigned char*)in.c_str(), in.length()) != 0) return "";
  return String((char*)out.data());
}

// ── AI enrichment: title + summary + cleaned body + topics ──────────────────
static bool enrichNote(const String& transcript, String& title, String& summary,
                       String& cleaned, std::vector<String>& topics) {
  title = ""; summary = ""; cleaned = ""; topics.clear();
  String key = cfg::openaiKey();
  if (key.length() == 0 || WiFi.status() != WL_CONNECTED) return false;

  String input = transcript;
  if (input.length() > 6000) input = input.substring(0, 6000);

  DynamicJsonDocument reqDoc(9000 + input.length() * 2);
  reqDoc["model"] = "gpt-4o-mini";
  reqDoc["temperature"] = 0;
  reqDoc.createNestedObject("response_format")["type"] = "json_object";
  JsonArray msgs = reqDoc.createNestedArray("messages");
  JsonObject sys = msgs.createNestedObject();
  sys["role"] = "system";
  sys["content"] =
    "You clean up and label a raw voice-note transcript. Reply with ONLY a JSON object, "
    "no prose, no code fences. Schema: {"
    "\"title\": string (max 8 words), "
    "\"summary\": string (1 sentence overview), "
    "\"cleaned\": string (the note rewritten as clear, coherent, succinct markdown in the "
    "speaker's own voice — first person if the original is. Remove filler words, false "
    "starts, stutters and repetition; fix grammar and punctuation; keep ALL substantive "
    "details, names, numbers, dates and intent; do NOT add anything that was not said. Use "
    "short paragraphs, and markdown bullet points only when the note is naturally a list. "
    "No headings.), "
    "\"topics\": array of 0-6 strings (proper-noun topics or people mentioned, Title Case, "
    "no # or brackets)}. If the transcript is empty or unintelligible return "
    "{\"title\":\"\",\"summary\":\"\",\"cleaned\":\"\",\"topics\":[]}.";
  JsonObject usr = msgs.createNestedObject();
  usr["role"] = "user"; usr["content"] = input;
  String body; serializeJson(reqDoc, body);

  std::vector<String> headers = {
    "Authorization: Bearer " + key,
    "Content-Type: application/json"
  };
  int status; String resp;
  if (!httpsSend("api.openai.com", "POST", "/v1/chat/completions", headers, body, status, resp))
    return false;
  if (status != 200) { Serial.printf("[enrich] http %d\n", status); return false; }

  DynamicJsonDocument outer(resp.length() + 1024);
  if (deserializeJson(outer, resp)) return false;
  String content = outer["choices"][0]["message"]["content"] | "";
  if (content.length() == 0) return false;

  DynamicJsonDocument inner(content.length() + 1024);
  if (deserializeJson(inner, content)) return false;
  title   = String((const char*)(inner["title"]   | ""));
  summary = String((const char*)(inner["summary"] | ""));
  cleaned = String((const char*)(inner["cleaned"] | ""));
  for (JsonVariant v : inner["topics"].as<JsonArray>()) {
    String t = v.as<String>(); t.trim();
    if (t.length()) topics.push_back(t);
  }
  return true;
}

// ── Markdown ────────────────────────────────────────────────────────────────
static String buildNoteMarkdown(int num, const String& transcript, const String& cleaned,
                                const String& title, const String& summary,
                                const std::vector<String>& topics,
                                const String& userTag, const String& createdUtc) {
  String md = "---\n";
  md += "title: \"" + yamlEsc(title) + "\"\n";
  if (createdUtc.length()) md += "date: " + createdUtc + "\n";
  md += "id: " + String(num) + "\n";
  md += "source: forrest-note\n";

  // frontmatter tags = user tag + topics (Obsidian tags can't contain spaces)
  String list = "";
  std::vector<String> all; all.push_back(userTag);
  for (size_t i = 0; i < topics.size(); i++) all.push_back(topics[i]);
  for (size_t i = 0; i < all.size(); i++) {
    String st = all[i]; st.trim(); st.replace(" ", "-"); st = linkSafe(st);
    if (!st.length()) continue;
    if (list.length()) list += ", ";
    list += "\"" + yamlEsc(st) + "\"";
  }
  md += "tags: [" + list + "]\n---\n\n";

  if (summary.length()) md += "> [!summary] " + summary + "\n\n";

  // Body: AI-cleaned coherent rewrite when available, else the raw transcript.
  String bodyText = cleaned.length() ? cleaned : transcript;
  if (bodyText.startsWith("---")) bodyText = "\n" + bodyText;   // don't look like frontmatter
  md += bodyText;
  if (!bodyText.endsWith("\n")) md += "\n";

  // Preserve the verbatim transcript in a foldable callout when we rewrote the body.
  if (cleaned.length() && transcript.length()) {
    String raw = transcript; raw.trim();
    md += "\n> [!quote]- Original transcript\n> ";
    for (size_t i = 0; i < raw.length(); i++) {
      char c = raw[i];
      if (c == '\r') continue;
      md += (c == '\n') ? String("\n> ") : String(c);
    }
    md += "\n";
  }

  if (!topics.empty()) {
    md += "\n---\nTopics: ";
    bool first = true;
    for (size_t i = 0; i < topics.size(); i++) {
      String lk = linkSafe(topics[i]);
      if (!lk.length()) continue;
      if (!first) md += " · ";
      md += "[[" + lk + "]]";
      first = false;
    }
    md += "\n";
  }
  return md;
}

// ── GitHub Contents API ─────────────────────────────────────────────────────
static int githubGetSha(const String& path, String& sha) {   // returns HTTP status, -1 on net fail
  sha = "";
  String url = "/repos/" + cfg::githubRepo() + "/contents/" + urlEncodePath(path) +
               "?ref=" + cfg::githubBranch();
  std::vector<String> headers = {
    "Authorization: Bearer " + cfg::githubToken(),
    "User-Agent: ForrestNote",
    "Accept: application/vnd.github+json"
  };
  int status; String resp;
  if (!httpsSend("api.github.com", "GET", url, headers, "", status, resp)) return -1;
  if (status == 200) {
    DynamicJsonDocument doc(resp.length() + 512);
    if (!deserializeJson(doc, resp)) sha = String((const char*)(doc["sha"] | ""));
  }
  return status;
}

static GhResult githubPutFile(const String& path, const String& content, const String& msg) {
  if (WiFi.status() != WL_CONNECTED) return GH_NET;

  String sha;
  int gs = githubGetSha(path, sha);
  if (gs == -1)  return GH_NET;
  if (gs == 401) return GH_AUTH;
  if (gs == 403) return GH_RATELIMIT;
  if (gs != 200 && gs != 404) return GH_OTHER;   // 200=update, 404=create

  String b64 = base64Encode(content);
  if (b64.length() == 0) return GH_OTHER;

  DynamicJsonDocument doc(b64.length() + 512);
  doc["message"] = msg;
  doc["content"] = b64;
  doc["branch"]  = cfg::githubBranch();
  if (sha.length()) doc["sha"] = sha;
  String body; serializeJson(doc, body);

  String url = "/repos/" + cfg::githubRepo() + "/contents/" + urlEncodePath(path);
  std::vector<String> headers = {
    "Authorization: Bearer " + cfg::githubToken(),
    "User-Agent: ForrestNote",
    "Accept: application/vnd.github+json",
    "Content-Type: application/json"
  };
  int status; String resp;
  if (!httpsSend("api.github.com", "PUT", url, headers, body, status, resp)) return GH_NET;
  if (status == 200 || status == 201) return GH_OK;
  if (status == 401) return GH_AUTH;
  if (status == 403) return (resp.indexOf("rate limit") >= 0) ? GH_RATELIMIT : GH_AUTH;
  Serial.printf("[gh] PUT %s -> %d\n", path.c_str(), status);
  return GH_OTHER;
}

static GhResult buildAndPushTagMOC(const char* tag) {
  String md = "---\ntitle: \"" + yamlEsc(String(tag)) + "\"\ntype: MOC\n---\n\n# " +
              String(tag) + "\n\n";
  for (int i = 0; i < (int)noteIndex.size(); i++) {
    if (noteIndex[i].hasText && strcmp(noteIndex[i].tag, tag) == 0) {
      char nm[24]; snprintf(nm, sizeof(nm), "note_%03d", noteIndex[i].num);
      md += "- [[" + String(nm) + "]]\n";
    }
  }
  String path = cfg::githubDir() + "/Tags/" + tagSlug(String(tag)) + ".md";
  return githubPutFile(path, md, "Update tag MOC: " + String(tag));
}

// ── Public entry point ──────────────────────────────────────────────────────
void obsidianSyncAll() {
  if (!cfg::hasGithub() || WiFi.status() != WL_CONNECTED) return;

  int pending = 0;
  for (int i = 0; i < (int)noteIndex.size(); i++)
    if (noteIndex[i].hasText && !noteObsidianPushed(noteIndex[i].num)) pending++;
  if (pending == 0) return;

  int done = 0; bool pushedAny = false;
  for (int i = 0; i < (int)noteIndex.size(); i++) {
    if (!noteIndex[i].hasText || noteObsidianPushed(noteIndex[i].num)) continue;
    if (WiFi.status() != WL_CONNECTED) break;

    showObsidianSync(done, pending);
    int num = noteIndex[i].num;
    String userTag = String(noteIndex[i].tag);
    String createdUtc = noteCreatedUtc(num);

    char tp[64]; snprintf(tp, sizeof(tp), "%s/note_%03d.txt", NOTES_DIR, num);
    File tf = SD_MMC.open(tp);
    String transcript = "";
    if (tf) { while (tf.available() && transcript.length() < 131072) transcript += (char)tf.read(); tf.close(); }

    String title, summary, cleaned; std::vector<String> topics;
    bool aiOn = cfg::githubAiEnrich(), haveKey = cfg::hasOpenAiKey();
    if (aiOn && haveKey) {
      bool ok = enrichNote(transcript, title, summary, cleaned, topics);
      Serial.printf("[sync] note %d enrich=%d title='%s' sum=%d clean=%d\n",
                    num, ok, title.c_str(), summary.length(), cleaned.length());
    } else {
      Serial.printf("[sync] note %d enrich SKIPPED (aiOn=%d haveKey=%d)\n", num, aiOn, haveKey);
    }
    if (title.length() == 0) {
      title = firstWords(transcript, 8);
      if (title.length() == 0) title = "Voice note " + String(num);
    }

    String md = buildNoteMarkdown(num, transcript, cleaned, title, summary, topics, userTag, createdUtc);
    char mp[16]; snprintf(mp, sizeof(mp), "note_%03d.md", num);
    String path = cfg::githubDir() + "/" + String(mp);

    GhResult r = GH_NET;
    for (int attempt = 0; attempt < 3 && WiFi.status() == WL_CONNECTED; attempt++) {
      r = githubPutFile(path, md, "Add " + String(mp));
      if (r == GH_OK || r == GH_AUTH) break;
      delay(1500);
    }
    if (r == GH_OK)            { markNoteObsidianPushed(num, true); done++; pushedAny = true; }
    else if (r == GH_AUTH)     { showError("GIT AUTH"); delay(1600); return; }
    else if (r == GH_RATELIMIT){ Serial.println("[gh] rate limited; stopping"); break; }
    // GH_NET / GH_OTHER: leave pending, try the next note
  }

  // Regenerate MOCs for every non-empty tag (cheap, <=20 tags) when we pushed ≥1 note.
  if (pushedAny) {
    for (int t = 0; t < tagCount; t++) {
      if (WiFi.status() != WL_CONNECTED) break;
      bool any = false;
      for (int i = 0; i < (int)noteIndex.size(); i++)
        if (noteIndex[i].hasText && strcmp(noteIndex[i].tag, tags[t]) == 0) { any = true; break; }
      if (any) buildAndPushTagMOC(tags[t]);
    }
  }
  Serial.printf("[gh] synced %d/%d notes\n", done, pending);
}
