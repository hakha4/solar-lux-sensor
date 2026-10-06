// =============================================================================
// portal.h - Configuration web form + captive portal
//
//  * launchPortal(): blocking AP-mode captive portal (192.168.4.1) used when
//    WiFi cannot connect. Restarts after save, or after PORTAL_TIMEOUT_MS idle.
//  * registerConfigRoutes(): same form served on /config in normal (STA) mode.
// =============================================================================
#pragma once
#ifndef PORTAL_H
#define PORTAL_H

#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include "settings.h"

static const char *LOCATIONS[] = {"east", "south", "west", "north"};
static const uint8_t NUM_LOCATIONS = sizeof(LOCATIONS) / sizeof(LOCATIONS[0]);

// ------------------------------------------------------------- helpers -------
static String htmlEscape(const char *in) {
  String out;
  for (const char *c = in; *c; ++c) {
    switch (*c) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += *c;
    }
  }
  return out;
}

static String ipToStr(const uint8_t o[4]) {
  return String(o[0]) + "." + o[1] + "." + o[2] + "." + o[3];
}

static bool parseIP(const String &str, uint8_t out[4]) {
  IPAddress a;
  if (!a.fromString(str)) return false;
  for (int i = 0; i < 4; i++) out[i] = a[i];
  return true;
}

static String chipIdShort() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[7];
  snprintf(buf, sizeof(buf), "%06X", (unsigned int)((mac >> 24) & 0xFFFFFF));
  return String(buf);
}

static const char PORTAL_CSS[] PROGMEM = R"CSS(
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;margin:0;background:#f2f4f7;color:#222}
.wrap{max-width:480px;margin:auto;padding:16px}
h1{font-size:1.4em;margin:8px 0 16px}
fieldset{border:1px solid #ccd;border-radius:10px;background:#fff;margin:0 0 14px;padding:10px 14px}
legend{font-weight:600;padding:0 6px}
label{display:block;font-size:.9em;margin:8px 0 3px}
input,select{width:100%;box-sizing:border-box;padding:10px;font-size:16px;border:1px solid #bbc;border-radius:7px}
input[type=checkbox]{width:auto;margin-right:8px;transform:scale(1.3)}
.row{display:flex;align-items:center;margin-top:8px}
button{width:100%;padding:14px;font-size:17px;border:0;border-radius:9px;background:#f39c12;color:#fff;font-weight:600}
.hint{font-size:.8em;color:#667}
a{color:#2266cc}
</style>
)CSS";

static void addField(String &h, const char *label, const char *name, const String &val,
                     const char *type = "text") {
  h += F("<label>"); h += label; h += F("</label><input type=\"");
  h += type; h += F("\" name=\""); h += name; h += F("\" value=\"");
  h += val; h += F("\">");
}

// Build the settings form pre-filled with current values
static String buildConfigPage(const Settings &s, bool apMode) {
  String h;
  h.reserve(6000);
  h += F("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>LuxSensor config</title>");
  h += FPSTR(PORTAL_CSS);
  h += F("</head><body><div class=\"wrap\"><h1>&#9728; LuxSensor ");
  h += htmlEscape(s.location);
  h += F("</h1><form method=\"POST\" action=\"/save\">");

  h += F("<fieldset><legend>WiFi</legend>");
  addField(h, "SSID", "ssid", htmlEscape(s.ssid));
  addField(h, "Password", "wpass", htmlEscape(s.wpass), "password");
  h += F("<div class=\"row\"><input type=\"checkbox\" name=\"static\" id=\"st\" value=\"1\"");
  if (s.useStaticIP) h += F(" checked");
  h += F("><label for=\"st\" style=\"margin:0\">Static IP</label></div>");
  addField(h, "IP address", "ip", ipToStr(s.ip));
  addField(h, "Gateway", "gw", ipToStr(s.gw));
  addField(h, "Subnet mask", "subnet", ipToStr(s.subnet));
  addField(h, "DNS 1", "dns1", ipToStr(s.dns1));
  addField(h, "DNS 2", "dns2", ipToStr(s.dns2));
  h += F("<p class=\"hint\">IP fields are ignored when Static IP is unchecked (DHCP).</p></fieldset>");

  h += F("<fieldset><legend>MQTT</legend>");
  addField(h, "Broker IP", "mqttip", ipToStr(s.mqttIP));
  addField(h, "Port", "mqttport", String(s.mqttPort), "number");
  addField(h, "User", "muser", htmlEscape(s.mqttUser));
  addField(h, "Password", "mpass", htmlEscape(s.mqttPass), "password");
  addField(h, "Topic prefix", "mprefix", htmlEscape(s.mqttPrefix));
  h += F("<p class=\"hint\">Topics: {prefix}/{location}/lux and {prefix}/{location}/lwt</p></fieldset>");

  h += F("<fieldset><legend>Sensor</legend><label>Location</label><select name=\"loc\">");
  for (uint8_t i = 0; i < NUM_LOCATIONS; i++) {
    h += F("<option value=\""); h += LOCATIONS[i]; h += '"';
    if (strcmp(s.location, LOCATIONS[i]) == 0) h += F(" selected");
    h += '>'; h += LOCATIONS[i]; h += F("</option>");
  }
  h += F("</select><label>Sensor type</label><select name=\"sensor\">");
  h += F("<option value=\"0\""); if (s.sensorType == 0) h += F(" selected"); h += F(">TSL2591</option>");
  h += F("<option value=\"1\""); if (s.sensorType == 1) h += F(" selected"); h += F(">VEML7700</option>");
  h += F("</select><p class=\"hint\">If the selected sensor is not found, the other one is tried automatically.</p></fieldset>");

  h += F("<button type=\"submit\">Save &amp; restart</button></form>");
  if (!apMode) h += F("<p><a href=\"/\">&larr; Status</a> &middot; <a href=\"/update\">Firmware update</a></p>");
  h += F("</div></body></html>");
  return h;
}

// Parse POST /save into s. Invalid IPs keep their previous value.
static void parseConfigForm(WebServer &server, Settings &s) {
  if (server.hasArg("ssid"))  strlcpy(s.ssid, server.arg("ssid").c_str(), sizeof(s.ssid));
  if (server.hasArg("wpass")) strlcpy(s.wpass, server.arg("wpass").c_str(), sizeof(s.wpass));
  s.useStaticIP = server.hasArg("static");
  parseIP(server.arg("ip"), s.ip);
  parseIP(server.arg("gw"), s.gw);
  parseIP(server.arg("subnet"), s.subnet);
  parseIP(server.arg("dns1"), s.dns1);
  parseIP(server.arg("dns2"), s.dns2);
  parseIP(server.arg("mqttip"), s.mqttIP);
  long port = server.arg("mqttport").toInt();
  if (port > 0 && port <= 65535) s.mqttPort = (uint16_t)port;
  if (server.hasArg("muser")) strlcpy(s.mqttUser, server.arg("muser").c_str(), sizeof(s.mqttUser));
  if (server.hasArg("mpass")) strlcpy(s.mqttPass, server.arg("mpass").c_str(), sizeof(s.mqttPass));

  // Topic prefix: trim, strip MQTT wildcards and trailing slashes
  String pre = server.arg("mprefix");
  pre.trim();
  pre.replace("#", ""); pre.replace("+", ""); pre.replace(" ", "_");
  while (pre.endsWith("/")) pre.remove(pre.length() - 1);
  if (pre.length() > 0) strlcpy(s.mqttPrefix, pre.c_str(), sizeof(s.mqttPrefix));

  String loc = server.arg("loc");
  for (uint8_t i = 0; i < NUM_LOCATIONS; i++)
    if (loc == LOCATIONS[i]) strlcpy(s.location, LOCATIONS[i], sizeof(s.location));

  long st = server.arg("sensor").toInt();
  if (st == 0 || st == 1) s.sensorType = (uint8_t)st;
}

static void sendSavedAndRestart(WebServer &server) {
  String h = F("<!DOCTYPE html><html><head><meta charset=\"utf-8\">");
  h += FPSTR(PORTAL_CSS);
  h += F("</head><body><div class=\"wrap\"><h1>Saved! Restarting...</h1>"
         "<p>The device restarts with the new settings.</p></div></body></html>");
  server.send(200, "text/html", h);
  delay(1500);  // let the response reach the browser
  ESP.restart();
}

// Normal (STA) mode: /config + /save on the main web server
static void registerConfigRoutes(WebServer &server, Settings &s) {
  server.on("/config", HTTP_GET, [&server, &s]() {
    server.send(200, "text/html", buildConfigPage(s, false));
  });
  server.on("/save", HTTP_POST, [&server, &s]() {
    parseConfigForm(server, s);
    saveSettings(s);
    sendSavedAndRestart(server);
  });
}

// Blocking captive portal. Never returns (restarts the ESP).
static void launchPortal(Settings &s, WebServer &server) {
  static DNSServer dns;
  const IPAddress apIP(192, 168, 4, 1);
  String apName = "LuxSensor-" + chipIdShort();

  Serial.printf("[PORTAL] Starting AP '%s' at 192.168.4.1\n", apName.c_str());
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apName.c_str());   // open network
  delay(200);

  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", apIP);

  unsigned long lastActivity = millis();

  server.on("/", HTTP_GET, [&]() {
    lastActivity = millis();
    server.send(200, "text/html", buildConfigPage(s, true));
  });
  server.on("/save", HTTP_POST, [&]() {
    parseConfigForm(server, s);
    saveSettings(s);
    sendSavedAndRestart(server);
  });
  // Captive portal: everything else (generate_204, hotspot-detect, ...) -> /
  server.onNotFound([&]() {
    lastActivity = millis();
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });
  server.begin();

  while (true) {
    dns.processNextRequest();
    server.handleClient();
    if (WiFi.softAPgetStationNum() > 0) lastActivity = max(lastActivity, millis() - 1000);
    if (millis() - lastActivity > PORTAL_TIMEOUT_MS) {
      Serial.println(F("[PORTAL] Idle timeout, restarting to retry WiFi"));
      ESP.restart();
    }
    delay(5);
  }
}

#endif  // PORTAL_H
