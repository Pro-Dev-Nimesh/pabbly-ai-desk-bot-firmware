// ============================================================================
//  Pabbly AI Desk Bot — ESP32-S3 firmware
//
//  Animated face (eyes + mouth), speaking gesture, standby clock. On first boot
//  it opens a "Pabbly-Setup" Wi-Fi portal, then SPEAKS and SHOWS a 6-digit code
//  you enter on the website to claim it. Touch the pad to talk (interim trigger;
//  the "Pabbly" wake word is a later stage). The face scales to the OLED size.
//
//  Board : Seeed XIAO ESP32-S3  (Tools > PSRAM > "OPI PSRAM" ON)
//  Libs  : "U8g2" (oliver) + "WiFiManager" (tzapu)   [Arduino Library Manager]
// ============================================================================

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <time.h>
#include "driver/i2s.h"
#include "mbedtls/base64.h"
#include "config.h"

#define I2S_MIC_PORT I2S_NUM_0
#define I2S_SPK_PORT I2S_NUM_1

U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, I2C_SCL, I2C_SDA);
Preferences prefs;

static uint8_t* recBuf = nullptr;
static size_t   recCap = 0;
String serverBase, deviceToken, deviceId, botName = DEFAULT_BOT_NAME;
int W = 128, H = 64;
uint32_t lastBeat = 0, lastPoll = 0;
bool shouldSave = false;

// =====================================================================  FACE
float eyeOpenNow() {                       // blink cycle
  uint32_t c = millis() % 3500;
  if (c < 150) { float p = c / 150.0f; return 1.0f - (1.0f - fabsf(p - 0.5f) * 2.0f); }
  return 1.0f;
}
void drawFace(float eyeOpen, float mouthOpen) {
  int ex1 = W * 0.30, ex2 = W * 0.70, ey = H * 0.44;
  int r = H / 6; if (r < 6) r = 6;
  int eh = (int)(r * 2 * eyeOpen); if (eh < 2) eh = 2;
  int rr = eh / 2; if (rr > r) rr = r;
  oled.drawRBox(ex1 - r, ey - eh / 2, r * 2, eh, rr);
  oled.drawRBox(ex2 - r, ey - eh / 2, r * 2, eh, rr);
  int mx = W / 2, my = H * 0.78, mw = W * 0.30;
  int mh = (int)(H * 0.14 * mouthOpen);
  if (mh < 3) oled.drawHLine(mx - mw / 2, my, mw);
  else oled.drawRBox(mx - mw / 2, my - mh / 2, mw, mh, mh / 2);
}
void topLabel(const char* left, const char* right) {
  oled.setFont(u8g2_font_5x8_tf);
  if (left) oled.drawStr(2, 7, left);
  if (right) { int w = oled.getStrWidth(right); oled.drawStr(W - w - 2, 7, right); }
}
void faceStandby() {
  oled.clearBuffer();
  char clk[6] = ""; struct tm t;
  if (getLocalTime(&t, 5)) strftime(clk, sizeof(clk), "%H:%M", &t);
  topLabel(botName.c_str(), clk[0] ? clk : nullptr);
  drawFace(eyeOpenNow(), 0.0f);
  oled.sendBuffer();
}
void faceState(const char* label, float mouthOpen) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_5x8_tf);
  int w = oled.getStrWidth(label); oled.drawStr((W - w) / 2, 7, label);
  drawFace(1.0f, mouthOpen);
  oled.sendBuffer();
}
void faceCode(const String& code) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_5x8_tf);
  const char* t = "Add me with code";
  int w = oled.getStrWidth(t); oled.drawStr((W - w) / 2, 8, t);
  oled.setFont(u8g2_font_logisoso20_tn);
  int cw = oled.getStrWidth(code.c_str()); oled.drawStr((W - cw) / 2, H - 4, code.c_str());
  oled.sendBuffer();
}
void faceMsg(const char* a, const String& b) {
  oled.clearBuffer(); oled.setFont(u8g2_font_6x12_tf);
  oled.drawStr(0, 12, a); oled.drawStr(0, 30, b.c_str()); oled.sendBuffer();
}

// =====================================================================  I2S
void initMic() {
  i2s_config_t c = { .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX), .sample_rate = REC_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT, .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S, .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 6, .dma_buf_len = 256, .use_apll = false, .tx_desc_auto_clear = false, .fixed_mclk = 0 };
  i2s_pin_config_t p = { .bck_io_num = MIC_SCK, .ws_io_num = MIC_WS, .data_out_num = I2S_PIN_NO_CHANGE, .data_in_num = MIC_SD };
  i2s_driver_install(I2S_MIC_PORT, &c, 0, NULL); i2s_set_pin(I2S_MIC_PORT, &p);
}
void initSpeaker() {
  i2s_config_t c = { .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX), .sample_rate = PLAY_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT, .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S, .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8, .dma_buf_len = 256, .use_apll = false, .tx_desc_auto_clear = true, .fixed_mclk = 0 };
  i2s_pin_config_t p = { .bck_io_num = SPK_BCLK, .ws_io_num = SPK_LRC, .data_out_num = SPK_DIN, .data_in_num = I2S_PIN_NO_CHANGE };
  i2s_driver_install(I2S_SPK_PORT, &c, 0, NULL); i2s_set_pin(I2S_SPK_PORT, &p);
}

size_t recordWhileTouched() {
  i2s_start(I2S_MIC_PORT);
  size_t total = 0; const int N = 256; int32_t raw[N]; uint32_t start = millis();
  faceState("listening", 0.0f);
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

// ==================================================================  helpers
void writeWavHeader(uint8_t* h, uint32_t pcm, uint32_t rate) {
  uint32_t chunk = 36 + pcm, br = rate * 2, s1 = 16; uint16_t fmt = 1, ch = 1, bps = 16, blk = 2;
  memcpy(h, "RIFF", 4); memcpy(h + 4, &chunk, 4); memcpy(h + 8, "WAVEfmt ", 8);
  memcpy(h + 16, &s1, 4); memcpy(h + 20, &fmt, 2); memcpy(h + 22, &ch, 2); memcpy(h + 24, &rate, 4);
  memcpy(h + 28, &br, 4); memcpy(h + 32, &blk, 2); memcpy(h + 34, &bps, 2); memcpy(h + 36, "data", 4); memcpy(h + 40, &pcm, 4);
}
String b64decode(const String& in) {
  size_t olen = 0, cap = (in.length() * 3) / 4 + 4; uint8_t* out = (uint8_t*)malloc(cap); if (!out) return "";
  mbedtls_base64_decode(out, cap, &olen, (const uint8_t*)in.c_str(), in.length());
  String r; r.reserve(olen); for (size_t i = 0; i < olen; i++) r += (char)out[i]; free(out); return r;
}
String jsonStr(const String& j, const char* key) {
  int k = j.indexOf(String("\"") + key + "\""); if (k < 0) return "";
  int c = j.indexOf(':', k); int q1 = j.indexOf('"', c + 1); if (q1 < 0) return "";
  int q2 = j.indexOf('"', q1 + 1); return j.substring(q1 + 1, q2);
}
bool jsonBoolTrue(const String& j, const char* key) {
  int k = j.indexOf(String("\"") + key + "\""); if (k < 0) return false;
  int c = j.indexOf(':', k); return j.substring(c + 1, c + 6).indexOf("true") >= 0;
}

// Play a PCM response stream to the speaker, animating the mouth from amplitude.
void playResponse(HTTPClient& http, const char* label) {
  int len = http.getSize(); WiFiClient* st = http.getStreamPtr();
  i2s_start(I2S_SPK_PORT);
  uint8_t buf[1024]; int remaining = len; uint32_t lastFace = 0; float mouth = 0;
  while (http.connected() && (remaining > 0 || len < 0)) {
    size_t avail = st->available();
    if (avail) {
      int n = st->readBytes(buf, min(avail, sizeof(buf)));
      long sum = 0; int samples = n / 2; int16_t* s = (int16_t*)buf;
      for (int i = 0; i < samples; i++) sum += abs(s[i]);
      float amp = samples ? (float)(sum / samples) / 6000.0f : 0; if (amp > 1) amp = 1;
      mouth = mouth * 0.5f + amp * 0.5f;                 // smooth
      size_t w = 0; i2s_write(I2S_SPK_PORT, buf, n, &w, portMAX_DELAY);
      if (remaining > 0) remaining -= n;
      if (millis() - lastFace > 80) { faceState(label, mouth); lastFace = millis(); }
    } else delay(2);
  }
  i2s_zero_dma_buffer(I2S_SPK_PORT); i2s_stop(I2S_SPK_PORT);
}

// ==================================================================  network
String postJson(const String& path, const String& body) {
  WiFiClientSecure c; c.setInsecure(); HTTPClient http; http.begin(c, serverBase + path);
  http.addHeader("Content-Type", "application/json"); http.addHeader("X-Device-Token", deviceToken);
  http.addHeader("X-Device-Id", deviceId); http.setTimeout(20000);
  int code = http.POST((uint8_t*)body.c_str(), body.length());
  String r = code == 200 ? http.getString() : ""; http.end(); return r;
}

void speakText(const String& text) {
  WiFiClientSecure c; c.setInsecure(); HTTPClient http; http.begin(c, serverBase + "/api/tts");
  http.addHeader("Content-Type", "application/json"); http.addHeader("X-Device-Token", deviceToken);
  http.addHeader("X-Device-Id", deviceId); http.setTimeout(30000);
  String body = String("{\"text\":\"") + text + "\"}";
  int code = http.POST((uint8_t*)body.c_str(), body.length());
  if (code == 200) playResponse(http, botName.c_str());
  http.end();
}

void talk(size_t pcmBytes) {
  size_t total = 44 + pcmBytes; uint8_t* body = (uint8_t*)ps_malloc(total);
  if (!body) { faceMsg("Error", "Out of memory"); return; }
  writeWavHeader(body, pcmBytes, REC_SAMPLE_RATE); memcpy(body + 44, recBuf, pcmBytes);
  faceState("thinking", 0.0f);
  WiFiClientSecure c; c.setInsecure(); HTTPClient http; http.begin(c, serverBase + "/api/talk");
  http.addHeader("Content-Type", "audio/wav"); http.addHeader("X-Device-Token", deviceToken);
  http.addHeader("X-Device-Id", deviceId);
  const char* want[] = { "X-Reply-Text" }; http.collectHeaders(want, 1); http.setTimeout(30000);
  int code = http.POST(body, total); free(body);
  if (code != 200) { faceMsg("Server error", String("HTTP ") + code); http.end(); delay(1500); return; }
  playResponse(http, botName.c_str());
  http.end();
}

void registerAndWait() {
  uint32_t lastSpoke = 0; String code;
  while (true) {
    String r = postJson("/api/device/register", String("{\"id\":\"") + deviceId + "\"}");
    if (r.length() == 0) { faceMsg("Server error", "Retrying..."); delay(3000); continue; }
    if (jsonBoolTrue(r, "claimed")) { String n = jsonStr(r, "name"); if (n.length()) botName = n; return; }
    code = jsonStr(r, "code");
    faceCode(code);
    if (millis() - lastSpoke > 30000 || lastSpoke == 0) {
      String spoken; for (unsigned i = 0; i < code.length(); i++) { spoken += code[i]; spoken += ' '; }
      speakText(String("Hi! Add me on the website using code ") + spoken);
      lastSpoke = millis(); faceCode(code);
    }
    for (int i = 0; i < 40; i++) { delay(100); }   // ~4s, keep responsive
  }
}

void checkServer() {
  uint32_t now = millis();
  if (now - lastBeat > HEARTBEAT_MS) { lastBeat = now; postJson("/api/device/heartbeat", String("{\"id\":\"") + deviceId + "\"}"); }
  if (now - lastPoll > CONFIG_POLL_MS) {
    lastPoll = now;
    WiFiClientSecure c; c.setInsecure(); HTTPClient http; http.begin(c, serverBase + "/api/device/config?id=" + deviceId);
    http.addHeader("X-Device-Token", deviceToken); http.setTimeout(10000);
    if (http.GET() == 200) {
      String r = http.getString();
      String n = jsonStr(r, "name"); if (n.length()) botName = n;
      if (jsonBoolTrue(r, "wifiReset")) { http.end(); faceMsg("Wi-Fi reset", "Reopening setup..."); WiFiManager wm; wm.resetSettings(); delay(1000); ESP.restart(); }
    }
    http.end();
  }
}

// ================================================================  provision
void saveCb() { shouldSave = true; }
void provision() {
  prefs.begin("pabbly", false);
  serverBase = prefs.getString("server", DEFAULT_SERVER);
  deviceToken = prefs.getString("token", DEFAULT_TOKEN);
  bool force = touchRead(TOUCH_PIN) > TOUCH_THRESHOLD;
  WiFiManager wm;
  WiFiManagerParameter pS("server", "Server URL (https://...workers.dev)", serverBase.c_str(), 128);
  WiFiManagerParameter pT("token", "Device Token", deviceToken.c_str(), 80);
  wm.addParameter(&pS); wm.addParameter(&pT); wm.setSaveConfigCallback(saveCb); wm.setConfigPortalTimeout(180);
  faceMsg("Wi-Fi setup", String("Join '") + SETUP_AP_NAME + "'");
  bool ok = force ? wm.startConfigPortal(SETUP_AP_NAME) : wm.autoConnect(SETUP_AP_NAME);
  if (!ok) { faceMsg("Wi-Fi failed", "Rebooting..."); delay(2000); ESP.restart(); }
  if (shouldSave) { serverBase = pS.getValue(); deviceToken = pT.getValue(); prefs.putString("server", serverBase); prefs.putString("token", deviceToken); }
  prefs.end();
}

// ==================================================================  Arduino
void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);
  oled.begin(); W = oled.getDisplayWidth(); H = oled.getDisplayHeight();
  faceMsg("Pabbly", "Starting...");

  recCap = (size_t)MAX_REC_SECONDS * REC_SAMPLE_RATE * 2;
  recBuf = (uint8_t*)ps_malloc(recCap);
  if (!recBuf) { faceMsg("FATAL", "Enable PSRAM"); while (1) delay(1000); }

  WiFi.mode(WIFI_STA);
  provision();
  deviceId = WiFi.macAddress();
  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER);

  initMic(); initSpeaker(); i2s_stop(I2S_MIC_PORT); i2s_stop(I2S_SPK_PORT);

  registerAndWait();
}

void loop() {
  checkServer();
  if (touchRead(TOUCH_PIN) > TOUCH_THRESHOLD) {
    size_t bytes = recordWhileTouched();
    if (bytes > 2000) talk(bytes);
    else faceState("say again", 0.0f), delay(800);
  } else {
    faceStandby();       // idle: blinking eyes + clock
  }
  delay(40);
}
