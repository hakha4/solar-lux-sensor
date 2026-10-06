// =============================================================================
// config.h - Compile-time defaults for the solar lux sensor
//
// These values are used on first boot (or after an NVS erase). Everything here
// can be changed at runtime via the web config page (/config) or the captive
// portal (AP "LuxSensor-xxxx" -> http://192.168.4.1). Runtime values are stored
// in NVS and take precedence over this file.
//
// >>> Replace every "xxx" placeholder below with your own values. <<<
// =============================================================================
#pragma once

// ---------------------------------------------------------------- WiFi -------
#define CFG_WIFI_SSID   "xxx"          // TODO: your WiFi SSID
#define CFG_WIFI_PASS   "xxx"          // TODO: your WiFi password

// ------------------------------------------------------------ Static IP ------
// true = use the static IP below, false = DHCP
#define CFG_STATIC_IP   true

// Octets are comma separated so they can be passed straight to IPAddress(...)
// TODO: replace xxx with real octets, e.g. 192, 168, 1, 50
#define CFG_IP_ADDR     192, 168, 1, 50     // xxx - device IP (unique per sensor!)
#define CFG_IP_GW       192, 168, 1, 1      // xxx - router / gateway
#define CFG_IP_SUBNET   255, 255, 255, 0    // subnet mask
#define CFG_IP_DNS1     192, 168, 1, 1      // xxx - primary DNS (often the router)
#define CFG_IP_DNS2     8, 8, 8, 8          // secondary DNS

// ---------------------------------------------------------------- MQTT -------
#define CFG_MQTT_IP     192, 168, 1, 10     // xxx - MQTT broker IP
#define CFG_MQTT_PORT   1883
#define CFG_MQTT_USER   "xxx"               // TODO: MQTT user ("" if none)
#define CFG_MQTT_PASS   "xxx"               // TODO: MQTT password ("" if none)
// Topics become: {prefix}/{location}/lux and {prefix}/{location}/lwt
#define CFG_MQTT_PREFIX "solar"

// ------------------------------------------------------- Sensor / place ------
#define DEFAULT_LOCATION "east"   // east / south / west / north
#define DEFAULT_SENSOR   0        // 0 = TSL2591, 1 = VEML7700

// I2C pins. ESP32-C3 has no GPIO22, so it uses GPIO8/9 by default.
#if defined(CONFIG_IDF_TARGET_ESP32C3)
  #define I2C_SDA_PIN 8
  #define I2C_SCL_PIN 9
#else
  #define I2C_SDA_PIN 21
  #define I2C_SCL_PIN 22
#endif

// -------------------------------------------------------------- Timing -------
#define PUBLISH_INTERVAL_MS   30000UL   // how often to read + publish
#define WIFI_TIMEOUT_MS       15000UL   // connect timeout before captive portal
#define PORTAL_TIMEOUT_MS     300000UL  // portal restarts device after 5 min idle
                                        // (so it retries WiFi after a router outage)
#define WIFI_LOST_RESTART_MS  300000UL  // restart if WiFi is lost this long

// ----------------------------------------------------------------- NTP -------
#define NTP_SERVER       "pool.ntp.org"
#define TIMEZONE_OFFSET  3600           // CET (UTC+1), seconds
// POSIX TZ string; handles CET/CEST daylight saving automatically
#define TZ_INFO          "CET-1CEST,M3.5.0,M10.5.0/3"

// ------------------------------------------------------------ Firmware -------
#define FW_VERSION       "1.0.0"
