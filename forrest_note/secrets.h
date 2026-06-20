#ifndef SECRETS_H
#define SECRETS_H

// ─────────────────────────────────────────────────────────────────────────────
// You normally do NOT need to edit this file.
//
// Forrest Note is provisioned at runtime over its Wi-Fi setup hotspot
// (see the README → "Setup"). Leave the "...." placeholders below and the
// firmware will boot straight into setup mode on first launch.
//
// These values are only used as a ONE-TIME seed: on first boot, any field that
// is NOT the "...." placeholder is copied into the device's encrypted-at-rest
// NVS storage, after which secrets live on the device and can be changed from
// the portal without reflashing.
//
// ⚠️  If you put real credentials here, DO NOT commit this file. Keys pasted
//     here would end up in your git history. Prefer the hotspot portal.
// ─────────────────────────────────────────────────────────────────────────────

#define WIFI_SSID   "...."   // your 2.4 GHz Wi-Fi name   (or leave as "...." )
#define WIFI_PASS   "...."   // your Wi-Fi password       (or leave as "...." )
#define OPENAI_KEY  "...."   // OpenAI API key            (or leave as "...." )

#endif
