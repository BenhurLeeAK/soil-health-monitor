/*
  soil_node_sim.ino - SIMULATED soil node for the Soil Monitor phone app
  Board: Waveshare ESP32-S3-A7670E-4G ("ESP32S3 Dev Module", USB CDC On Boot Enabled, PSRAM Disabled, Upload Speed 115200)
  Libraries: LiquidCrystal I2C (Frank de Brabander). WiFi, WebServer and Wire come with the ESP32 core.

  The soil sensors are not working yet, so this sketch makes up realistic values for:
    NPK (N, P, K), soil moisture, soil temperature, pH, air temperature/humidity, analog moisture, battery.
  Everything else is real: Wi-Fi hotspot "SoilNode-XXXX" (password soilnode123, IP 192.168.4.1),
  GET /api/info and GET /api/readings exactly as in esp-api-contract.md (schema 1),
  the 16x2 LCD, the two page buttons and the buzzer.

  Buttons: UP / DOWN = change LCD page.
           Hold UP + DOWN for 1.5 s = cycle a simulated sensor failure (none -> NPK -> SOIL -> AIR -> none),
           to test the app's "sensor not responding" screens.
  Serial Monitor (115200): type  0 = no fault,  n = NPK fault,  z = soil fault,  a = air fault.
*/
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <HardwareSerial.h>
#include <LiquidCrystal_I2C.h>
#include <math.h>

// ================= 1. SETTINGS =================
#define FW_VERSION "0.1.0-sim"
#define API_SCHEMA 1

const char* AP_PASSWORD = "soilnode123";   // 8 characters or more
const int   AP_CHANNEL  = 1;

// pins (from the bench pin map)
#define LCD_SDA 8
#define LCD_SCL 9
#define BTN_UP 15
#define BTN_DOWN 16
#define BUZZER_PIN 10

// buzzer type. BUZZER_ACTIVE_LOW true = the buzzer sounds when the pin is LOW.
// BUZZER_PASSIVE true = the sketch drives a ~2 kHz tone itself (passive buzzer).
// Current build: passive buzzer, sounds when the pin is driven HIGH.
#define BUZZER_ACTIVE_LOW false
#define BUZZER_PASSIVE true

// Air sensor: 0 = simulated too (nothing to wire), 1 = read the real JXBS-3001-TH-RS
// (RX GPIO 5, TX GPIO 6, address 1, no DE pin: same as the working air-sensor sketch).
#define USE_REAL_AIR 0
const int      PIN_AIR_RX = 5, PIN_AIR_TX = 6, PIN_AIR_DE = -1;
const uint8_t  ADDR_AIR = 1;
const uint32_t AIR_BAUD = 9600;

// Soil sensor warm-up. The real ZTS needs 300 s. The demo uses 60 s so you do not wait 5 minutes.
const uint32_t ZTS_WARMUP_S = 60;

// Simulation: how fast the made-up "day" cycles (seconds per full cycle) and rain/irrigation events
const float SIM_CYCLE_S = 600.0f;
#define SIM_IRRIGATION_EVENTS 1

// ================= 2. DATA =================
struct Val { float v = NAN; uint8_t fails = 0; };        // fails counts consecutive misses
struct Readings {
  Val airT, airRH, soilM, soilT, soilPH, n, p, k, moistA, vbat;
  uint32_t seq = 0;                                      // +1 after each full pass over all groups
} R;

static bool good(const Val& a) { return !isnan(a.v) && a.fails < 3; }
static void put(Val& d, bool ok, float v) {
  if (ok) { d.v = v; d.fails = 0; }
  else if (d.fails < 255 && ++d.fails >= 3) d.v = NAN;    // after 3 misses the value becomes null in the JSON
}

// NOTE: the Arduino IDE auto-generates function prototypes and places them before the enum below,
// so functions that take the fault as a parameter use int, not Fault.
enum Fault { FAULT_NONE = 0, FAULT_NPK, FAULT_SOIL, FAULT_AIR, FAULT_COUNT };
Fault fault = FAULT_NONE;
const char* faultName(int f) {
  switch (f) {
    case FAULT_NPK:  return "NPK";
    case FAULT_SOIL: return "SOIL";
    case FAULT_AIR:  return "AIR";
    default:         return "none";
  }
}

// hidden "true" state of the simulated field
float simMoist = 22.8f;     // soil moisture %
float simPH    = 6.4f;
float simN     = 58.0f, simP = 18.0f, simK = 135.0f;
float irrigBoost = 0.0f;    // extra moisture right after a simulated rain/irrigation event

static float randn() {      // roughly normal, mean 0, standard deviation 1
  float s = 0;
  for (int i = 0; i < 6; i++) s += random(0, 10000) / 10000.0f;
  return (s - 3.0f) / 0.7071f;
}
static float round1(float x) { return roundf(x * 10.0f) / 10.0f; }   // 0.1 resolution like the real sensors

static float dayPhase() { return 2.0f * PI * (millis() / 1000.0f) / SIM_CYCLE_S; }

static float warmFactor() {                              // 1.0 right after power-up, 0.0 when warm-up is over
  float up = millis() / 1000.0f;
  float left = (float)ZTS_WARMUP_S - up;
  if (left <= 0) return 0.0f;
  return left / (float)ZTS_WARMUP_S;
}

// ================= 3. Real air sensor (Modbus RTU), only used when USE_REAL_AIR = 1 =================
HardwareSerial AirSerial(1);

static uint16_t crc16(const uint8_t* d, size_t n) {
  uint16_t c = 0xFFFF;
  for (size_t i = 0; i < n; i++) { c ^= d[i]; for (int b = 0; b < 8; b++) c = (c & 1) ? (c >> 1) ^ 0xA001 : c >> 1; }
  return c;
}

static bool readRegsAir(uint8_t addr, uint16_t reg, uint8_t count, uint16_t* out) {
  uint8_t f[8] = {addr, 0x03, (uint8_t)(reg >> 8), (uint8_t)reg, 0, count, 0, 0};
  uint16_t c = crc16(f, 6); f[6] = c & 0xFF; f[7] = c >> 8;            // CRC low byte first
  const size_t want = 5 + 2 * count;
  for (int attempt = 0; attempt < 2; attempt++) {
    while (AirSerial.available()) AirSerial.read();
    if (PIN_AIR_DE >= 0) digitalWrite(PIN_AIR_DE, HIGH);
    AirSerial.write(f, 8); AirSerial.flush();
    if (PIN_AIR_DE >= 0) digitalWrite(PIN_AIR_DE, LOW);
    uint8_t r[32]; size_t got = 0; uint32_t t0 = millis();
    while (millis() - t0 < 300 && got < want) if (AirSerial.available()) r[got++] = AirSerial.read();
    if (got == want && r[0] == addr && r[1] == 0x03 && r[2] == 2 * count) {
      uint16_t rc = crc16(r, want - 2);
      if ((rc & 0xFF) == r[want - 2] && (rc >> 8) == r[want - 1]) {
        for (int i = 0; i < count; i++) out[i] = (r[3 + 2 * i] << 8) | r[4 + 2 * i];
        delay(200); return true;
      }
    }
    delay(200);
  }
  return false;
}

// ================= 4. "Sensor" groups: simulated (or real air), one group per call =================
static void pollAir() {
#if USE_REAL_AIR
  uint16_t r[2] = {0, 0};
  bool ok = (fault != FAULT_AIR) && readRegsAir(ADDR_AIR, 0x0000, 2, r);
  put(R.airRH, ok, r[0] / 10.0f);                        // register 0 = humidity x10
  put(R.airT,  ok, (int16_t)r[1] / 10.0f);               // register 1 = temperature x10 (signed)
#else
  if (fault == FAULT_AIR) { put(R.airT, false, 0); put(R.airRH, false, 0); return; }
  float ph = dayPhase();
  float t  = 30.0f + 3.0f * sinf(ph) + randn() * 0.12f;           // about 27 to 33 C
  float rh = 62.0f - 7.0f * sinf(ph) + randn() * 0.5f;            // falls when it gets warmer
  put(R.airT,  true, round1(t));
  put(R.airRH, true, round1(constrain(rh, 30.0f, 98.0f)));
#endif
}

static void pollNPK() {                                  // JXBS-3001-NPK-RS: N, P, K in mg/kg (whole numbers)
  if (fault == FAULT_NPK) { put(R.n, false, 0); put(R.p, false, 0); put(R.k, false, 0); return; }
  simN += (58.0f  - simN) * 0.02f + randn() * 0.9f;
  simP += (18.0f  - simP) * 0.02f + randn() * 0.3f;
  simK += (135.0f - simK) * 0.02f + randn() * 1.8f;
  // the real sensor estimates nutrients from conductivity, so it reads a bit higher when the soil is wetter
  float f = 0.85f + 0.15f * ((simMoist + irrigBoost) / 22.8f);
  put(R.n, true, roundf(constrain(simN * f, 0.0f, 1999.0f)));
  put(R.p, true, roundf(constrain(simP * f, 0.0f, 1999.0f)));
  put(R.k, true, roundf(constrain(simK * f, 0.0f, 1999.0f)));
}

static void pollSoil() {                                 // ZTS-3001-TR-THPH-N01: moisture %, temperature C, pH
  if (fault == FAULT_SOIL) { put(R.soilM, false, 0); put(R.soilT, false, 0); put(R.soilPH, false, 0); return; }
  simMoist += (22.8f - simMoist) * 0.01f + randn() * 0.06f;
  simMoist = constrain(simMoist, 12.0f, 40.0f);
#if SIM_IRRIGATION_EVENTS
  if (random(0, 150) == 0) irrigBoost += 5.0f;           // about one event per 10 minutes
  irrigBoost *= 0.97f;                                   // soaks away over a couple of minutes
#endif
  simPH += (6.4f - simPH) * 0.02f + randn() * 0.03f;
  simPH = constrain(simPH, 5.5f, 7.5f);

  float w = warmFactor();                                // values are off while the sensor warms up
  float soilTemp = 27.0f + 1.6f * sinf(dayPhase() - 0.6f) + randn() * 0.04f;
  put(R.soilM,  true, round1(constrain(simMoist + irrigBoost + 6.0f * w, 0.0f, 100.0f)));
  put(R.soilT,  true, round1(soilTemp + 2.5f * w));
  put(R.soilPH, true, round1(constrain(simPH - 0.9f * w, 3.0f, 10.0f)));
}

static void pollAnalog() {                               // analog moisture and battery
  float a = (simMoist + irrigBoost) * 1.3f + 3.0f + randn() * 0.4f;
  put(R.moistA, true, round1(constrain(a, 0.0f, 100.0f)));
  float vb = 3.95f - (millis() / 1000.0f) * 0.00002f + randn() * 0.003f;   // drains about 0.07 V per hour
  put(R.vbat, true, constrain(vb, 3.30f, 4.20f));
}

// ================= 5. JSON (same as the contract) =================
static String num(const Val& v, int dec) { return good(v) ? String(v.v, dec) : String("null"); }
static String flag(bool b) { return b ? "true" : "false"; }

static String readingsJson() {
  uint32_t up = millis() / 1000;
  int32_t warm = (int32_t)ZTS_WARMUP_S - (int32_t)up; if (warm < 0) warm = 0;
  String s; s.reserve(700);
  s += "{\"schema\":" + String(API_SCHEMA);
  s += ",\"seq\":" + String(R.seq);
  s += ",\"uptime_s\":" + String(up);
  s += ",\"air\":{\"temp_c\":" + num(R.airT, 1) + ",\"rh_pct\":" + num(R.airRH, 1) + ",\"ok\":" + flag(good(R.airT) && good(R.airRH)) + "}";
  s += ",\"soil\":{\"moisture_pct\":" + num(R.soilM, 1) + ",\"temp_c\":" + num(R.soilT, 1) + ",\"ph\":" + num(R.soilPH, 1);
  s += ",\"ok\":" + flag(good(R.soilM) && good(R.soilT) && good(R.soilPH));
  s += ",\"warming_up\":" + flag(warm > 0) + ",\"warmup_s_left\":" + String(warm) + "}";
  s += ",\"npk\":{\"n_mgkg\":" + num(R.n, 0) + ",\"p_mgkg\":" + num(R.p, 0) + ",\"k_mgkg\":" + num(R.k, 0);
  s += ",\"ok\":" + flag(good(R.n) && good(R.p) && good(R.k)) + "}";
  s += ",\"analog_moisture_pct\":" + num(R.moistA, 1);
  s += ",\"battery_v\":" + num(R.vbat, 2);
  s += ",\"gps\":null}";
  return s;
}

static String idSuffix() { String m = WiFi.macAddress(); m.replace(":", ""); return m.substring(8); }

// ================= 6. Web server =================
WebServer server(80);
static void sendJson(const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", body);
}

// ================= 7. LCD, buttons, buzzer =================
LiquidCrystal_I2C lcd(0x27, 16, 2);          // use 0x3F if the screen stays blank
String ssidName;
const uint8_t NUM_PAGES = 5;
uint8_t page = 0;
bool needRedraw = true;

static void buzzerOn()  { digitalWrite(BUZZER_PIN, BUZZER_ACTIVE_LOW ? LOW : HIGH); }
static void buzzerOff() { digitalWrite(BUZZER_PIN, BUZZER_ACTIVE_LOW ? HIGH : LOW); }
static void beep(int ms) {
  if (BUZZER_PASSIVE) {
    uint32_t end = millis() + ms;
    while (millis() < end) { buzzerOn(); delayMicroseconds(250); buzzerOff(); delayMicroseconds(250); }
  } else {
    buzzerOn(); delay(ms); buzzerOff();
  }
}

static void lcdLine(uint8_t row, const char* text) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", text);
  lcd.setCursor(0, row);
  lcd.print(buf);
}

static void fmtVal(char* out, size_t n, const Val& v, int dec) {
  if (good(v)) snprintf(out, n, "%.*f", dec, v.v);
  else         snprintf(out, n, "--");
}

static void drawPage() {
  char l1[40], a[12], b[12], c[12];
  int32_t warm = (int32_t)ZTS_WARMUP_S - (int32_t)(millis() / 1000);
  switch (page) {
    case 0:
      lcdLine(0, "AIR TEMP / HUM");
      if (good(R.airT) && good(R.airRH)) {
        fmtVal(a, sizeof(a), R.airT, 1); fmtVal(b, sizeof(b), R.airRH, 1);
        snprintf(l1, sizeof(l1), "T%sC H%s%%", a, b);
        lcdLine(1, l1);
      } else lcdLine(1, "NO SENSOR DATA");
      break;
    case 1:
      if (warm > 0) { snprintf(l1, sizeof(l1), "SOIL warm %ds", (int)warm); lcdLine(0, l1); }
      else lcdLine(0, "SOIL M/T/pH");
      if (good(R.soilM) && good(R.soilT) && good(R.soilPH)) {
        fmtVal(a, sizeof(a), R.soilM, 1); fmtVal(b, sizeof(b), R.soilT, 1); fmtVal(c, sizeof(c), R.soilPH, 1);
        snprintf(l1, sizeof(l1), "%s%% %sC %s", a, b, c);
        lcdLine(1, l1);
      } else lcdLine(1, "NO SENSOR DATA");
      break;
    case 2:
      lcdLine(0, "NPK mg/kg");
      if (good(R.n) && good(R.p) && good(R.k)) {
        fmtVal(a, sizeof(a), R.n, 0); fmtVal(b, sizeof(b), R.p, 0); fmtVal(c, sizeof(c), R.k, 0);
        snprintf(l1, sizeof(l1), "N%s P%s K%s", a, b, c);
        lcdLine(1, l1);
      } else lcdLine(1, "NO SENSOR DATA");
      break;
    case 3:
      lcdLine(0, ssidName.c_str());
      snprintf(l1, sizeof(l1), "192.168.4.1 c:%d", WiFi.softAPgetStationNum());
      lcdLine(1, l1);
      break;
    case 4:
      snprintf(l1, sizeof(l1), "seq %lu up %lus", (unsigned long)R.seq, (unsigned long)(millis() / 1000));
      lcdLine(0, l1);
      snprintf(l1, sizeof(l1), "SIM fault:%s", faultName(fault));
      lcdLine(1, l1);
      break;
  }
}

static void setFault(int f) {
  fault = (Fault)f;
  Serial.printf("Simulated fault: %s\n", faultName(fault));
  beep(150);
  needRedraw = true;
}

static void handleButtons() {
  static uint32_t bothSince = 0, singleSince = 0;
  static bool bothDone = false, singleDone = false, singleActive = false;
  bool up = (digitalRead(BTN_UP) == LOW);
  bool down = (digitalRead(BTN_DOWN) == LOW);

  if (up && down) {
    singleActive = false; singleDone = false;
    if (!bothSince) bothSince = millis();
    if (!bothDone && millis() - bothSince >= 1500) {     // hold both for 1.5 s: next simulated fault
      bothDone = true;
      setFault((Fault)((fault + 1) % FAULT_COUNT));
    }
  } else {
    bothSince = 0; bothDone = false;
    if (up != down) {                                    // exactly one button pressed
      if (!singleActive) { singleActive = true; singleDone = false; singleSince = millis(); }
      if (!singleDone && millis() - singleSince >= 80) { // debounced
        singleDone = true;
        if (up) page = (page + 1) % NUM_PAGES;
        else    page = (page == 0) ? NUM_PAGES - 1 : page - 1;
        beep(60);
        needRedraw = true;
      }
    } else {
      singleActive = false; singleDone = false;
    }
  }
}

static void handleSerial() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '0') setFault(FAULT_NONE);
    else if (ch == 'n' || ch == 'N') setFault(FAULT_NPK);
    else if (ch == 'z' || ch == 'Z') setFault(FAULT_SOIL);
    else if (ch == 'a' || ch == 'A') setFault(FAULT_AIR);
  }
}

// ================= 8. Setup and loop =================
void setup() {
  // keep the buzzer silent from the very first moment
  pinMode(BUZZER_PIN, OUTPUT);
  buzzerOff();

  Serial.begin(115200);
  delay(500);
  Serial.println("\nSOIL NODE SIMULATOR (fake sensor values, real Wi-Fi + API)");

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.init();
  lcd.backlight();
  lcdLine(0, "SOIL NODE SIM");
  lcdLine(1, "Starting...");

#if USE_REAL_AIR
  if (PIN_AIR_DE >= 0) { pinMode(PIN_AIR_DE, OUTPUT); digitalWrite(PIN_AIR_DE, LOW); }
  AirSerial.begin(AIR_BAUD, SERIAL_8N1, PIN_AIR_RX, PIN_AIR_TX);
#endif

  WiFi.mode(WIFI_AP);
  ssidName = "SoilNode-" + idSuffix();
  WiFi.softAP(ssidName.c_str(), AP_PASSWORD, AP_CHANNEL, 0, 4);
  Serial.printf("Hotspot: %s  password: %s  IP: %s\n", ssidName.c_str(), AP_PASSWORD,
                WiFi.softAPIP().toString().c_str());

  server.on("/api/info", HTTP_GET, []() {
    String s = "{\"device\":\"soil-node\",\"id\":\"" + idSuffix() + "\",\"fw\":\"" FW_VERSION "\",\"schema\":" + String(API_SCHEMA);
    s += ",\"ssid\":\"" + ssidName + "\",\"uptime_s\":" + String(millis() / 1000) + "}";
    sendJson(s);
  });
  server.on("/api/readings", HTTP_GET, []() { sendJson(readingsJson()); });
  server.onNotFound([]() { server.send(404, "application/json", "{\"error\":\"not found\"}"); });
  server.begin();

  // one full pass now, so the very first request already has values
  pollAir(); pollNPK(); pollSoil(); pollAnalog();
  R.seq = 1;

  lcdLine(0, ssidName.c_str());
  lcdLine(1, "pw soilnode123");
  beep(150);
  delay(1500);
  needRedraw = true;
}

void loop() {
  server.handleClient();
  handleButtons();
  handleSerial();

  static uint32_t lastPoll = 0, lastDraw = 0;
  static uint8_t step = 0;
  if (millis() - lastPoll >= 1000) {                    // one group per second: each refreshed about every 4 s
    lastPoll = millis();
    switch (step++ % 4) {
      case 0: pollAir(); break;
      case 1: pollNPK(); break;
      case 2: pollSoil(); break;
      case 3: pollAnalog(); R.seq++;
              Serial.printf("seq %lu  airT %s  soilM %s  pH %s  N %s  P %s  K %s\n",
                            (unsigned long)R.seq, num(R.airT, 1).c_str(), num(R.soilM, 1).c_str(),
                            num(R.soilPH, 1).c_str(), num(R.n, 0).c_str(), num(R.p, 0).c_str(),
                            num(R.k, 0).c_str());
              break;
    }
    needRedraw = true;
  }
  if (needRedraw || millis() - lastDraw >= 500) {
    lastDraw = millis();
    needRedraw = false;
    drawPage();
  }
}
