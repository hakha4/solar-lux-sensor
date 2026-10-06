// =============================================================================
// settings.h - Runtime settings struct + NVS persistence (Preferences)
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

struct Settings {
  char ssid[64];
  char wpass[64];
  bool useStaticIP;
  uint8_t ip[4], gw[4], subnet[4], dns1[4], dns2[4];
  uint8_t mqttIP[4];
  uint16_t mqttPort;
  char mqttUser[32];
  char mqttPass[32];
  char mqttPrefix[32];  // topic prefix, e.g. "solar"
  char location[16];    // "east", "south", ...
  uint8_t sensorType;   // 0 = TSL2591, 1 = VEML7700
};

static const char *NVS_NAMESPACE = "lux_cfg";

static inline void setOctets(uint8_t dst[4], uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  dst[0] = a; dst[1] = b; dst[2] = c; dst[3] = d;
}

// Fill with compile-time defaults from config.h
static inline void defaultSettings(Settings &s) {
  memset(&s, 0, sizeof(s));
  strlcpy(s.ssid, CFG_WIFI_SSID, sizeof(s.ssid));
  strlcpy(s.wpass, CFG_WIFI_PASS, sizeof(s.wpass));
  s.useStaticIP = CFG_STATIC_IP;
  setOctets(s.ip, CFG_IP_ADDR);
  setOctets(s.gw, CFG_IP_GW);
  setOctets(s.subnet, CFG_IP_SUBNET);
  setOctets(s.dns1, CFG_IP_DNS1);
  setOctets(s.dns2, CFG_IP_DNS2);
  setOctets(s.mqttIP, CFG_MQTT_IP);
  s.mqttPort = CFG_MQTT_PORT;
  strlcpy(s.mqttUser, CFG_MQTT_USER, sizeof(s.mqttUser));
  strlcpy(s.mqttPass, CFG_MQTT_PASS, sizeof(s.mqttPass));
  strlcpy(s.mqttPrefix, CFG_MQTT_PREFIX, sizeof(s.mqttPrefix));
  strlcpy(s.location, DEFAULT_LOCATION, sizeof(s.location));
  s.sensorType = DEFAULT_SENSOR;
}

// Load from NVS namespace "lux_cfg"; missing keys fall back to config.h
static inline void loadSettings(Settings &s) {
  defaultSettings(s);
  Preferences p;
  if (!p.begin(NVS_NAMESPACE, true)) {   // read-only; fails if never written
    Serial.println(F("[NVS] No saved settings, using config.h defaults"));
    return;
  }
  if (p.isKey("ssid"))   p.getString("ssid", s.ssid, sizeof(s.ssid));
  if (p.isKey("wpass"))  p.getString("wpass", s.wpass, sizeof(s.wpass));
  s.useStaticIP = p.getBool("static", s.useStaticIP);
  if (p.isKey("ip"))     p.getBytes("ip", s.ip, 4);
  if (p.isKey("gw"))     p.getBytes("gw", s.gw, 4);
  if (p.isKey("subnet")) p.getBytes("subnet", s.subnet, 4);
  if (p.isKey("dns1"))   p.getBytes("dns1", s.dns1, 4);
  if (p.isKey("dns2"))   p.getBytes("dns2", s.dns2, 4);
  if (p.isKey("mqttip")) p.getBytes("mqttip", s.mqttIP, 4);
  s.mqttPort = p.getUShort("mqttport", s.mqttPort);
  if (p.isKey("muser"))  p.getString("muser", s.mqttUser, sizeof(s.mqttUser));
  if (p.isKey("mpass"))  p.getString("mpass", s.mqttPass, sizeof(s.mqttPass));
  if (p.isKey("mprefix"))p.getString("mprefix", s.mqttPrefix, sizeof(s.mqttPrefix));
  if (p.isKey("loc"))    p.getString("loc", s.location, sizeof(s.location));
  s.sensorType = p.getUChar("sensor", s.sensorType);
  p.end();
  if (s.sensorType > 1) s.sensorType = DEFAULT_SENSOR;
  Serial.println(F("[NVS] Settings loaded"));
}

static inline bool saveSettings(const Settings &s) {
  Preferences p;
  if (!p.begin(NVS_NAMESPACE, false)) {
    Serial.println(F("[NVS] ERROR: cannot open namespace for writing"));
    return false;
  }
  p.putString("ssid", s.ssid);
  p.putString("wpass", s.wpass);
  p.putBool("static", s.useStaticIP);
  p.putBytes("ip", s.ip, 4);
  p.putBytes("gw", s.gw, 4);
  p.putBytes("subnet", s.subnet, 4);
  p.putBytes("dns1", s.dns1, 4);
  p.putBytes("dns2", s.dns2, 4);
  p.putBytes("mqttip", s.mqttIP, 4);
  p.putUShort("mqttport", s.mqttPort);
  p.putString("muser", s.mqttUser);
  p.putString("mpass", s.mqttPass);
  p.putString("mprefix", s.mqttPrefix);
  p.putString("loc", s.location);
  p.putUChar("sensor", s.sensorType);
  p.end();
  Serial.println(F("[NVS] Settings saved"));
  return true;
}
