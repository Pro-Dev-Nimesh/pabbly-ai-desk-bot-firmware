#pragma once
// ============================================================================
//  Voice Buddy — device configuration.
//  Wi-Fi, server URL and device token are now entered ON THE DEVICE via the
//  setup Wi-Fi portal (no reflashing to change networks). The values below are
//  just the DEFAULTS shown in that portal + the hardware pins.
// ============================================================================

// ---- Setup portal ----
#define SETUP_AP_NAME     "VoiceBuddy-Setup"   // Wi-Fi you join to configure the device
#define DEFAULT_SERVER    "https://voice-buddy.YOUR-SUBDOMAIN.workers.dev"  // no trailing slash
#define DEFAULT_TOKEN     "choose-a-long-random-string"                     // must match Worker secret

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

// ---- Behaviour ----
#define HEARTBEAT_MS      60000   // how often to report "alive" to the server
// Tip: hold the touch pad while powering on to erase saved Wi-Fi and re-open the portal.
