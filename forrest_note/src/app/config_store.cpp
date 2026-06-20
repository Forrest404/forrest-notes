#include "config_store.h"
#include <Preferences.h>
#include <string.h>
#include "../../secrets.h"

namespace {
  Preferences prefs;
  const char* NS = "forrest";

  // secrets.h ships with "...." placeholders; treat all-dots or empty as unset.
  bool isPlaceholder(const char* s) {
    if (!s || s[0] == '\0') return true;
    return strspn(s, ".") == strlen(s);
  }
}

namespace cfg {

void begin() {
  prefs.begin(NS, false);
  // One-time migration: seed NVS from compiled secrets.h only for real (non
  // placeholder) values that aren't already stored. After this, secrets live in
  // NVS and can be rotated at runtime without reflashing.
  if (!prefs.isKey("ssid") && !isPlaceholder(WIFI_SSID)) {
    prefs.putString("ssid", WIFI_SSID);
    prefs.putString("pass", WIFI_PASS);
  }
  if (!prefs.isKey("oaikey") && !isPlaceholder(OPENAI_KEY)) {
    prefs.putString("oaikey", OPENAI_KEY);
  }

  // One-time correction (cfgv=1): the setup form persists the AI-enrich checkbox
  // state, so an older save with the box unchecked could leave "ghai" stuck false
  // — which silently disables note summaries/cleanup on sync. Force it back on once.
  if (!prefs.isKey("cfgv")) {
    prefs.putBool("ghai", true);
    prefs.putUInt("cfgv", 1);
  }
}

String wifiSsid()  { return prefs.getString("ssid", ""); }
String wifiPass()  { return prefs.getString("pass", ""); }
String openaiKey() { return prefs.getString("oaikey", ""); }

bool hasWifi()      { return wifiSsid().length() > 0; }
bool hasOpenAiKey() { return openaiKey().length() > 0; }

bool setWifi(const String& ssid, const String& pass) {
  if (ssid.length() == 0) return false;
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  return true;
}

bool setOpenAiKey(const String& key) {
  prefs.putString("oaikey", key);
  return true;
}

// ── GitHub / Obsidian vault ─────────────────────────────────────────────────
// NVS keys must be <=15 chars.
String githubToken()  { return prefs.getString("ghtok", ""); }
String githubRepo()   { return prefs.getString("ghrepo", ""); }
String githubBranch() { String b = prefs.getString("ghbranch", ""); return b.length() ? b : "main"; }
String githubDir()    { String d = prefs.getString("ghdir", "");    return d.length() ? d : "VoiceNotes"; }
bool   githubEnabled()   { return prefs.getBool("ghon", false); }
bool   githubAiEnrich()  { return prefs.getBool("ghai", true); }

bool hasGithub() {
  return githubEnabled() && githubToken().length() > 0 && githubRepo().indexOf('/') > 0;
}

bool setGithubToken(const String& token) { prefs.putString("ghtok", token); return true; }

bool setGithubRepo(const String& ownerRepo) {
  String r = ownerRepo; r.trim();
  while (r.endsWith("/")) r.remove(r.length() - 1);
  if (r.indexOf('/') <= 0 || r.indexOf('/') != r.lastIndexOf('/')) return false;  // exactly one '/'
  prefs.putString("ghrepo", r);
  return true;
}

bool setGithubBranch(const String& branch) {
  String b = branch; b.trim();
  prefs.putString("ghbranch", b);
  return true;
}

bool setGithubDir(const String& dir) {
  String d = dir; d.trim();
  while (d.endsWith("/"))   d.remove(d.length() - 1);
  while (d.startsWith("/")) d.remove(0, 1);
  prefs.putString("ghdir", d);
  return true;
}

void setGithubEnabled(bool on)  { prefs.putBool("ghon", on); }
void setGithubAiEnrich(bool on) { prefs.putBool("ghai", on); }

void factoryReset() { prefs.clear(); }

}  // namespace cfg
