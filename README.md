# Pabbly AI Desk Bot — Firmware 🔌

The ESP32-S3 device firmware for the **Pabbly AI Desk Bot** — plus a browser flasher
so anyone can install it over USB without an IDE.

> This is the **device** half. The AI backend it talks to lives in the companion repo
> **pabbly-ai-desk-bot-server** (the Cloudflare Worker that does the auto-reply).

```
firmware/voicebot/   ESP32-S3 sketch (voicebot.ino + config.h)
web-flasher/         Browser flasher (ESP Web Tools) → GitHub Pages
.github/workflows/
  build-firmware.yml   builds firmware, attaches .bin to Releases
  deploy-pages.yml     builds firmware + publishes the flasher to GitHub Pages
```

## Hardware
Seeed **XIAO ESP32-S3** · **INMP441** I²S mic · **MAX98357A** amp + speaker ·
0.96″ I²C **OLED** · capacitive **touch** pad · LiPo. Reference carrier PCB: 25 × 48 mm, 2-layer.

| Signal | XIAO pin | Device |
|---|---|---|
| I²C SDA / SCL | D4 / D5 | OLED |
| I²S mic SCK / WS / SD | D0 / D1 / D2 | INMP441 |
| I²S amp BCLK / LRC / DIN | D8 / D9 / D10 | MAX98357A |
| Touch | D3 | copper pad |

## What the device does
- **Animated face** — blinking eyes + a mouth that moves while it speaks (lip-sync from audio level); scales to the OLED size.
- **Standby** — shows the bot's name and a live **clock** (NTP) with idle blinking eyes.
- **Onboarding** — on first boot it **speaks its pairing code aloud** and shows it on screen.
- Touch-to-talk (interim); the spoken **"Pabbly"** wake word is a later stage.

## Before you flash
Open `firmware/voicebot/config.h` and set:
- `DEFAULT_SERVER` → your deployed Worker URL from **pabbly-ai-desk-bot-server** (e.g. `https://pabbly-ai-desk-bot-server.<you>.workers.dev`, no trailing slash)
- `DEFAULT_TOKEN`  → the same `DEVICE_TOKEN` you set on that Worker
- `GMT_OFFSET_SEC` → your timezone offset for the standby clock (default +5:30 India)

## Flash it — two ways
**A) Browser flasher (easiest)** — once GitHub Pages is enabled (Settings → Pages → Source = GitHub Actions),
open `https://<you>.github.io/pabbly-ai-desk-bot-firmware/`, plug in via USB-C, click **Connect & Install**.
*(GitHub Pages is free only on public repos.)*

**B) Arduino IDE** — install the **ESP32** boards package + **U8g2** and **WiFiManager** libraries,
open `firmware/voicebot/voicebot.ino`, select board **XIAO_ESP32S3**, set **Tools → PSRAM → OPI PSRAM**, and Upload.

## First-time device setup (like Xiaozhi)
1. Power on → the OLED says **"Join 'Pabbly-Setup'"**.
2. Join that Wi-Fi on your phone → pick your home Wi-Fi → confirm Server URL + Device Token → save.
3. The device **speaks and shows a 6-digit code** → sign in to the web console (server repo) and enter it to claim the bot.
4. Touch the pad and talk. Re-open Wi-Fi setup later by holding the pad while powering on, or from the console's **Change Wi-Fi** button.

## Calibration
Serial @ 115200. Tune `TOUCH_THRESHOLD` (watch the idle value) and `MIC_GAIN_SHIFT` (lower = louder) in `firmware/voicebot/config.h`.

## License
MIT — see [LICENSE](LICENSE).
