#pragma once
// ============================================================================
//  Pabbly AI Desk Bot — device configuration.
//  Wi-Fi, server URL and device token are entered ON THE DEVICE via the setup
//  Wi-Fi portal (no reflashing to change networks). Values below are DEFAULTS
//  shown in that portal, plus hardware pins and display/behaviour tuning.
// ============================================================================

// ---- Setup portal ----
#define SETUP_AP_NAME     "Pabbly-Setup"        // Wi-Fi you join to configure the device
#define DEFAULT_SERVER    "https://pabbly-ai-desk-bot.YOUR-SUBDOMAIN.workers.dev"  // no trailing slash
#define DEFAULT_TOKEN     "choose-a-long-random-string"                            // must match Worker DEVICE_TOKEN
#define DEFAULT_BOT_NAME  "Pabbly"              // shown on standby until claimed/named on the site

// ---- Pins (match the carrier board; XIAO ESP32-S3 labels shown) ----
#define I2C_SDA        5     // D4  OLED
#define I2C_SCL        6     // D5  OLED
#define MIC_SCK        1     // D0  INMP441
#define MIC_WS         2     // D1  INMP441
#define MIC_SD         3     // D2  INMP441
#define SPK_BCLK       7     // D8  MAX98357A
#define SPK_LRC        8     // D9  MAX98357A
#define SPK_DIN        9     // D10 MAX98357A
#define TOUCH_PIN      4     // D3  capacitive pad
#define TOUCH_THRESHOLD 40000 // TUNE via Serial

// ---- Audio ----
#define REC_SAMPLE_RATE   16000
#define PLAY_SAMPLE_RATE  24000
#define MAX_REC_SECONDS   5
#define MIC_GAIN_SHIFT    14      // lower = louder

// ---- Clock (standby face) ----
#define GMT_OFFSET_SEC    19800   // +5:30 India. e.g. 0=UTC, 3600=+1h, -18000=US Eastern
#define DST_OFFSET_SEC    0
#define NTP_SERVER        "pool.ntp.org"

// ---- Behaviour ----
#define HEARTBEAT_MS      60000   // report "alive" to the server this often
#define CONFIG_POLL_MS    30000   // check the server for name/Wi-Fi-reset this often
// Tip: hold the touch pad while powering on to erase saved Wi-Fi and re-open the portal.
