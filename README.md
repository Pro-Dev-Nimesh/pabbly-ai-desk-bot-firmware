# Pabbly AI Desk Bot — Firmware 🔌

The ESP32-S3 device firmware for the **Pabbly AI Desk Bot** — plus a browser flasher
so anyone can install it over USB without an IDE.

> This is the **device** half. The AI backend it talks to lives in the companion repo
> **pabbly-ai-desk-bot-server** (the Cloudflare Worker that does the auto-reply).

```
firmware/       ESP32-S3 sketch (Wi-Fi setup portal + device claim + voice pipeline)
web-flasher/    Browser flasher (ESP Web Tools) → GitHub Pages
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

## Before you flash
Open `firmware/config.h` and set:
- `DEFAULT_SERVER` → your deployed Worker URL from **pabbly-ai-desk-bot-server** (e.g. `https://voice-buddy.<you>.workers.dev`, no trailing slash)
- `DEFAULT_TOKEN`  → the same `DEVICE_TOKEN` you set on that Worker

## Flash it — two ways
**A) Browser flasher (easiest)** — once GitHub Pages is enabled (Settings → Pages → Source = GitHub Actions),
open `https://<you>.github.io/pabbly-ai-desk-bot-firmware/`, plug in via USB-C, click **Connect & Install**.
*(GitHub Pages is free only on public repos.)*

**B) Arduino IDE** — install the **ESP32** boards package + **U8g2** and **WiFiManager** libraries,
open `firmware/voicebot.ino`, set **Tools → PSRAM → OPI PSRAM**, and Upload.
*(PlatformIO: open `firmware/` and Upload.)*

## First-time device setup (like Xiaozhi)
1. Power on → the OLED says **"Join 'VoiceBuddy-Setup'"**.
2. Join that Wi-Fi on your phone → pick your home Wi-Fi → confirm Server URL + Device Token → save.
3. The device shows a **6-digit code** → enter it in the web console (from the server repo) to claim it.
4. Touch the pad and talk. Re-open setup later by holding the pad while powering on.

## Calibration
Serial @ 115200. Tune `TOUCH_THRESHOLD` (watch the idle value) and `MIC_GAIN_SHIFT` (lower = louder) in `config.h`.

## License
MIT — see [LICENSE](LICENSE).
