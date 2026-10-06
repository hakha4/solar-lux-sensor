// =============================================================================
// lux_sensor.ino - ESP32 lux sensor for solar forecasting
//
// Sensors : TSL2591 (auto-gain) or VEML7700 (auto lux), selectable at runtime
// Output  : JSON over MQTT -> {prefix}/{location}/lux, LWT on {prefix}/{location}/lwt
// Web     : /  status  | /config settings | /update OTA | /json latest reading
// Board   : ESP32C3 Dev Module (also works on classic ESP32)
// =============================================================================
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_TSL2591.h>
#include <Adafruit_VEML7700.h>

#include "config.h"
#include "settings.h"
#include "portal.h"
#include "ota_handler.h"

// ------------------------------------------------------------- globals -------
Settings cfg;
WebServer server(80);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
Adafruit_TSL2591 tsl(2591);
Adafruit_VEML7700 veml;

enum SensorKind : uint8_t { SENSOR_TSL2591 = 0, SENSOR_VEML7700 = 1, SENSOR_NONE = 255 };
SensorKind activeSensor = SENSOR_NONE;

struct Reading {
  bool valid = false;
  float lux = 0;
  uint16_t full = 0, ir = 0;
  uint32_t gainX = 0;
  String timestamp;
} lastReading;

String topicLux, topicLwt, clientId;
unsigned long lastPublish = 0;
unsigned long lastMqttAttempt = 0;
unsigned long mqttBackoffMs = 1000;
unsigned long wifiLostSince = 0;
bool timeSynced = false;

// ------------------------------------------------------------- helpers -------
static String chipIdHex() {
  char buf[13];
  snprintf(buf, sizeof(buf), "%012llX", ESP.getEfuseMac());
  return String(buf);
}

static const char *sensorName(SensorKind k) {
  switch (k) {
    case SENSOR_TSL2591: return "TSL2591";
    case SENSOR_VEML7700: return "VEML7700";
    default: return "none";
  }
}

// ------------------------------------------------------------------ WiFi -----
bool connectWiFi(Settings &s) {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(("luxsensor-" + String(s.location)).c_str());
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  if (s.useStaticIP) {
    IPAddress ip(s.ip), gw(s.gw), sn(s.subnet), d1(s.dns1), d2(s.dns2);
    if (!WiFi.config(ip, gw, sn, d1, d2)) Serial.println(F("[WiFi] Static IP config failed"));
    Serial.printf("[WiFi] Static IP %s\n", ip.toString().c_str());
  } else {
    Serial.println(F("[WiFi] Using DHCP"));
  }

  Serial.printf("[WiFi] Connecting to '%s'", s.ssid);
  WiFi.begin(s.ssid, s.wpass);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[WiFi] Connection timeout"));
    return false;
  }
  Serial.printf("[WiFi] Connected, IP %s, RSSI %d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

// ------------------------------------------------------------------- NTP -----
void setupTime() {
  // POSIX TZ handles CET/CEST switching (TIMEZONE_OFFSET kept for reference)
  configTzTime(TZ_INFO, NTP_SERVER);
  struct tm t;
  timeSynced = getLocalTime(&t, 10000);
  Serial.println(timeSynced ? F("[NTP] Time synced") : F("[NTP] Sync failed (will retry in background)"));
}

String getTimestamp() {
  struct tm t;
  if (!getLocalTime(&t, 50)) return String("");
  timeSynced = true;
  char buf[20];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &t);
  return String(buf);
}

// ------------------------------------------------------------------ MQTT -----
void buildTopics() {
  topicLux = String(cfg.mqttPrefix) + "/" + cfg.location + "/lux";
  topicLwt = String(cfg.mqttPrefix) + "/" + cfg.location + "/lwt";
  clientId = "lux_" + String(cfg.location) + "_" + chipIdHex();
}

bool mqttTryOnce() {
  const char *user = strlen(cfg.mqttUser) ? cfg.mqttUser : nullptr;
  const char *pass = strlen(cfg.mqttPass) ? cfg.mqttPass : nullptr;
  bool ok = mqtt.connect(clientId.c_str(), user, pass,
                         topicLwt.c_str(), 1, true, "offline");
  if (ok) {
    mqtt.publish(topicLwt.c_str(), "online", true);
    Serial.printf("[MQTT] Connected as %s\n", clientId.c_str());
  } else {
    Serial.printf("[MQTT] Connect failed, state=%d\n", mqtt.state());
  }
  return ok;
}

// Blocking connect used in setup(): backoff 1,2,4,8,16 s (max 30 s), 5 tries
bool connectMQTT() {
  mqtt.setServer(IPAddress(cfg.mqttIP), cfg.mqttPort);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(60);
  unsigned long backoff = 1000;
  for (int attempt = 1; attempt <= 5; attempt++) {
    Serial.printf("[MQTT] Attempt %d -> %s:%u\n", attempt,
                  IPAddress(cfg.mqttIP).toString().c_str(), cfg.mqttPort);
    if (mqttTryOnce()) { mqttBackoffMs = 1000; return true; }
    if (attempt < 5) {
      delay(backoff);
      backoff = min(backoff * 2, 30000UL);
    }
  }
  Serial.println(F("[MQTT] Giving up for now, will retry in loop()"));
  lastMqttAttempt = millis();
  return false;
}

// Non-blocking reconnect used in loop(): exponential backoff, max 30 s
void maintainMQTT() {
  if (mqtt.connected()) { mqtt.loop(); return; }
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastMqttAttempt < mqttBackoffMs) return;
  lastMqttAttempt = millis();
  if (mqttTryOnce()) {
    mqttBackoffMs = 1000;
  } else {
    mqttBackoffMs = min(mqttBackoffMs * 2, 30000UL);
    Serial.printf("[MQTT] Next retry in %lu ms\n", mqttBackoffMs);
  }
}

// --------------------------------------------------------------- Sensors -----
// TSL2591 gain ladder: LOW(1x) -> MED(25x) -> HIGH(428x) -> MAX(9876x)
static const tsl2591Gain_t GAINS[] = {TSL2591_GAIN_LOW, TSL2591_GAIN_MED,
                                      TSL2591_GAIN_HIGH, TSL2591_GAIN_MAX};
static const uint32_t GAIN_X[] = {1, 25, 428, 9876};
static int8_t gainIdx = 1;  // start at MED

bool initTSL() {
  if (!tsl.begin(&Wire)) return false;
  tsl.setGain(GAINS[gainIdx]);
  tsl.setTiming(TSL2591_INTEGRATIONTIME_300MS);
  return true;
}

bool initVEML() {
  if (!veml.begin(&Wire)) return false;
  veml.setGain(VEML7700_GAIN_1_8);
  veml.setIntegrationTime(VEML7700_IT_100MS);
  return true;
}

// Init the configured sensor; if missing, fall back to the other one
void initSensor() {
  SensorKind wanted = (SensorKind)cfg.sensorType;
  SensorKind other = (wanted == SENSOR_TSL2591) ? SENSOR_VEML7700 : SENSOR_TSL2591;
  for (SensorKind k : {wanted, other}) {
    bool ok = (k == SENSOR_TSL2591) ? initTSL() : initVEML();
    if (ok) {
      activeSensor = k;
      if (k != wanted)
        Serial.printf("[SENSOR] WARNING: %s not found, using %s instead\n",
                      sensorName(wanted), sensorName(k));
      else
        Serial.printf("[SENSOR] %s found\n", sensorName(k));
      return;
    }
    Serial.printf("[SENSOR] %s not found on I2C (SDA=%d SCL=%d)\n",
                  sensorName(k), I2C_SDA_PIN, I2C_SCL_PIN);
  }
  activeSensor = SENSOR_NONE;
  Serial.println(F("[SENSOR] ERROR: no sensor found, check wiring"));
}

// Auto-gain read (max 4 iterations). Blocks ~0.3-0.6 s per iteration.
bool readTSL(Reading &r) {
  uint16_t full = 0, ir = 0;
  for (int iter = 0; iter < 4; iter++) {
    uint32_t lum = tsl.getFullLuminosity();
    ir = lum >> 16;
    full = lum & 0xFFFF;
    if (full >= 0xFFF0 || ir >= 0xFFF0) {          // saturated -> less gain
      if (gainIdx == 0) break;
      tsl.setGain(GAINS[--gainIdx]);
    } else if (full < 100) {                       // too dark -> more gain
      if (gainIdx == 3) break;
      tsl.setGain(GAINS[++gainIdx]);
    } else {
      break;                                       // good range
    }
  }
  float lux = tsl.calculateLux(full, ir);
  if (lux < 0 || isnan(lux)) {
    // Overflow at LOW gain = extremely bright; very dark => treat as 0
    if (full >= 0xFFF0) { Serial.println(F("[TSL] Saturated at lowest gain")); return false; }
    lux = 0;
  }
  r.lux = lux; r.full = full; r.ir = ir; r.gainX = GAIN_X[gainIdx];
  return true;
}

bool readVEML(Reading &r) {
  float lux = veml.readLux(VEML_LUX_AUTO);
  if (isnan(lux) || lux < 0) return false;
  r.lux = lux; r.full = 0; r.ir = 0; r.gainX = 0;  // not applicable
  return true;
}

bool readSensor(Reading &r) {
  r.valid = false;
  if (activeSensor == SENSOR_NONE) {
    initSensor();                       // hot-plug retry
    if (activeSensor == SENSOR_NONE) return false;
  }
  bool ok = (activeSensor == SENSOR_TSL2591) ? readTSL(r) : readVEML(r);
  r.valid = ok;
  r.timestamp = getTimestamp();
  return ok;
}

// ---------------------------------------------------------------- Publish ----
String buildJson(const Reading &r) {
  JsonDocument doc;
  doc["lux"] = serialized(String(r.lux, 2));
  doc["full"] = r.full;
  doc["ir"] = r.ir;
  doc["gain_x"] = r.gainX;
  doc["sensor"] = sensorName(activeSensor);
  doc["location"] = cfg.location;
  doc["timestamp"] = r.timestamp;
  doc["epoch"] = timeSynced ? (uint32_t)time(nullptr) : 0;
  doc["uptime_s"] = millis() / 1000;
  doc["rssi"] = WiFi.RSSI();
  doc["wifi_ok"] = WiFi.status() == WL_CONNECTED;
  doc["mqtt_ok"] = mqtt.connected();
  String out;
  serializeJson(doc, out);
  return out;
}

void readAndPublish() {
  Reading r;
  if (!readSensor(r)) {
    Serial.println(F("[SENSOR] Read failed"));
    return;
  }
  lastReading = r;
  String json = buildJson(r);
  Serial.printf("[DATA] %s\n", json.c_str());
  if (mqtt.connected()) {
    if (!mqtt.publish(topicLux.c_str(), json.c_str()))
      Serial.println(F("[MQTT] Publish failed"));
  }
}

// -------------------------------------------------------------- Web server ---
void handleStatus() {
  unsigned long up = millis() / 1000;
  char upStr[32];
  snprintf(upStr, sizeof(upStr), "%lud %02lu:%02lu:%02lu",
           up / 86400, (up / 3600) % 24, (up / 60) % 60, up % 60);

  String h;
  h.reserve(2500);
  h += F("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
         "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
         "<meta http-equiv=\"refresh\" content=\"30\"><title>LuxSensor</title>"
         "<style>body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;max-width:480px;margin:auto;padding:16px;background:#f2f4f7}"
         ".big{font-size:2.6em;font-weight:700;color:#e67e22}table{width:100%;background:#fff;border-radius:10px;padding:8px}"
         "td{padding:6px}td:first-child{color:#667}.ok{color:#27ae60}.bad{color:#c0392b}</style></head><body>");
  h += F("<h1>&#9728; LuxSensor "); h += htmlEscape(cfg.location); h += F("</h1><div class=\"big\">");
  h += lastReading.valid ? String(lastReading.lux, 1) + " lx" : String("&ndash;");
  h += F("</div><table>");
  auto row = [&h](const char *k, const String &v) {
    h += F("<tr><td>"); h += k; h += F("</td><td>"); h += v; h += F("</td></tr>");
  };
  row("Sensor", sensorName(activeSensor));
  if (activeSensor == SENSOR_TSL2591)
    row("Full / IR / gain", String(lastReading.full) + " / " + lastReading.ir + " / " + lastReading.gainX + "x");
  row("Last reading", lastReading.timestamp.length() ? lastReading.timestamp : String("&ndash;"));
  row("WiFi", WiFi.status() == WL_CONNECTED
                ? "<span class=ok>OK</span> (" + String(WiFi.RSSI()) + " dBm)"
                : String("<span class=bad>down</span>"));
  row("MQTT", mqtt.connected() ? String("<span class=ok>OK</span>") : String("<span class=bad>down</span>"));
  row("Topic", htmlEscape(topicLux.c_str()));
  row("IP", WiFi.localIP().toString());
  row("Uptime", upStr);
  row("Firmware", FW_VERSION);
  h += F("</table><p><a href=\"/config\">Settings</a> &middot; <a href=\"/update\">Firmware update</a>"
         " &middot; <a href=\"/json\">JSON</a></p></body></html>");
  server.send(200, "text/html", h);
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleStatus);
  server.on("/json", HTTP_GET, []() {
    server.send(200, "application/json", buildJson(lastReading));
  });
  registerConfigRoutes(server, cfg);
  setupOTA(server);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
  Serial.println(F("[WEB] Server started on port 80"));
}

// ------------------------------------------------------------------ setup ----
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\n\n=== LuxSensor v%s ===\n", FW_VERSION);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  loadSettings(cfg);
  buildTopics();
  Serial.printf("[CFG] location=%s sensor=%s topic=%s\n",
                cfg.location, sensorName((SensorKind)cfg.sensorType), topicLux.c_str());

  if (!connectWiFi(cfg)) {
    launchPortal(cfg, server);   // blocks, restarts on save/timeout
  }

  setupTime();
  initSensor();
  connectMQTT();
  setupWebServer();

  Serial.printf("[READY] http://%s/  (config: /config, OTA: /update)\n",
                WiFi.localIP().toString().c_str());
  lastPublish = millis() - PUBLISH_INTERVAL_MS;  // publish right away
}

// ------------------------------------------------------------------- loop ----
void loop() {
  server.handleClient();

  // WiFi watchdog: auto-reconnect is on; restart if down too long
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiLostSince == 0) {
      wifiLostSince = millis();
      Serial.println(F("[WiFi] Connection lost"));
    } else if (millis() - wifiLostSince > WIFI_LOST_RESTART_MS) {
      Serial.println(F("[WiFi] Lost too long, restarting"));
      ESP.restart();
    }
  } else if (wifiLostSince != 0) {
    wifiLostSince = 0;
    Serial.printf("[WiFi] Reconnected, RSSI %d dBm\n", WiFi.RSSI());
  }

  maintainMQTT();

  if (millis() - lastPublish >= PUBLISH_INTERVAL_MS) {
    lastPublish = millis();
    readAndPublish();
  }
}
