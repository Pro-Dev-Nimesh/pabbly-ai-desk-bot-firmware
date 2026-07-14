// ============================================================================
//  Voice Buddy — ESP32-S3 firmware (with Wi-Fi setup portal + device claiming)
//
//  First boot:  opens a "VoiceBuddy-Setup" Wi-Fi network. Join it on your phone,
//  pick your home Wi-Fi, and enter the Server URL + Device Token. The device then
//  shows a 6-digit CODE on its OLED — enter that in the web console to claim it.
//  After that: touch the pad -> speak -> hear the reply.
//
//  Board : Seeed XIAO ESP32-S3  (Tools > PSRAM > "OPI PSRAM" must be ON)
//  Libs  : "U8g2" (oliver) + "WiFiManager" (tzapu)   [Arduino Library Manager]
// ============================================================================

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <U8g2lib.h>
#include <Wire.h>
#include "driver/i2s.h"
#include "mbedtls/base64.h"
#include "config.h"

#define I2S_MIC_PORT I2S_NUM_0
#define I2S_SPK_PORT I2S_NUM_1

U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, I2C_SCL, I2C_SDA);
Preferences prefs;

static uint8_t* recBuf = nullptr;
static size_t   recCap = 0;
String serverBase, deviceToken, deviceId;
uint32_t lastBeat = 0;
bool shouldSave = false;

// ---------------------------------------------------------------- OLED helper
void showLines(const char* title, const String& body) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tf);
  oled.drawStr(0, 10, title);
  oled.drawHLine(0, 13, 128);
  int y = 26; String w = "", line = "", s = body + " ";
  for (unsigned i = 0; i < s.length() && y <= 62; i++) {
    char ch = s[i];
    if (ch == ' ') {
      if (line.length() + w.length() + 1 > 21) { oled.drawStr(0, y, line.c_str()); y += 12; line = ""; }
      line += (line.length() ? " " : "") + w; w = "";
    } else w += ch;
  }
  if (line.length() && y <= 62) oled.drawStr(0, y, line.c_str());
  oled.sendBuffer();
}
void showBig(const char* title, const String& big) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tf); oled.drawStr(0, 10, title);
  oled.setFont(u8g2_font_logisoso20_tn); oled.drawStr(6, 46, big.c_str());
  oled.sendBuffer();
}

// ------------------------------------------------------------------ I2S setup
void initMic() {
  i2s_config_t cfg = { .mode=(i2s_mode_t)(I2S_MODE_MASTER|I2S_MODE_RX), .sample_rate=REC_SAMPLE_RATE,
    .bits_per_sample=I2S_BITS_PER_SAMPLE_32BIT, .channel_format=I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format=I2S_COMM_FORMAT_STAND_I2S, .intr_alloc_flags=ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count=6, .dma_buf_len=256, .use_apll=false, .tx_desc_auto_clear=false, .fixed_mclk=0 };
  i2s_pin_config_t pins = { .bck_io_num=MIC_SCK, .ws_io_num=MIC_WS, .data_out_num=I2S_PIN_NO_CHANGE, .data_in_num=MIC_SD };
  i2s_driver_install(I2S_MIC_PORT, &cfg, 0, NULL); i2s_set_pin(I2S_MIC_PORT, &pins);
}
void initSpeaker() {
  i2s_config_t cfg = { .mode=(i2s_mode_t)(I2S_MODE_MASTER|I2S_MODE_TX), .sample_rate=PLAY_SAMPLE_RATE,
    .bits_per_sample=I2S_BITS_PER_SAMPLE_16BIT, .channel_format=I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format=I2S_COMM_FORMAT_STAND_I2S, .intr_alloc_flags=ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count=8, .dma_buf_len=256, .use_apll=false, .tx_desc_auto_clear=true, .fixed_mclk=0 };
  i2s_pin_config_t pins = { .bck_io_num=SPK_BCLK, .ws_io_num=SPK_LRC, .data_out_num=SPK_DIN, .data_in_num=I2S_PIN_NO_CHANGE };
  i2s_driver_install(I2S_SPK_PORT, &cfg, 0, NULL); i2s_set_pin(I2S_SPK_PORT, &pins);
}

// --------------------------------------------------------------- record audio
size_t recordWhileTouched() {
  i2s_start(I2S_MIC_PORT);
  size_t total = 0; const int N = 256; int32_t raw[N];
  uint32_t start = millis();
  showLines("Listening...", "Release to send");
  while (touchRead(TOUCH_PIN) > TOUCH_THRESHOLD || millis() - start < 400) {
    size_t got = 0; i2s_read(I2S_MIC_PORT, raw, sizeof(raw), &got, portMAX_DELAY);
    int n = got / sizeof(int32_t);
    for (int i = 0; i < n && total + 2 <= recCap; i++) {
      int16_t s = (int16_t)(raw[i] >> MIC_GAIN_SHIFT);
      recBuf[total++] = s & 0xFF; recBuf[total++] = (s >> 8) & 0xFF;
    }
    if (total + 2 > recCap || millis() - start > (uint32_t)MAX_REC_SECONDS * 1000) break;
  }
  i2s_stop(I2S_MIC_PORT);
  return total;
}

// -------------------------------------------------------------- small helpers
void writeWavHeader(uint8_t* h, uint32_t pcm, uint32_t rate) {
  uint32_t chunk = 36 + pcm, br = rate * 2; uint32_t s1 = 16; uint16_t fmt = 1, ch = 1, bps = 16, blk = 2;
  memcpy(h, "RIFF", 4); memcpy(h+4,&chunk,4); memcpy(h+8,"WAVEfmt ",8);
  memcpy(h+16,&s1,4); memcpy(h+20,&fmt,2); memcpy(h+22,&ch,2); memcpy(h+24,&rate,4);
  memcpy(h+28,&br,4); memcpy(h+32,&blk,2); memcpy(h+34,&bps,2); memcpy(h+36,"data",4); memcpy(h+40,&pcm,4);
}
String b64decode(const String& in) {
  size_t olen = 0, cap = (in.length()*3)/4 + 4; uint8_t* out = (uint8_t*)malloc(cap); if(!out) return "";
  mbedtls_base64_decode(out, cap, &olen, (const uint8_t*)in.c_str(), in.length());
  String r; r.reserve(olen); for (size_t i=0;i<olen;i++) r += (char)out[i]; free(out); return r;
}
String jsonStr(const String& j, const char* key) {
  int k = j.indexOf(String("\"")+key+"\""); if (k<0) return "";
  int c = j.indexOf(':', k); int q1 = j.indexOf('"', c+1); if (q1<0) return "";
  int q2 = j.indexOf('"', q1+1); return j.substring(q1+1, q2);
}
bool jsonTrue(const String& j, const char* key) {
  int k = j.indexOf(String("\"")+key+"\""); if (k<0) return false;
  return j.indexOf("true", k) == j.indexOf(':', k) + 1 || j.substring(k, k+30).indexOf("true") > 0;
}

// -------------------------------------------------------------- server calls
String httpPost(const String& url, const String& contentType, const uint8_t* body, size_t len, String* replyHdr = nullptr) {
  WiFiClientSecure c; c.setInsecure();
  HTTPClient http; http.begin(c, url);
  http.addHeader("Content-Type", contentType);
  http.addHeader("X-Device-Token", deviceToken);
  http.addHeader("X-Device-Id", deviceId);
  if (replyHdr) { const char* w[] = {"X-Reply-Text"}; http.collectHeaders(w, 1); }
  http.setTimeout(30000);
  int code = http.POST((uint8_t*)body, len);
  String resp = (code == 200 && !replyHdr) ? http.getString() : "";
  if (replyHdr && code == 200) *replyHdr = http.header("X-Reply-Text");
  String out = (replyHdr ? String(code) : resp);
  http.end();
  return out;
}
String httpPostJson(const String& path, const String& json) {
  return httpPost(serverBase + path, "application/json", (const uint8_t*)json.c_str(), json.length());
}

// register / claim loop: returns when the device is claimed
void registerAndWait() {
  while (true) {
    String r = httpPostJson("/api/device/register", String("{\"id\":\"") + deviceId + "\"}");
    if (r.length() == 0) { showLines("Server error", "Retrying..."); delay(3000); continue; }
    if (jsonTrue(r, "claimed")) { showLines("Ready", jsonStr(r, "name")); delay(1200); return; }
    String code = jsonStr(r, "code");
    showBig("Claim code:", code);
    // second line hint
    oled.setFont(u8g2_font_6x12_tf); oled.drawStr(0, 62, "Enter it in the console"); oled.sendBuffer();
    delay(4000); // poll again
  }
}

void heartbeat() {
  if (millis() - lastBeat < HEARTBEAT_MS) return;
  lastBeat = millis();
  httpPostJson("/api/device/heartbeat", String("{\"id\":\"") + deviceId + "\"}");
}

// ------------------------------------------------------------ talk round-trip
void sendAndPlay(size_t pcmBytes) {
  size_t total = 44 + pcmBytes;
  uint8_t* body = (uint8_t*)ps_malloc(total);
  if (!body) { showLines("Error", "Out of memory"); return; }
  writeWavHeader(body, pcmBytes, REC_SAMPLE_RATE);
  memcpy(body + 44, recBuf, pcmBytes);

  showLines("Thinking...", "");
  WiFiClientSecure c; c.setInsecure();
  HTTPClient http; http.begin(c, serverBase + "/api/talk");
  http.addHeader("Content-Type", "audio/wav");
  http.addHeader("X-Device-Token", deviceToken);
  http.addHeader("X-Device-Id", deviceId);
  const char* want[] = {"X-Reply-Text"}; http.collectHeaders(want, 1);
  http.setTimeout(30000);
  int code = http.POST(body, total);
  free(body);
  if (code != 200) { showLines("Server error", String("HTTP ") + code); http.end(); return; }

  showLines("Buddy:", b64decode(http.header("X-Reply-Text")));

  int len = http.getSize(); WiFiClient* stream = http.getStreamPtr();
  i2s_start(I2S_SPK_PORT);
  uint8_t buf[1024]; int remaining = len;
  while (http.connected() && (remaining > 0 || len < 0)) {
    size_t avail = stream->available();
    if (avail) {
      int n = stream->readBytes(buf, min(avail, sizeof(buf)));
      size_t w = 0; i2s_write(I2S_SPK_PORT, buf, n, &w, portMAX_DELAY);
      if (remaining > 0) remaining -= n;
    } else delay(2);
  }
  i2s_zero_dma_buffer(I2S_SPK_PORT); i2s_stop(I2S_SPK_PORT); http.end();
}

// ------------------------------------------------------------------- provision
void saveCb() { shouldSave = true; }

void provision() {
  prefs.begin("buddy", false);
  serverBase  = prefs.getString("server", DEFAULT_SERVER);
  deviceToken = prefs.getString("token",  DEFAULT_TOKEN);

  // Hold touch at boot to wipe Wi-Fi and reopen the portal.
  bool forcePortal = touchRead(TOUCH_PIN) > TOUCH_THRESHOLD;

  WiFiManager wm;
  WiFiManagerParameter pServer("server", "Server URL (https://...workers.dev)", serverBase.c_str(), 128);
  WiFiManagerParameter pToken("token", "Device Token", deviceToken.c_str(), 80);
  wm.addParameter(&pServer); wm.addParameter(&pToken);
  wm.setSaveConfigCallback(saveCb);
  wm.setConfigPortalTimeout(180);

  showLines("Wi-Fi setup", String("Join '") + SETUP_AP_NAME + "' to configure");
  bool ok = forcePortal ? wm.startConfigPortal(SETUP_AP_NAME) : wm.autoConnect(SETUP_AP_NAME);
  if (!ok) { showLines("Wi-Fi failed", "Rebooting..."); delay(2000); ESP.restart(); }

  if (shouldSave) {
    serverBase = pServer.getValue(); deviceToken = pToken.getValue();
    prefs.putString("server", serverBase); prefs.putString("token", deviceToken);
  }
  prefs.end();
}

// ------------------------------------------------------------------- Arduino
void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);
  oled.begin();
  showLines("Voice Buddy", "Starting...");

  recCap = (size_t)MAX_REC_SECONDS * REC_SAMPLE_RATE * 2;
  recBuf = (uint8_t*)ps_malloc(recCap);
  if (!recBuf) { showLines("FATAL", "Enable PSRAM"); while (1) delay(1000); }

  WiFi.mode(WIFI_STA);
  provision();                                   // captive portal / auto-connect
  deviceId = WiFi.macAddress();

  initMic(); initSpeaker();
  i2s_stop(I2S_MIC_PORT); i2s_stop(I2S_SPK_PORT);

  registerAndWait();                             // show claim code until claimed
  showLines("Voice Buddy", "Touch pad to talk");
}

void loop() {
  heartbeat();
  if (touchRead(TOUCH_PIN) > TOUCH_THRESHOLD) {
    size_t bytes = recordWhileTouched();
    if (bytes > 2000) sendAndPlay(bytes);
    else showLines("Too short", "Hold, then speak");
    delay(300);
    showLines("Voice Buddy", "Touch pad to talk");
  }
  delay(30);
}
