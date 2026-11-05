/*
  soil_node_api.ino - reference firmware for the Soil Monitor phone app
  Board: Waveshare ESP32-S3-A7670E-4G ("ESP32S3 Dev Module", USB CDC on boot Enabled, PSRAM Disabled, Upload Speed 115200)
  Arduino core: ESP32 by Espressif 3.x. No extra libraries (WiFi, WebServer and Wire ship with the core).

  What it does:
    - starts its own Wi-Fi hotspot  "SoilNode-XXXX"  (XXXX = last 4 hex digits of the MAC), IP 192.168.4.1
    - reads the three RS485 sensors + analog moisture (+ battery gauge) in the background, about every 4 s
    - serves the latest values as JSON:   GET /api/info    GET /api/readings
  The phone app only needs those two endpoints. See docs/04-api-contract.md for the exact JSON rules.

  NOT yet compiled or run on hardware: treat as a first version to bring up and test.
  Prototype mode: always awake (no deep sleep), because the app shows live values.
*/
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <math.h>

// ================= 1. SETTINGS the hardware engineer edits =================
#define FW_VERSION "0.1.0"
#define API_SCHEMA 1                         // bump only if the JSON structure changes

const char* AP_PASSWORD = "soilnode123";     // 8 characters or more. Tell the app team if you change it.
const int   AP_CHANNEL  = 1;

// RS485. Default: bus A = NPK + ZTS, bus B = TH-RS (two driver boards, two UARTs).
// If all three sensors share ONE driver board, set SINGLE_BUS to 1 (bus A pins carry everything).
#define SINGLE_BUS 0
const int PIN_A_TX = 14, PIN_A_RX = 13, PIN_A_DE = 12;   // DE and RE joined; -1 = module switches direction itself
const int PIN_B_TX = 40, PIN_B_RX = 39, PIN_B_DE = -1;
const uint32_t RS485_BAUD = 9600;
const uint8_t ADDR_NPK = 1, ADDR_TH = 2, ADDR_ZTS = 3;    // set on the bench BEFORE joining sensors to one bus

const int PIN_SENSOR_EN = -1;                // GPIO that switches sensor power (HIGH = on), -1 if sensors are always powered
const int PIN_MOIST     = 1;                 // analog soil-moisture sensor, ADC1 pin
const int ADC_AIR = 3000, ADC_WATER = 1300;  // raw ADC in dry air / in water: CALIBRATE
const uint32_t ZTS_WARMUP_S = 300;           // ZTS manual: about 5 minutes after power-up

#define HAVE_GAUGE 1                         // on-board MAX17048 battery gauge (I2C on IO3/IO2). Set 0 if absent.
const int PIN_GAUGE_SDA = 3, PIN_GAUGE_SCL = 2;

// ================= 2. Data =================
struct Val { float v = NAN; uint8_t fails = 0; };       // fails counts consecutive misses
struct Readings {
  Val airT, airRH, soilM, soilT, soilPH, n, p, k, moistA, vbat;
  uint32_t seq = 0;                                     // +1 after each full pass over all sensors
} R;

static bool good(const Val& a) { return !isnan(a.v) && a.fails < 3; }
static void put(Val& d, bool ok, float v) {
  if (ok) { d.v = v; d.fails = 0; }
  else if (d.fails < 255 && ++d.fails >= 3) d.v = NAN;  // after 3 misses the value becomes null in the JSON
}

// ================= 3. Modbus RTU (function 0x03) =================
struct Port { HardwareSerial* s; int de; };
HardwareSerial UartA(1), UartB(2);
Port portA{&UartA, PIN_A_DE}, portB{&UartB, PIN_B_DE};
Port *pNPK, *pZTS, *pTH;

static uint16_t crc16(const uint8_t* d, size_t n) {
  uint16_t c = 0xFFFF;
  for (size_t i = 0; i < n; i++) { c ^= d[i]; for (int b = 0; b < 8; b++) c = (c & 1) ? (c >> 1) ^ 0xA001 : c >> 1; }
  return c;
}

static bool readRegs(Port& p, uint8_t addr, uint16_t reg, uint8_t count, uint16_t* out) {
  uint8_t f[8] = {addr, 0x03, (uint8_t)(reg >> 8), (uint8_t)reg, 0, count, 0, 0};
  uint16_t c = crc16(f, 6); f[6] = c & 0xFF; f[7] = c >> 8;          // CRC low byte first
  const size_t want = 5 + 2 * count;
  for (int attempt = 0; attempt < 2; attempt++) {
    while (p.s->available()) p.s->read();
    if (p.de >= 0) digitalWrite(p.de, HIGH);
    p.s->write(f, 8); p.s->flush();
    if (p.de >= 0) digitalWrite(p.de, LOW);
    uint8_t r[32]; size_t got = 0; uint32_t t0 = millis();
    while (millis() - t0 < 300 && got < want) if (p.s->available()) r[got++] = p.s->read();
    if (got == want && r[0] == addr && r[1] == 0x03 && r[2] == 2 * count) {
      uint16_t rc = crc16(r, want - 2);
      if ((rc & 0xFF) == r[want - 2] && (rc >> 8) == r[want - 1]) {
        for (int i = 0; i < count; i++) out[i] = (r[3 + 2 * i] << 8) | r[4 + 2 * i];
        delay(200); return true;                                       // 200 ms gap between requests
      }
    }
    delay(200);
  }
  return false;
}

// ================= 4. Sensor polling (one group per call, never inside an HTTP handler) =================
static void pollTH() {                       // JXBS-3001-TH-RS: reg 0 = RH x10, reg 1 = temperature x10 (signed)
  uint16_t r[2] = {0, 0};
  bool ok = readRegs(*pTH, ADDR_TH, 0x0000, 2, r);
  put(R.airRH, ok, r[0] / 10.0f);
  put(R.airT,  ok, (int16_t)r[1] / 10.0f);
}
static void pollNPK() {                      // JXBS-3001-NPK-RS: regs 0x001E..0x0020 = N, P, K in mg/kg
  uint16_t r[3] = {0, 0, 0};
  bool ok = readRegs(*pNPK, ADDR_NPK, 0x001E, 3, r);
  put(R.n, ok, r[0]); put(R.p, ok, r[1]); put(R.k, ok, r[2]);
}
static void pollZTS() {                      // ZTS-3001-TR-THPH-N01: moisture x10, temp x10 (signed), [EC absent], pH x10
  uint16_t r[4] = {0, 0, 0, 0};
  bool ok = readRegs(*pZTS, ADDR_ZTS, 0x0000, 4, r);
  put(R.soilM,  ok, r[0] / 10.0f);
  put(R.soilT,  ok, (int16_t)r[1] / 10.0f);
  put(R.soilPH, ok, r[3] / 10.0f);
}
static void pollAnalog() {
  long sum = 0; for (int i = 0; i < 8; i++) { sum += analogRead(PIN_MOIST); delay(5); }
  float raw = sum / 8.0f;
  float pct = 100.0f * (ADC_AIR - raw) / (float)(ADC_AIR - ADC_WATER);
  put(R.moistA, true, constrain(pct, 0.0f, 100.0f));
#if HAVE_GAUGE
  Wire1.beginTransmission(0x36); Wire1.write(0x02);                    // MAX17048 VCELL, 78.125 uV per LSB
  if (Wire1.endTransmission(false) == 0 && Wire1.requestFrom(0x36, 2) == 2) {
    uint16_t raw16 = (Wire1.read() << 8) | Wire1.read();
    put(R.vbat, true, raw16 * 78.125e-6f);
  } else put(R.vbat, false, 0);
#endif
}

// ================= 5. JSON =================
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
  s += ",\"gps\":null}";                        // fill {"lat":..,"lon":..,"fix":true} only if the ESP has a valid GNSS fix
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

void setup() {
  Serial.begin(115200); delay(500);
  if (PIN_SENSOR_EN >= 0) { pinMode(PIN_SENSOR_EN, OUTPUT); digitalWrite(PIN_SENSOR_EN, HIGH); }
  if (PIN_A_DE >= 0) { pinMode(PIN_A_DE, OUTPUT); digitalWrite(PIN_A_DE, LOW); }
  if (PIN_B_DE >= 0) { pinMode(PIN_B_DE, OUTPUT); digitalWrite(PIN_B_DE, LOW); }
  analogReadResolution(12);

  UartA.begin(RS485_BAUD, SERIAL_8N1, PIN_A_RX, PIN_A_TX);
  pNPK = &portA; pZTS = &portA;
#if SINGLE_BUS
  pTH = &portA;
#else
  UartB.begin(RS485_BAUD, SERIAL_8N1, PIN_B_RX, PIN_B_TX);
  pTH = &portB;
#endif
#if HAVE_GAUGE
  Wire1.begin(PIN_GAUGE_SDA, PIN_GAUGE_SCL, 100000);
#endif

  WiFi.mode(WIFI_AP);
  String ssid = "SoilNode-" + idSuffix();
  WiFi.softAP(ssid.c_str(), AP_PASSWORD, AP_CHANNEL, 0, 4);
  Serial.printf("Hotspot: %s  password: %s  IP: %s\n", ssid.c_str(), AP_PASSWORD, WiFi.softAPIP().toString().c_str());

  server.on("/api/info", HTTP_GET, []() {
    String s = "{\"device\":\"soil-node\",\"id\":\"" + idSuffix() + "\",\"fw\":\"" FW_VERSION "\",\"schema\":" + String(API_SCHEMA);
    s += ",\"ssid\":\"SoilNode-" + idSuffix() + "\",\"uptime_s\":" + String(millis() / 1000) + "}";
    sendJson(s);
  });
  server.on("/api/readings", HTTP_GET, []() { sendJson(readingsJson()); });
  server.onNotFound([]() { server.send(404, "application/json", "{\"error\":\"not found\"}"); });
  server.begin();
}

void loop() {
  server.handleClient();
  static uint32_t last = 0; static uint8_t step = 0;
  if (millis() - last >= 1000) {                // one sensor group per second, so each is refreshed about every 4 s
    last = millis();
    switch (step++ % 4) {
      case 0: pollTH(); break;
      case 1: pollNPK(); break;
      case 2: pollZTS(); break;
      case 3: pollAnalog(); R.seq++;
              Serial.printf("seq %lu  airT %s  soilPH %s  N %s\n", (unsigned long)R.seq,
                            num(R.airT, 1).c_str(), num(R.soilPH, 1).c_str(), num(R.n, 0).c_str());
              break;
    }
  }
}
