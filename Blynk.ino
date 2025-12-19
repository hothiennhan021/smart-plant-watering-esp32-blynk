/*************************************************
 * SMART PLANT WATERING – STABLE (Blynk.begin) – HEARTBEAT FIX
 * - RELAY: ACTIVE HIGH => ON=HIGH, OFF=LOW
 * - Sen đá: SOIL_LOW=20, SOIL_HIGH=40
 * - Manual timeout = 8s
 * - Anti-noise DHT when pump ON
 * - DHT chỉ đọc mỗi 10s (giảm block -> giảm heartbeat timeout)
 * - Tăng heartbeat để chống lag mạng
 *************************************************/

// ===================== BLYNK TEMPLATE =====================
#define BLYNK_TEMPLATE_ID    "TMPL65hLcvy7H"
#define BLYNK_TEMPLATE_NAME  "Hệ thống tưới cây tự động"
#define BLYNK_PRINT Serial

// ✅ Chống Heartbeat timeout khi mạng lag
#define BLYNK_HEARTBEAT 60
#define BLYNK_TIMEOUT_MS 15000

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>

// ===================== USER SECRETS (DO NOT COMMIT REAL VALUES) =====================
// Điền thật khi chạy, KHÔNG push token/WiFi thật lên GitHub
const char* WIFI_SSID  = "YOUR_WIFI_SSID";
const char* WIFI_PASS  = "YOUR_WIFI_PASS";
const char* BLYNK_AUTH = "YOUR_BLYNK_AUTH_TOKEN";
// ================================================================================

// ===================== PINOUT =====================
const int dhtPin          = 32;
const int pumpPin         = 33;
const int soilMoisturePin = 34;
const int waterLevelPin   = 35;

// ===================== DHT =====================
#define DHTTYPE DHT22
DHT dht(dhtPin, DHTTYPE);

// ===================== RELAY (ACTIVE HIGH) =====================
#define RELAY_ON  HIGH
#define RELAY_OFF LOW

// ===================== THRESHOLDS (SEN ĐÁ) =====================
const int SOIL_LOW  = 20;
const int SOIL_HIGH = 40;

// Manual timeout
const unsigned long MANUAL_TIMEOUT = 8000UL; // 8s

// ===== SOIL CALIB (tạm) =====
const int SOIL_RAW_DRY = 2538;  // mốc khô (ngoài không khí)
const int SOIL_RAW_WET = 1500;  // mốc ướt (tạm)

// Water protect
const int WATER_LOW_PCT   = 10;
const int WATER_RAW_EMPTY = 0;
const int WATER_RAW_FULL  = 2500;

// ===== DHT FAIL FILTER =====
const int DHT_FAIL_LIMIT = 5;
int dhtFailCount = 0;

// ===== DHT ANTI-NOISE (PUMP) =====
const unsigned long DHT_SETTLE_OFF_MS = 400;
const bool IGNORE_DHT_FAIL_WHEN_PUMP_ON = true;

// ✅ DHT chỉ đọc mỗi 10s
unsigned long lastDhtReadMs = 0;
const unsigned long DHT_PERIOD_MS = 10000UL;

// ===================== BLYNK V-PINS =====================
#define VPIN_HUMIDITY    V0
#define VPIN_TEMP        V1
#define VPIN_SOIL        V2
#define VPIN_PUMP_BTN    V3
#define VPIN_WATER       V4
#define VPIN_MODE        V5
#define VPIN_LOG         V10

WidgetTerminal terminal(VPIN_LOG);
BlynkTimer timer;

// ===================== GLOBALS =====================
int pumpMode = 0;                 // 0=Auto, 1=Manual
bool isManualRun = false;
unsigned long manualStartTime = 0;

volatile bool pumpState = false;

int soilPct = 0, soilRawAvg = 0;
int waterPct = 0, waterRaw = 0;
float lastH = NAN, lastT = NAN;

bool lastWaterLow = false;
bool lastDhtRealFail = false;

const unsigned long EVENT_COOLDOWN = 60000UL;
unsigned long lastEventWaterMs = 0;
unsigned long lastEventDhtMs   = 0;

const unsigned long PERIOD_LOGIC_MS = 2000UL;
const unsigned long PERIOD_SEND_MS  = 10000UL;

unsigned long bootMs = 0;

// ===================== LOG HELPERS =====================
void blynkLogLine(const String& msg) {
  Serial.println(msg);
  if (Blynk.connected()) {
    terminal.println(msg);
    terminal.flush();
  }
}

void blynkLogEventCooldown(const char* eventName, const String& msg, unsigned long &lastMs) {
  unsigned long now = millis();
  if (!Blynk.connected()) return;
  if (now - lastMs < EVENT_COOLDOWN) return;
  Blynk.logEvent(eventName, msg);
  lastMs = now;
}

// ===================== SUPPORT =====================
inline void applyPumpPin(bool on) {
  digitalWrite(pumpPin, on ? RELAY_ON : RELAY_OFF);
}

void setPump(bool on) {
  pumpState = on;
  applyPumpPin(on);
  if (Blynk.connected()) Blynk.virtualWrite(VPIN_PUMP_BTN, on ? 1 : 0);
}

void forcePumpOff(const String& reason) {
  pumpState = false;
  isManualRun = false;
  applyPumpPin(false);
  if (Blynk.connected()) Blynk.virtualWrite(VPIN_PUMP_BTN, 0);
  blynkLogLine(reason);
}

int readAdcAvg(int pin, int samples = 10, int delayMs = 3) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delay(delayMs);
  }
  return (int)(sum / samples);
}

int soilRawToPct(int raw) {
  int dry = SOIL_RAW_DRY;
  int wet = SOIL_RAW_WET;
  if (dry < wet) { int tmp = dry; dry = wet; wet = tmp; }

  int pct = map(raw, dry, wet, 0, 100);
  return constrain(pct, 0, 100);
}

int getWaterPctFromRaw(int raw) {
  int pct = map(raw, WATER_RAW_EMPTY, WATER_RAW_FULL, 0, 100);
  return constrain(pct, 0, 100);
}

bool readDhtOnce(float &hOut, float &tOut) {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (isnan(h) || isnan(t)) return false;
  hOut = h; tOut = t;
  return true;
}

bool readDhtSafe(float &hOut, float &tOut) {
  if (readDhtOnce(hOut, tOut)) return true;
  delay(250);
  return readDhtOnce(hOut, tOut);
}

bool readDhtAntiNoise(float &hOut, float &tOut) {
  bool wasOn = pumpState;
  if (wasOn) {
    applyPumpPin(false);
    delay(DHT_SETTLE_OFF_MS);
  }

  bool ok = readDhtSafe(hOut, tOut);

  if (wasOn) {
    applyPumpPin(true);
  }
  return ok;
}

// ===================== BLYNK HANDLERS =====================
BLYNK_WRITE(VPIN_MODE) {
  int newMode = param.asInt();
  if (pumpMode != newMode) forcePumpOff("INFO: Mode changed -> Pump OFF");
  pumpMode = newMode;
}

BLYNK_WRITE(VPIN_PUMP_BTN) {
  int val = param.asInt();
  if (pumpMode == 1) { // MANUAL
    if (val == 1) {
      isManualRun = true;
      manualStartTime = millis();
      setPump(true);
    } else {
      isManualRun = false;
      setPump(false);
    }
  } else { // AUTO: block manual button
    if (val == 1 && Blynk.connected()) Blynk.virtualWrite(VPIN_PUMP_BTN, 0);
  }
}

BLYNK_CONNECTED() {
  Blynk.syncVirtual(VPIN_MODE);
  Blynk.syncVirtual(VPIN_PUMP_BTN);
  blynkLogLine("Blynk connected. Sync OK.");
}

// ===================== MAIN LOGIC =====================
void logicTask() {
  // Soil
  soilRawAvg = readAdcAvg(soilMoisturePin, 10, 3);
  soilPct = soilRawToPct(soilRawAvg);

  // Water
  waterRaw = readAdcAvg(waterLevelPin, 10, 3);
  waterPct = getWaterPctFromRaw(waterRaw);

  // ✅ DHT chỉ đọc mỗi 10s
  unsigned long now = millis();
  if (now - lastDhtReadMs >= DHT_PERIOD_MS) {
    lastDhtReadMs = now;

    float h, t;
    bool ok = readDhtAntiNoise(h, t);

    if (ok) {
      dhtFailCount = 0;
      lastH = h;
      lastT = t;
    } else {
      if (!(IGNORE_DHT_FAIL_WHEN_PUMP_ON && pumpState)) dhtFailCount++;
    }
  }

  bool waterLow = (waterPct < WATER_LOW_PCT);
  bool dhtRealFail = (dhtFailCount >= DHT_FAIL_LIMIT);

  if (waterLow && !lastWaterLow) {
    forcePumpOff("ALERT: Water low -> Pump forced OFF");
    blynkLogEventCooldown("water_low",
                          "Cảnh báo: Bình chứa sắp hết nước! Đã ngắt bơm.",
                          lastEventWaterMs);
  }
  lastWaterLow = waterLow;

  if (dhtRealFail && !lastDhtRealFail) {
    blynkLogLine("ALERT: DHT REAL FAIL -> Pump forced OFF");
    blynkLogEventCooldown("sensor_error",
                          "Cảnh báo: DHT lỗi liên tiếp. Kiểm tra dây/pull-up!",
                          lastEventDhtMs);
  }
  lastDhtRealFail = dhtRealFail;

  if (waterLow) { forcePumpOff("PROTECT: WaterLow -> Pump OFF"); return; }
  if (dhtRealFail) { forcePumpOff("PROTECT: DHT REAL FAIL -> Pump OFF"); return; }

  if (pumpMode == 0) { // AUTO
    if (soilPct <= SOIL_LOW) setPump(true);
    else if (soilPct >= SOIL_HIGH) setPump(false);
  } else { // MANUAL
    if (isManualRun && (millis() - manualStartTime > MANUAL_TIMEOUT)) {
      isManualRun = false;
      setPump(false);
      blynkLogLine("INFO: Manual timeout -> Pump OFF");
    }
  }
}

void sendTask() {
  unsigned long up = (millis() - bootMs) / 1000UL;
  unsigned long mins = up / 60UL;

  String line =
    String("UP=") + mins + "m"
    + " | WiFi=" + (WiFi.status() == WL_CONNECTED ? "OK" : "NO")
    + " Blynk=" + (Blynk.connected() ? "OK" : "NO")
    + " | Soil=" + String(soilPct) + "%(Raw:" + String(soilRawAvg) + ")"
    + " | Water=" + String(waterPct) + "%(Raw:" + String(waterRaw) + ")"
    + " | DHT=" + (dhtFailCount == 0 ? "OK" : String("FAILx") + dhtFailCount)
    + " T=" + String(isfinite(lastT) ? lastT : 0, 1)
    + " H=" + String(isfinite(lastH) ? lastH : 0, 1)
    + " | Pump=" + (pumpState ? "ON" : "OFF")
    + " | Mode=" + (pumpMode == 0 ? "AUTO" : "MANUAL");

  blynkLogLine(line);

  if (Blynk.connected()) {
    if (isfinite(lastH) && isfinite(lastT)) {
      Blynk.virtualWrite(VPIN_HUMIDITY, lastH);
      Blynk.virtualWrite(VPIN_TEMP, lastT);
    }
    Blynk.virtualWrite(VPIN_SOIL, soilPct);
    Blynk.virtualWrite(VPIN_WATER, waterPct);
  }
}

// ===================== SETUP & LOOP =====================
void setup() {
  Serial.begin(115200);
  bootMs = millis();

  pinMode(pumpPin, OUTPUT);
  applyPumpPin(false);
  pumpState = false;

  analogReadResolution(12);
  dht.begin();

  // ✅ Auto connect chuẩn (không hard-code secrets trong repo)
  Blynk.begin(BLYNK_AUTH, WIFI_SSID, WIFI_PASS);

  timer.setInterval(PERIOD_LOGIC_MS, logicTask);
  timer.setInterval(PERIOD_SEND_MS,  sendTask);
}

void loop() {
  Blynk.run();
  timer.run();
}
