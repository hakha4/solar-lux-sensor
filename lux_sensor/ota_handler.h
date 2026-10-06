// =============================================================================
// ota_handler.h - Browser based firmware update on /update
// Upload the .bin from Arduino IDE: Sketch -> Export Compiled Binary.
// =============================================================================
#pragma once
#ifndef OTAHANDLER_H
#define OTAHANDLER_H

#include <WebServer.h>
#include <Update.h>

static const char OTA_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Firmware update</title>
<style>body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;max-width:480px;margin:auto;padding:16px;background:#f2f4f7}
.card{background:#fff;border-radius:10px;padding:16px}input,button{width:100%;padding:12px;font-size:16px;margin:8px 0;box-sizing:border-box}
button{background:#f39c12;color:#fff;border:0;border-radius:8px;font-weight:600}</style></head><body>
<h1>Firmware update</h1><div class="card">
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="firmware" accept=".bin" required>
<button type="submit">Upload &amp; flash</button></form>
<p>Do not power off the device during the update.</p></div>
<p><a href="/">&larr; Back to status</a></p></body></html>)HTML";

static void setupOTA(WebServer &server) {
  server.on("/update", HTTP_GET, [&server]() {
    server.send_P(200, "text/html", OTA_PAGE);
  });

  server.on("/update", HTTP_POST,
    // Called when the upload is finished
    [&server]() {
      bool ok = !Update.hasError();
      String h = F("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
                   "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"></head>"
                   "<body style=\"font-family:sans-serif;padding:16px\">");
      if (ok) {
        h += F("<h2>Update successful! Restarting...</h2>");
      } else {
        h += F("<h2>Update FAILED</h2><p>");
        h += Update.errorString();
        h += F("</p><p><a href=\"/update\">Try again</a></p>");
      }
      h += F("</body></html>");
      server.sendHeader("Connection", "close");
      server.send(ok ? 200 : 500, "text/html", h);
      if (ok) {
        delay(1500);
        ESP.restart();
      }
    },
    // Called for each chunk of the uploaded file
    [&server]() {
      HTTPUpload &up = server.upload();
      if (up.status == UPLOAD_FILE_START) {
        Serial.printf("[OTA] Start: %s\n", up.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
          Serial.printf("[OTA] begin failed: %s\n", Update.errorString());
        }
      } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.isRunning() && Update.write(up.buf, up.currentSize) != up.currentSize) {
          Serial.printf("[OTA] write failed: %s\n", Update.errorString());
        }
      } else if (up.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
          Serial.printf("[OTA] Success, %u bytes\n", up.totalSize);
        } else {
          Serial.printf("[OTA] end failed: %s\n", Update.errorString());
        }
      } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        Serial.println(F("[OTA] Aborted"));
      }
    });
}

#endif  // OTAHANDLER_H
