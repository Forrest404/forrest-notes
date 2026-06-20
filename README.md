# Forrest Note 🎙️→📝

**A pocket voice-note device that records your voice, transcribes it, and uses AI to turn rambling speech into clean, coherent notes — synced straight to a GitHub repo and ready for Obsidian.**

Hold a button, talk, let go. A few seconds later a tidy Markdown note — with an AI-written title, a one-line summary, and a cleaned-up body — appears in your notes vault. The raw transcript is always preserved, too.

---

## ✨ What it does

- **One-button voice capture** on a tiny e-ink device — no screen-tapping, no phone.
- **On-device transcription** via OpenAI Whisper.
- **AI note cleanup (the headline upgrade):** a language model rewrites your messy, filler-filled speech into **coherent, succinct prose**, generates a **title**, a **one-sentence summary**, and **topic links** — automatically, every time you sync.
- **Verbatim safety net:** the original raw transcript is tucked into a foldable callout, so nothing you said is ever lost.
- **Syncs to GitHub** as clean Markdown with YAML frontmatter and tags — drop it into **Obsidian** and your notes organise themselves.
- **No app, no account lock-in, no cloud middleman** — it talks directly to OpenAI and to *your* GitHub repo.

---

## 🙏 Credits

Forrest Note is built on the original **Pala Note** firmware — full credit to its original author for the hardware bring-up, the e-ink/audio/codec drivers, the recording engine, and the device UI. This project stands entirely on that foundation.

> _If you're the original Pala Note author: thank you. Add your preferred attribution link here._
> Original project: **Pala Note** — `<add original repo/author link>`

### What this fork adds (the "Forrest Note" upgrade)

Everything below is new on top of the original Pala Note firmware:

| Upgrade | What changed |
|---|---|
| 🤖 **AI note cleanup** | A `gpt-4o-mini` pass rewrites each transcript into coherent, succinct prose — removing "um", false starts, and repetition while preserving every fact, name, and number. |
| 🧠 **AI metadata** | Auto-generated note **title**, one-line **summary** callout, and **topic backlinks** (`[[wikilinks]]`) plus auto-built tag index ("MOC") pages. |
| 🗂️ **Original transcript preserved** | The verbatim transcript is kept in a foldable `> [!quote]- Original transcript` callout under the clean version. |
| 📊 **Snappier level meter** | The live recording VU meter refresh rate was increased (~2 Hz → ~10 Hz) for responsive feedback. |
| 🐛 **Chunked-HTTP fix** | The HTTPS client now decodes `Transfer-Encoding: chunked` responses — which is what makes the OpenAI chat (enrichment) calls actually work. Without it, enrichment silently fails. |
| 🔐 **Zero secrets in code** | Wi-Fi and API keys are provisioned at runtime over a setup hotspot and stored on-device — nothing sensitive lives in this repo. |

---

## 🧰 Hardware

This firmware targets the **Pala Note ESP32-S3 device**:

- **MCU:** ESP32-S3 **N8R8** module — 8 MB flash + 8 MB **OCTAL (OPI)** PSRAM
- **Display:** 1.54" **200×200 e-ink**
- **Audio:** ES8311 (speaker/DAC) + ES7210 (mic/ADC) codecs
- **Storage:** microSD (SD_MMC, 1-bit)
- **Extras:** PCF85063 RTC, SHTC3 temp/humidity sensor, LiPo battery with power latch
- **Buttons:** Record (GPIO0 / BOOT) and Power (GPIO18)

---

## 📋 What you'll need before you start

1. **The device** (assembled Pala Note hardware) and a **USB-C cable**.
2. A computer (macOS/Linux/Windows).
3. An **OpenAI API key** — from <https://platform.openai.com/api-keys>. (Used for Whisper transcription + note cleanup. Billing must be enabled.)
4. A **GitHub repo** to store your notes (can be private) and a **fine-grained Personal Access Token** with **Contents: Read and write** on that repo — from <https://github.com/settings/tokens>.
5. Your **2.4 GHz Wi-Fi** name + password (the ESP32-S3 Wi-Fi is 2.4 GHz only).

> You do **not** put any of these into the code. You enter them on the device's setup hotspot (see **Setup**, below).

---

## 🚀 Installation

There are two paths: the **easy way** (let Claude Code do it for you) and the **manual way**. Both end the same place: firmware flashed onto your device.

### Option A — The easy way (with Claude Code)

If you have [Claude Code](https://claude.com/claude-code) installed, `cd` into this repo and paste these prompts one at a time. Each one is self-contained.

**1. Install the toolchain & build the firmware:**

```
Set up my environment to build this Forrest Note ESP32-S3 firmware. Install arduino-cli if missing,
install the esp32 board core version 3.2.0, and install the "Adafruit GFX Library" and "ArduinoJson"
libraries. Then compile the sketch in ./forrest_note for an ESP32-S3 N8R8 board using these options:
PSRAM=opi, PartitionScheme=custom, CDCOnBoot=cdc, FlashSize=8M. Report any errors.
```

**2. Flash it to the device:**

```
Flash the Forrest Note firmware in ./forrest_note to my connected ESP32-S3 device. The board only
stays on its USB port if it's in the ROM bootloader, so walk me through this: tell me to HOLD the
record button (BOOT/GPIO0), plug in USB while holding, and keep holding until the write finishes.
Detect the serial port, then run the upload with the board options PSRAM=opi, PartitionScheme=custom,
CDCOnBoot=cdc, FlashSize=8M. Confirm when the hash is verified.
```

**3. (Optional) Set up an Obsidian vault that auto-pulls your notes:**

```
I want my Forrest Note notes (which the device pushes to my GitHub repo OWNER/REPO under the
VoiceNotes/ folder) to show up in Obsidian. Clone that repo into a local folder, open it as an
Obsidian vault, and configure the Obsidian Git community plugin to auto-pull every minute and on
launch so new notes appear automatically. Don't push my Obsidian config back to the repo.
```

> Replace `OWNER/REPO` with your actual repo. After flashing, continue to **Setup** below to provision the device.

### Option B — Manual setup

**1. Install `arduino-cli`** (or use the Arduino IDE — board/lib names are identical).

```bash
# macOS (Homebrew)
brew install arduino-cli
# or: curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
```

**2. Install the ESP32 board core (version 3.2.0) and libraries:**

```bash
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.2.0
arduino-cli lib install "Adafruit GFX Library" "ArduinoJson"
```

**3. Compile** (run from the repo root):

```bash
arduino-cli compile \
  -b "esp32:esp32:esp32s3:PSRAM=opi,PartitionScheme=custom,CDCOnBoot=cdc,FlashSize=8M" \
  ./forrest_note
```

You should see something like `Sketch uses ~1.43 MB (8%) of program storage space.`

**4. Flash it.** ⚠️ **Flashing quirk:** this board powers its USB port down ~1 second after plug-in unless it's parked in the ROM bootloader. So:

1. **Press and hold the record button** (BOOT / GPIO0).
2. **While holding**, plug in the USB-C cable.
3. **Keep holding** through the entire write.

Find the port and upload:

```bash
# find the port (macOS shows /dev/cu.usbmodemXXXX ; Linux /dev/ttyACM0 ; Windows COMx)
arduino-cli board list

arduino-cli compile --upload -p /dev/cu.usbmodemXXXX \
  -b "esp32:esp32:esp32s3:PSRAM=opi,PartitionScheme=custom,CDCOnBoot=cdc,FlashSize=8M" \
  ./forrest_note
```

When you see `Hash of data verified.` and `Hard resetting...`, release the button. Done.

> **Arduino IDE alternative:** open `forrest_note/forrest_note.ino`, select board **ESP32S3 Dev Module**, then set: **PSRAM → OPI PSRAM**, **Partition Scheme → custom** (uses the included `partitions.csv`), **USB CDC On Boot → Enabled**, **Flash Size → 8MB**. Hold BOOT, plug in, click Upload.

---

## 🔧 Setup — provision the device over its hotspot

The firmware ships with **no Wi-Fi or keys baked in**. On first boot (or any time it has no Wi-Fi credentials) it hosts a setup hotspot.

1. **Power on the device.** With no Wi-Fi stored, it broadcasts an **open Wi-Fi network called `ForrestNote-Setup`**.
2. **Connect your phone or laptop** to `ForrestNote-Setup`.
3. **Open a browser** to **`http://192.168.4.1`** (most phones pop up the captive portal automatically). You'll land on the **forrest setup** page.
4. **Fill in the form:**
   | Field | What to enter |
   |---|---|
   | **Wi-Fi network (SSID)** | Your 2.4 GHz Wi-Fi name |
   | **Wi-Fi password** | Your Wi-Fi password |
   | **OpenAI API key (sk-...)** | Your OpenAI key |
   | **GitHub repo (owner/name)** | e.g. `yourname/Notes` |
   | **Branch** | `main` (default) |
   | **Vault folder** | `VoiceNotes` (default) |
   | **GitHub token (github_pat_...)** | Your fine-grained PAT with Contents R/W |
   | ☑ **Enable GitHub sync** | tick this on |
   | ☑ **AI titles + topic links** | tick this on (enables the AI cleanup) |
5. **Tap Save.** The device stores everything in on-device NVS and reboots onto your Wi-Fi.

> Tip: you can re-open this page any time the device is on your network at `http://<device-ip>/provision` (or just reconnect to the setup hotspot). Leave any text field blank to keep its current value.

That's it — the device is now a personal AI note-taker.

---

## 🎛️ Using it

| Action | Control |
|---|---|
| **Record a note** | Hold the **record** button while idle, speak, release |
| **Stop recording** | Release the record button |
| **Scroll / next** | Tap **power** |
| **Select / open** | Tap **record** |
| **Back** | Hold **record** |
| **Delete a note** | Hold **power** (while viewing a note) |

After recording, the device transcribes and (when online) syncs to GitHub automatically. Each note becomes a Markdown file like `VoiceNotes/note_001.md`.

---

## 🤖 How the AI pipeline works

On sync, for each new note the firmware:

1. **Transcribes** the audio with **Whisper** (`whisper-1`).
2. **Enriches** it with **`gpt-4o-mini`**, which returns: a short **title**, a one-sentence **summary**, a **cleaned** coherent rewrite of the body, and up to 6 **topics**.
3. **Writes Markdown** and **pushes** it to your GitHub repo via the GitHub Contents API, plus updates a tag index page per tag.

A generated note looks like:

```markdown
---
title: "Valuing and auctioning three paintings"
date: 2026-06-20T11:18:09Z
id: 19
source: forrest-note
tags: ["Idea"]
---

> [!summary] Plan to value three paintings ($2,000 each) and sell them at auction.

I have three paintings, each worth $2,000. They need to be valued on an online
platform for art from the Middle East, then sold through an auction house...

> [!quote]- Original transcript
> I have three paintings by ... that are worth $2,000 each. They need to be valued...
```

---

## 🗂️ Obsidian integration

Because notes are plain Markdown with YAML frontmatter and `tags`, they work in Obsidian out of the box. To sync them into an Obsidian vault:

1. **Clone** your notes repo locally.
2. **Open the folder as an Obsidian vault.**
3. Install the **Obsidian Git** community plugin and enable **auto-pull on launch** + an **interval pull** (1 min) so new device notes appear automatically.

The `[[topic]]` backlinks and per-tag index pages give you an auto-built map of content. (The Claude Code prompt in *Installation → Option A, step 3* sets all of this up for you.)

---

## ⚙️ Configuration reference

All runtime config lives in the device's NVS (namespace `forrest`) and is set via the portal — never in code:

- `WIFI_SSID` / `WIFI_PASS` — Wi-Fi credentials
- `OPENAI_KEY` — OpenAI API key (Whisper + enrichment)
- GitHub: `repo` (owner/name), `branch` (default `main`), `dir` (default `VoiceNotes`), `token`, `enabled`, `ai-enrich`

`secrets.h` exists only as an optional **one-time seed** and ships with `"...."` placeholders — you normally never touch it.

---

## 🛠️ Troubleshooting

- **Device won't stay on the USB port for flashing** → that's expected; hold the record/BOOT button while plugging in and keep holding through the write.
- **Build error about a duplicate `.cpp`** → delete any `* 2.cpp` / `* copy.cpp` files that cloud-sync may have created under `src/`.
- **Boot loop / PSRAM errors** → make sure you selected **PSRAM = OPI** and **Flash Size = 8MB** (this is an N8R8 module).
- **Notes sync but have no AI summary** → make sure **"AI titles + topic links"** is ticked in the portal and your OpenAI key has billing/chat access. (The chunked-HTTP fix in this fork is required for enrichment to work — make sure you flashed *this* firmware.)
- **Wi-Fi won't connect** → the ESP32-S3 is **2.4 GHz only**; a 5 GHz-only network won't appear.

---

## 🔒 Security & privacy

- **No secrets in this repo.** Keys and Wi-Fi are entered on-device and stored in NVS.
- Your notes go **only** to OpenAI (for transcription/cleanup) and to **your own** GitHub repo. There is no third-party server.
- If you ever paste a real key into `secrets.h`, **do not commit it** (uncomment the `secrets.h` line in `.gitignore` first).

---

## 📄 License

Forrest Note's additions (the AI enrichment pipeline, the chunked-HTTP fix, the level-meter change, and this documentation) are released under the **MIT License** — see [`LICENSE`](LICENSE).

The firmware is built on the original **Pala Note** project; please also honour the original author's license terms for their portions of the code.
