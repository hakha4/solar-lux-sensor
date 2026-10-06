# ☀️ solar-lux-sensor

ESP32-baserad ljussensor (lux) för solprognoser. En enhet per väderstreck (t.ex. **öst** och **syd**) mäter ljusstyrkan och publicerar JSON via MQTT. Datan kan sedan användas av t.ex. [solar-predictor](https://github.com/hakha4/solar-predictor) / Gen24-Shadow.

**Funktioner**
- Stöd för **TSL2591** (auto-gain 1x → 9876x) och **VEML7700** (auto-lux) i **samma kodbas** – sensorn väljs på webbsidan, ingen omkompilering behövs. Hittas inte den valda sensorn provas den andra automatiskt.
- WiFi med statisk IP eller DHCP
- MQTT med LWT (`online`/`offline`), robust återanslutning med exponentiell backoff
- NTP-tidsstämpel (CET/CEST med automatisk sommartid)
- Captive portal för förstagångskonfiguration
- Webbaserad konfiguration (`/config`) av **alla** inställningar: WiFi, statisk IP, MQTT-uppgifter, **MQTT-topikprefix**, plats och sensortyp
- OTA-uppdatering via webbläsaren (`/update`)
- Statussida (`/`) och JSON-endpoint (`/json`)

## Projektstruktur

```
solar-lux-sensor/
├── README.md
└── lux_sensor/
    ├── lux_sensor.ino   – huvudsketch (sensorer, WiFi, MQTT, NTP, webbserver)
    ├── config.h         – kompileringstidsstandardvärden
    ├── settings.h       – inställningsstruktur + lagring i NVS
    ├── portal.h         – captive portal + konfigurationsformulär
    └── ota_handler.h    – firmwareuppdatering via webbläsare
```

Lokal mapp (Windows): `C:\Projects\lux-sensor`

```bat
cd C:\Projects
git clone https://github.com/hakha4/solar-lux-sensor.git lux-sensor
```

Öppna sedan `C:\Projects\lux-sensor\lux_sensor\lux_sensor.ino` i Arduino IDE.

## Kopplingsschema

Båda sensorerna använder I2C och 3,3 V. Bara **en** sensor behöver kopplas in (de har olika I2C-adresser: TSL2591 = `0x29`, VEML7700 = `0x10`).

| Sensor-pinne | ESP32 (klassisk) | ESP32-C3 |
|--------------|------------------|----------|
| VIN / VCC    | 3V3              | 3V3      |
| GND          | GND              | GND      |
| SDA          | GPIO21           | GPIO8    |
| SCL          | GPIO22           | GPIO9    |

> ⚠️ ESP32-C3 har ingen GPIO22, därför används GPIO8/9 automatiskt när kortet är **ESP32C3 Dev Module**. Pinnarna kan ändras i `config.h` (`I2C_SDA_PIN`, `I2C_SCL_PIN`).

```
TSL2591 / VEML7700          ESP32-C3
   VIN  ────────────────────  3V3
   GND  ────────────────────  GND
   SDA  ────────────────────  GPIO8   (GPIO21 på klassisk ESP32)
   SCL  ────────────────────  GPIO9   (GPIO22 på klassisk ESP32)
```

**Placering:** montera sensorn vänd mot respektive väderstreck, skyddad mot regn (t.ex. bakom en klar akryl-/glaskupol). Tänk på att TSL2591 kan mättas i direkt sol – den kan behöva en diffusor (t.ex. vit PTFE/opalakryl). VEML7700 klarar upp till ca 120 000 lux.

## Arduino IDE – installation

1. **Board:** Installera *esp32 by Espressif Systems* (≥ 3.x) via Boards Manager. Välj **ESP32C3 Dev Module**.
   Aktivera *USB CDC On Boot: Enabled* om du vill se seriell logg via USB på C3.
2. **Partition Scheme:** standard (*Default 4MB with spiffs*) – krävs för OTA.
3. **Bibliotek** (Library Manager):
   - PubSubClient by Nick O'Leary
   - Adafruit TSL2591 Library
   - Adafruit VEML7700 Library
   - Adafruit Unified Sensor
   - Adafruit BusIO
   - ArduinoJson by Benoit Blanchon (**version 7.x**)

   `WiFi`, `WebServer`, `DNSServer`, `Preferences` och `Update` ingår i ESP32-kärnan.

## Konfiguration

### 1. Standardvärden i `config.h`
Ersätt alla `xxx`-platshållare: WiFi-SSID/lösenord, IP-adresser, MQTT-broker, användare/lösenord. Sätt `DEFAULT_LOCATION` (`"east"`/`"south"`) och `DEFAULT_SENSOR` (`0` = TSL2591, `1` = VEML7700).
Dessa värden används bara vid första start – därefter gäller det som sparats i NVS.

> Tips: ge varje enhet en **egen statisk IP** (t.ex. `.50` öst, `.51` syd).

### 2. Ändra inställningar under drift
Gå till `http://<enhetens-ip>/config`. Där kan du ändra allt:
WiFi, statisk IP/DHCP, MQTT-IP/port/användare/lösenord, **topikprefix**, plats och sensortyp. Klicka *Save & restart*.

### 3. Captive portal
Om enheten inte kan ansluta till WiFi inom 15 s startar den ett öppet nätverk **`LuxSensor-XXXXXX`**. Anslut med mobilen – konfigurationssidan öppnas automatiskt (annars gå till **http://192.168.4.1**). Efter 5 minuter utan aktivitet startar enheten om och provar WiFi igen (bra efter strömavbrott när routern startar långsamt).

## MQTT

| Topik | Innehåll |
|-------|----------|
| `{prefix}/{location}/lux` | JSON-mätvärde var 30:e sekund |
| `{prefix}/{location}/lwt` | `online` / `offline` (retained, Last Will) |

Med standardprefix `solar`: `solar/east/lux`, `solar/south/lux`, `solar/east/lwt` …

Exempel på payload:

```json
{
  "lux": 1234.56,
  "full": 5302,
  "ir": 118,
  "gain_x": 25,
  "sensor": "TSL2591",
  "location": "east",
  "timestamp": "2026-06-01T12:30:00",
  "epoch": 1780309800,
  "uptime_s": 3600,
  "rssi": -65,
  "wifi_ok": true,
  "mqtt_ok": true
}
```

`full`, `ir` och `gain_x` är `0` för VEML7700 (ej tillämpligt). `timestamp` är lokal tid (CET/CEST), `epoch` är UTC-sekunder (0 om NTP inte synkats).

Testa med: `mosquitto_sub -h <broker> -u <user> -P <pass> -t "solar/#" -v`

## Webbgränssnitt

| URL | Funktion |
|-----|----------|
| `http://<enhetens-ip>/` | Statussida (lux, sensor, WiFi/MQTT, IP, drifttid) |
| `http://<enhetens-ip>/config` | Alla inställningar |
| `http://<enhetens-ip>/update` | OTA-firmwareuppdatering |
| `http://<enhetens-ip>/json` | Senaste mätvärdet som JSON |

### OTA-uppdatering
1. Arduino IDE → *Sketch → Export Compiled Binary*.
2. Öppna `http://<enhetens-ip>/update`, välj `lux_sensor.ino.bin` och klicka *Upload & flash*.
3. Enheten startar om automatiskt. Inställningarna i NVS behålls.

> 🔒 `/config` och `/update` saknar lösenord – använd bara enheten på ett betrott lokalt nätverk.

## Byta sensor
Koppla in den andra sensorn, gå till `/config` och välj sensortyp. Glömmer du att byta i inställningarna hittar firmware ändå den inkopplade sensorn automatiskt (en varning loggas).
