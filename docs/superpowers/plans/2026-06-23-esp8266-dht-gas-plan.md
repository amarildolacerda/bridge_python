# ESP8266 DHT22 + MQ-2 Client Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan.

**Goal:** Create ESP8266 client combining DHT22 (temp/hum) + MQ-2 (gas) sensors registering two separate devices on the bridge.

**Architecture:** Single ESP8266 running Arduino framework, starts HTTP server + UDP listener, discovers bridge via UDP, registers two devices (`temperature` + `gas`), sends state for each independently.

**Tech Stack:** ESP8266, Arduino framework, PlatformIO, ArduinoJson 7, WiFiManager 2, DHT sensor library

## Global Constraints

- Device IDs: `esp8266_<chip_id>_temp` and `esp8266_<chip_id>_gas`
- Base ID `esp8266_<chip_id>` used for discovery UDP
- DHT22 on GPIO 4, MQ-2 analog on A0, MQ-2 digital on GPIO 16
- LED status on GPIO 2, LED alerta on GPIO 14, LED alarme on GPIO 12
- BRIDGE_HOST = "0.0.0.0" (UDP discovery only, no fallback)
- EEPROM: 128 bytes, marker 0xFF at addr 0, device name at addrs 1-48
- cJSON copy rule (not applicable — using ArduinoJson)
- Follow existing client patterns exactly (esp8266_gas, esp8266_dht21)

---
### Task 1: Project scaffold

**Files:**
- Create: `clients/esp8266_dht_gas/platformio.ini`
- Create: `clients/esp8266_dht_gas/include/config.h`

**Interfaces:**
- Consumes: nothing
- Produces: build config + pin/interval definitions used by all following tasks

- [ ] **Step 1: Create platformio.ini**

Create `clients/esp8266_dht_gas/platformio.ini`:
```ini
[env:esp8266]
platform = espressif8266
board = nodemcuv2
framework = arduino
monitor_speed = 115200
lib_deps =
    bblanchon/ArduinoJson@^7.3.1
    tzapu/WiFiManager@^2.0.0
    adafruit/DHT sensor library@^1.4.6
```

- [ ] **Step 2: Create config.h**

Create `clients/esp8266_dht_gas/include/config.h`:
```cpp
#pragma once
#include <Arduino.h>

#define DEVICE_NAME "DHT + Gas"

#define STATE_UPDATE_INTERVAL 5000
#define STATE_SEND_THRESHOLD_TEMP 0.5
#define STATE_SEND_THRESHOLD_HUM 3.0
#define STATE_SEND_THRESHOLD_GAS 5
#define STATE_FORCE_INTERVAL 300000
#define HEARTBEAT_INTERVAL 60000
#define TELEMETRY_INTERVAL 30000

#define DISCOVERY_PORT 5000
#define DISCOVERY_TIMEOUT 30000

#define BRIDGE_HOST "0.0.0.0"
#define BRIDGE_PORT 80

#define DHT_PIN 4
#define DHT_TYPE DHT22
#define GAS_ANALOG_PIN A0
#define GAS_DIGITAL_PIN 16

#define LED_PIN 2
#define GAS_LED_ALERT_PIN 14
#define GAS_LED_ALARM_PIN 12

#define GAS_ALERT_THRESHOLD 15
#define GAS_ALARM_THRESHOLD 30
#define NORMAL_PULSE_MS 500
#define ALARM_BLINK_FAST_MS 200
#define ALARM_BLINK_SLOW_MS 1000
#define LED_BLINK_WIFI_MS 200
#define LED_BLINK_BRIDGE_MS 2000
```

---
### Task 2: Dashboard HTML

**Files:**
- Create: `clients/esp8266_dht_gas/include/pages.h`

**Interfaces:**
- Consumes: nothing (standalone HTML)
- Produces: `PAGE_DASHBOARD` PROGMEM string used by `handle_root()`

- [ ] **Step 1: Create pages.h**

Create `clients/esp8266_dht_gas/include/pages.h` with combined dashboard showing temperature, humidity, and gas level, following the dark theme pattern from existing clients:

```cpp
#pragma once

static const char PAGE_DASHBOARD[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>DHT + Gas</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:system-ui,-apple-system,sans-serif;background:#010102;color:#f7f8f8;display:flex;justify-content:center;align-items:center;min-height:100vh}
.card{background:#0f1011;border-radius:12px;padding:24px;text-align:center;max-width:360px;width:90%;border:1px solid #23252a}
h1{font-size:1.3rem;color:#5e6ad2;margin-bottom:4px}
.ip-badge{background:#141516;color:#8a8f98;font-size:.75rem;padding:3px 10px;border-radius:12px;display:inline-block;margin-bottom:12px;font-family:monospace}
.valor-sensor{font-size:2.5rem;margin:.25rem 0;color:#f7f8f8}
.label{color:#8a8f98;font-size:.85rem;margin-bottom:4px}
.valor-gas{font-size:2.5rem;margin:.25rem 0;transition:color .3s}
.valor-gas.safe{color:#27a644}
.valor-gas.warn{color:#f5a623}
.valor-gas.alarm{color:#e5484d}
.alarm-badge{display:inline-block;padding:4px 14px;border-radius:20px;font-size:.8rem;font-weight:700;margin-top:4px;margin-bottom:8px;transition:background .3s}
.alarm-badge.safe{background:rgba(39,166,68,0.15);color:#27a644}
.alarm-badge.alert{background:rgba(255,152,0,0.15);color:#f5a623}
.alarm-badge.danger{background:rgba(229,72,77,0.15);color:#e5484d}
.divider{border:none;border-top:1px solid #23252a;margin:16px 0}
.info{color:#62666d;font-size:.8rem;margin-top:16px;word-break:break-all}
</style>
</head>
<body>
<div class="card">
<h1>DHT + Gas</h1>
<div class="ip-badge" id="ipBadge">http://--.---.---.---</div>
<div class="label">Temperatura</div>
<div class="valor-sensor" id="temp">--.-°C</div>
<div class="label">Umidade</div>
<div class="valor-sensor" id="humid">--.-%</div>
<hr class="divider">
<div class="label">Nivel de Gas</div>
<div class="valor-gas safe" id="gasLevel">---%</div>
<div class="alarm-badge safe" id="alarmBadge">Seguro</div>
<div class="info" id="info"></div>
</div>
<script>
const ipEl=document.getElementById('ipBadge');
const tempEl=document.getElementById('temp');
const humEl=document.getElementById('humid');
const glEl=document.getElementById('gasLevel');
const abEl=document.getElementById('alarmBadge');
const inf=document.getElementById('info');
async function fetchState(){
try{
const r=await fetch('/api/state');const d=await r.json();
ipEl.textContent='http://'+d.ip;
tempEl.textContent=d.temperature.toFixed(1)+'\u00B0C';
humEl.textContent=d.humidity.toFixed(1)+'%';
const lvl=d.gas_level|0;
glEl.textContent=lvl+'%';
if(lvl>=30){glEl.className='valor-gas alarm';abEl.textContent='\u26A0 VAZAMENTO';abEl.className='alarm-badge danger'}
else if(lvl>=15){glEl.className='valor-gas warn';abEl.textContent='Atencao';abEl.className='alarm-badge alert'}
else{glEl.className='valor-gas safe';abEl.textContent='Seguro';abEl.className='alarm-badge safe'}
let u=d.uptime_s|0,upt=Math.floor(u/86400)+'d '+Math.floor((u%86400)/3600)+'h '+Math.floor((u%3600)/60)+'m '+u%60+'s';
let ls=d.last_send_s;let lastSend=ls==null?'nunca':ls<60?ls+'s':ls<3600?Math.floor(ls/60)+'m':Math.floor(ls/3600)+'h';
inf.innerHTML='RSSI: '+d.rssi+'dBm | Up: '+upt+'<br>Ultima coleta: '+lastSend;
}catch{glEl.textContent='\u26A0';glEl.className='valor-gas alarm';inf.textContent='Erro de conex\u00E3o'}
}
fetchState();setInterval(fetchState,3000)
</script>
</body>
</html>
)rawliteral";
```

---
### Task 3: Main firmware

**Files:**
- Create: `clients/esp8266_dht_gas/src/main.cpp`

**Interfaces:**
- Consumes: `config.h` (defines), `pages.h` (dashboard HTML)
- Produces: Complete ESP8266 firmware

- [ ] **Step 1: Write includes, tag, globals**

```cpp
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <EEPROM.h>
#include <DHT.h>
#include <ArduinoOTA.h>
#include <Updater.h>
#include "config.h"
#include "pages.h"

static const char *TAG = "esp8266-dht-gas";

static WiFiClient s_wifi;
static HTTPClient s_http;
static WiFiUDP s_udp;
static DHT *s_dht = nullptr;

static unsigned long s_last_state_update = 0;
static unsigned long s_last_telemetry_update = 0;
static unsigned long s_last_reconnect_attempt = 0;
static unsigned long s_last_broadcast_check = 0;
static unsigned long s_last_bridge_reconnect = 0;

static bool s_bridge_connected = false;

static float s_temperature = 0;
static float s_humidity = 0;
static int s_gas_level = 0;
static bool s_alarm = false;
static bool s_sensor_error = false;
static bool s_dht_valid = false;
static int s_battery = 100;
static unsigned long s_start_time = 0;
static unsigned long s_last_send_ms = 0;

static char s_device_id[32];
static char s_device_id_temp[40];
static char s_device_id_gas[40];
static char s_device_name[48] = DEVICE_NAME;

static char s_bridge_host[64] = BRIDGE_HOST;
static uint16_t s_bridge_port = BRIDGE_PORT;
static bool s_bridge_discovered = false;
static bool s_pending_register_state = false;

static int s_dht_pin = DHT_PIN;
static bool s_wifi_configuration_mode = false;
static unsigned long s_wifi_config_start_time = 0;

static ESP8266WebServer s_server(80);
```

- [ ] **Step 2: Write get_device_type_string**

```cpp
static const char *get_device_type_string(bool gas)
{
    return gas ? "gas" : "temperature";
}
```

- [ ] **Step 3: Write EEPROM functions** (identical to existing pattern)

```cpp
#define EEPROM_NAME_ADDR 0
#define EEPROM_NAME_MAX 48

static void save_device_name(const char *name)
{
    EEPROM.begin(128);
    EEPROM.write(EEPROM_NAME_ADDR, 0xFF);
    for (int i = 0; i < EEPROM_NAME_MAX - 1; i++)
    {
        EEPROM.write(EEPROM_NAME_ADDR + 1 + i, name[i]);
        if (name[i] == '\0') break;
    }
    EEPROM.write(EEPROM_NAME_ADDR + EEPROM_NAME_MAX, '\0');
    EEPROM.commit();
    EEPROM.end();
}

static bool is_valid_name(const char *s)
{
    if (!s || s[0] == '\0') return false;
    for (int i = 0; s[i]; i++)
    {
        char c = s[i];
        if (c < 32 || c > 126) return false;
    }
    return true;
}

static void load_device_name(void)
{
    EEPROM.begin(128);
    uint8_t marker = EEPROM.read(EEPROM_NAME_ADDR);
    if (marker == 0xFF)
    {
        char buf[EEPROM_NAME_MAX];
        for (int i = 0; i < EEPROM_NAME_MAX - 1; i++)
        {
            buf[i] = EEPROM.read(EEPROM_NAME_ADDR + 1 + i);
            if (buf[i] == '\0') break;
        }
        buf[EEPROM_NAME_MAX - 1] = '\0';
        if (is_valid_name(buf))
        {
            strncpy(s_device_name, buf, sizeof(s_device_name) - 1);
            s_device_name[sizeof(s_device_name) - 1] = '\0';
        }
    }
    EEPROM.end();
}
```

- [ ] **Step 4: Write http_post**

```cpp
static bool http_post(const char *path, const String &body)
{
    s_http.begin(s_wifi, String("http://") + s_bridge_host + ":" + s_bridge_port + path);
    s_http.addHeader("Content-Type", "application/json");
    s_http.setTimeout(5000);
    int code = s_http.POST(body);
    bool ok = (code == 200);
    s_bridge_connected = ok;
    if (!ok)
    {
        static unsigned long last_warn = 0;
        if (millis() - last_warn > 30000)
        {
            last_warn = millis();
            Serial.printf("[%s] POST %s -> %d\n", TAG, path, code);
        }
    }
    s_http.end();
    return ok;
}
```

- [ ] **Step 5: Write register_device (registers both devices)**

```cpp
static bool register_device(void)
{
    bool ok = true;
    // Register temperature device
    {
        String body;
        JsonDocument doc;
        doc["id"] = s_device_id_temp;
        doc["type"] = get_device_type_string(false);
        doc["name"] = s_device_name;
        doc["ip"] = WiFi.localIP().toString();
        serializeJson(doc, body);
        Serial.printf("[%s] Registering temp device: %s\n", TAG, s_device_id_temp);
        if (!http_post("/api/device/register", body))
        {
            Serial.printf("[%s] Failed to register temp device\n", TAG);
            ok = false;
        }
        else
        {
            Serial.printf("[%s] Temp device registered\n", TAG);
        }
    }
    // Register gas device
    {
        String body;
        JsonDocument doc;
        doc["id"] = s_device_id_gas;
        doc["type"] = get_device_type_string(true);
        doc["name"] = s_device_name;
        doc["ip"] = WiFi.localIP().toString();
        serializeJson(doc, body);
        Serial.printf("[%s] Registering gas device: %s\n", TAG, s_device_id_gas);
        if (!http_post("/api/device/register", body))
        {
            Serial.printf("[%s] Failed to register gas device\n", TAG);
            ok = false;
        }
        else
        {
            Serial.printf("[%s] Gas device registered\n", TAG);
        }
    }
    return ok;
}
```

- [ ] **Step 6: Write send_heartbeat (sends 2 heartbeats)**

```cpp
static void send_heartbeat(void)
{
    if (!s_bridge_discovered || !s_bridge_connected) return;

    s_http.begin(s_wifi, String("http://") + s_bridge_host + ":" + s_bridge_port + "/api/device/heartbeat");
    s_http.addHeader("Content-Type", "application/json");
    s_http.setTimeout(3000);
    String body = "{\"id\":\"" + String(s_device_id_temp) + "\"}";
    s_http.POST(body);
    s_http.end();

    s_http.begin(s_wifi, String("http://") + s_bridge_host + ":" + s_bridge_port + "/api/device/heartbeat");
    s_http.addHeader("Content-Type", "application/json");
    body = "{\"id\":\"" + String(s_device_id_gas) + "\"}";
    s_http.POST(body);
    s_http.end();
}
```

- [ ] **Step 7: Write send_state (sends state for both devices)**

```cpp
static void send_state(bool force)
{
    if (!s_bridge_discovered || !s_bridge_connected) return;

    static float last_temp = -999;
    static float last_hum = -999;
    static int last_gas = -1;
    static bool last_alarm = false;

    // Temperature state
    {
        bool changed = force || abs(s_temperature - last_temp) > STATE_SEND_THRESHOLD_TEMP
                       || abs(s_humidity - last_hum) > STATE_SEND_THRESHOLD_HUM;
        if (changed && s_dht_valid)
        {
            String body;
            JsonDocument doc;
            doc["id"] = s_device_id_temp;
            doc["temperature"] = s_temperature;
            doc["humidity"] = s_humidity;
            serializeJson(doc, body);
            if (http_post("/api/device/state", body))
            {
                last_temp = s_temperature;
                last_hum = s_humidity;
                s_last_send_ms = millis();
                Serial.printf("[%s] temp=%.1f hum=%.1f\n", TAG, s_temperature, s_humidity);
            }
        }
    }

    // Gas state
    {
        bool changed = force || s_alarm != last_alarm
                       || abs(s_gas_level - last_gas) >= STATE_SEND_THRESHOLD_GAS;
        if (changed)
        {
            String body;
            JsonDocument doc;
            doc["id"] = s_device_id_gas;
            doc["gas_level"] = s_gas_level;
            doc["alarm"] = s_alarm;
            serializeJson(doc, body);
            if (http_post("/api/device/state", body))
            {
                last_gas = s_gas_level;
                last_alarm = s_alarm;
                s_last_send_ms = millis();
                Serial.printf("[%s] gas=%d%% alarm=%s\n", TAG, s_gas_level, s_alarm ? "SIM" : "nao");
            }
        }
    }
}
```

- [ ] **Step 8: Write maintain_bridge_discovery** (identical to existing pattern, using `s_device_id` as base ID in discovery messages)

```cpp
static void maintain_bridge_discovery(void)
{
    unsigned long now = millis();
    if (now - s_last_broadcast_check < 100)
        return;
    s_last_broadcast_check = now;

    int packet_size = s_udp.parsePacket();
    if (packet_size)
    {
        char buffer[512];
        int len = s_udp.read(buffer, sizeof(buffer) - 1);
        buffer[len] = '\0';

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, buffer);
        if (!error && doc.containsKey("service"))
        {
            if (strcmp(doc["service"], "esp-bridge") == 0)
            {
                const char *host = doc["ip_sta"];
                int port = doc["http_port"] | 0;
                if (!port) port = BRIDGE_PORT;
                if (host && strlen(host) > 0)
                {
                    if (strcmp(s_bridge_host, host) != 0 || s_bridge_port != port)
                    {
                        strcpy(s_bridge_host, host);
                        s_bridge_port = port;
                        s_bridge_discovered = true;
                        Serial.printf("[%s] Bridge discovered: http://%s:%d\n", TAG, s_bridge_host, s_bridge_port);
                    }
                }
                bool re_reg = doc["re_register"] | false;
                if (re_reg)
                {
                    Serial.printf("[%s] Re-register requested by bridge\n", TAG);
                    register_device();
                    s_pending_register_state = true;
                }
            }
        }
    }

    static unsigned long last_active_discovery = 0;
    unsigned long discovery_interval = s_bridge_discovered ? 60000 : 10000;
    if (now - last_active_discovery > discovery_interval)
    {
        bool should_discover = !s_bridge_discovered;
        if (s_bridge_discovered && !s_bridge_connected)
            should_discover = true;
        if (should_discover)
        {
            last_active_discovery = now;
            JsonDocument req;
            req["service"] = "esp-bridge";
            req["discover"] = true;
            req["id"] = s_device_id;
            String payload;
            serializeJson(req, payload);
            s_udp.beginPacket(IPAddress(255, 255, 255, 255), DISCOVERY_PORT);
            s_udp.write((const uint8_t *)payload.c_str(), payload.length());
            s_udp.endPacket();
            Serial.printf("[%s] Discovery request sent\n", TAG);
        }
    }
}
```

- [ ] **Step 9: Write discover_bridge** (identical to existing pattern, using `s_device_id`)

```cpp
static bool discover_bridge(void)
{
    JsonDocument req;
    req["service"] = "esp-bridge";
    req["discover"] = true;
    req["id"] = s_device_id;
    String payload;
    serializeJson(req, payload);

    s_udp.beginPacket(IPAddress(255, 255, 255, 255), DISCOVERY_PORT);
    s_udp.write((const uint8_t *)payload.c_str(), payload.length());
    s_udp.endPacket();
    Serial.printf("[%s] Active discovery request sent\n", TAG);

    unsigned long start = millis();
    while (millis() - start < 5000)
    {
        int packet_size = s_udp.parsePacket();
        if (packet_size)
        {
            char buffer[512];
            int len = s_udp.read(buffer, sizeof(buffer) - 1);
            buffer[len] = '\0';

            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, buffer);
            if (!error && doc.containsKey("service"))
            {
                if (strcmp(doc["service"], "esp-bridge") == 0)
                {
                    const char *host = doc["ip_sta"];
                    if (host && strlen(host) > 0)
                    {
                        strcpy(s_bridge_host, host);
                        s_bridge_port = doc["http_port"] | BRIDGE_PORT;
                        s_bridge_discovered = true;
                        Serial.printf("[%s] Bridge discovered: %s:%d\n", TAG, s_bridge_host, s_bridge_port);
                        return true;
                    }
                }
            }
        }
        delay(10);
    }

    if (strcmp(BRIDGE_HOST, "0.0.0.0") != 0)
    {
        Serial.printf("[%s] Bridge discovery timeout, using configured: %s:%d\n", TAG, s_bridge_host, s_bridge_port);
        s_bridge_discovered = true;
        return true;
    }
    Serial.printf("[%s] Bridge discovery timeout, no fallback IP configured\n", TAG);
    return false;
}
```

- [ ] **Step 10: Write wifi_setup** (identical to existing pattern)

```cpp
static bool wifi_setup(bool force_config_portal = false)
{
    WiFiManager wifiManager;
    wifiManager.setConnectTimeout(20);

    if (!force_config_portal && WiFi.SSID() != "")
    {
        wifiManager.setTimeout(180);
        wifiManager.setConnectRetries(3);
        Serial.printf("[%s] Connecting to saved WiFi: %s\n", TAG, WiFi.SSID().c_str());
        if (wifiManager.autoConnect())
        {
            Serial.printf("[%s] WiFi connected! IP: %s\n", TAG, WiFi.localIP().toString().c_str());
            s_wifi_configuration_mode = false;
            return true;
        }
        Serial.printf("[%s] Failed to connect to saved WiFi\n", TAG);
    }

    Serial.printf("[%s] Starting configuration portal...\n", TAG);
    s_wifi_configuration_mode = true;
    s_wifi_config_start_time = millis();
    wifiManager.setConfigPortalTimeout(300);

    WiFiManagerParameter custom_bridge_host("bridge_host", "Bridge IP", s_bridge_host, 64);
    WiFiManagerParameter custom_bridge_port("bridge_port", "Bridge Port", String(s_bridge_port).c_str(), 6);
    wifiManager.addParameter(&custom_bridge_host);
    wifiManager.addParameter(&custom_bridge_port);

    WiFiManagerParameter custom_dev_name("dev_name", "Device Name", s_device_name, 48);
    wifiManager.addParameter(&custom_dev_name);

    if (wifiManager.startConfigPortal("ESP8266_DHT_Gas", "password123"))
    {
        if (strlen(custom_bridge_host.getValue()) > 0)
        {
            strcpy(s_bridge_host, custom_bridge_host.getValue());
            s_bridge_port = atoi(custom_bridge_port.getValue());
            if (s_bridge_port == 0)
                s_bridge_port = BRIDGE_PORT;
        }
        if (strlen(custom_dev_name.getValue()) > 0 && strcmp(s_device_name, custom_dev_name.getValue()) != 0)
        {
            strncpy(s_device_name, custom_dev_name.getValue(), sizeof(s_device_name) - 1);
            s_device_name[sizeof(s_device_name) - 1] = '\0';
            save_device_name(s_device_name);
        }
        s_wifi_configuration_mode = false;
        return true;
    }

    Serial.printf("[%s] Configuration portal timed out\n", TAG);
    s_wifi_configuration_mode = false;
    return false;
}
```

- [ ] **Step 11: Write maintain_wifi_connection** (identical to existing pattern)

```cpp
static void maintain_wifi_connection(void)
{
    if (WiFi.status() == WL_CONNECTED)
        return;
    unsigned long now = millis();
    if (now - s_last_reconnect_attempt < 30000)
        return;
    s_last_reconnect_attempt = now;

    Serial.printf("[%s] WiFi disconnected. Reconnecting...\n", TAG);
    WiFi.begin();
    unsigned long connect_start = millis();
    while (millis() - connect_start < 15000)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.printf("[%s] Reconnected! IP: %s\n", TAG, WiFi.localIP().toString().c_str());
            return;
        }
        delay(500);
    }

    static unsigned long last_config_attempt = 0;
    if (now - last_config_attempt > 300000)
    {
        last_config_attempt = now;
        wifi_setup(true);
    }
}
```

- [ ] **Step 12: Write read_sensors** (merged DHT + gas)

```cpp
static void read_sensors(void)
{
    // DHT
    float temp = s_dht->readTemperature();
    float hum = s_dht->readHumidity();
    if (!isnan(temp) && !isnan(hum))
    {
        s_temperature = temp;
        s_humidity = hum;
        s_dht_valid = true;
    }
    else
    {
        s_dht_valid = false;
        s_temperature += (random(-10, 10) / 20.0);
        s_humidity += (random(-20, 20) / 10.0);
        if (s_temperature < 18.0) s_temperature = 18.0;
        if (s_temperature > 30.0) s_temperature = 30.0;
        if (s_humidity < 30.0) s_humidity = 30.0;
        if (s_humidity > 70.0) s_humidity = 70.0;
    }

    // Gas
    int raw = analogRead(GAS_ANALOG_PIN);
    s_sensor_error = (raw == 0 || raw == 1024);
    s_gas_level = constrain(map(raw, 0, 1024, 0, 100), 0, 100);

    bool alarm = (s_gas_level >= GAS_ALARM_THRESHOLD);
    bool alert = (s_gas_level >= GAS_ALERT_THRESHOLD && s_gas_level < GAS_ALARM_THRESHOLD);
    s_alarm = alarm || alert;

    static int counter = 0;
    counter++;
    if (counter > 100)
    {
        counter = 0;
        s_battery = max(0, s_battery - 1);
    }
}
```

- [ ] **Step 13: Write init_hardware** (includes DHT pin detection from dht21 client)

```cpp
static int detect_dht_pin(void)
{
    const int candidates[] = {DHT_PIN, 4, 12, 13, 14, 0, 2, 15};
    Serial.printf("[%s] Detectando pino do DHT22...\n", TAG);
    for (int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++)
    {
        int pin = candidates[i];
        pinMode(pin, INPUT_PULLUP);
        DHT dht(pin, DHT_TYPE);
        dht.begin();
        delay(250);
        float t = dht.readTemperature();
        float h = dht.readHumidity();
        if (!isnan(t) && !isnan(h))
        {
            Serial.printf("[%s] DHT22 encontrado no GPIO %d (T=%.1f H=%.1f)\n", TAG, pin, t, h);
            return pin;
        }
    }
    Serial.printf("[%s] DHT22 nao encontrado, usando GPIO %d\n", TAG, DHT_PIN);
    return DHT_PIN;
}

static void init_hardware(void)
{
    s_dht_pin = detect_dht_pin();
    s_dht = new DHT(s_dht_pin, DHT_TYPE);
    s_dht->begin();

    pinMode(GAS_ANALOG_PIN, INPUT);

    pinMode(GAS_LED_ALERT_PIN, OUTPUT);
    digitalWrite(GAS_LED_ALERT_PIN, LOW);

    pinMode(GAS_LED_ALARM_PIN, OUTPUT);
    digitalWrite(GAS_LED_ALARM_PIN, LOW);

#ifdef LED_PIN
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
#endif
}
```

- [ ] **Step 14: Write check_config_portal_timeout**

```cpp
static void check_config_portal_timeout(void)
{
    if (s_wifi_configuration_mode && (millis() - s_wifi_config_start_time > 600000))
    {
        Serial.printf("[%s] Config portal timeout. Restarting...\n", TAG);
        ESP.restart();
    }
}
```

- [ ] **Step 15: Write handle_root and handle_api_state**

```cpp
static void handle_root(void)
{
    s_server.send(200, "text/html", FPSTR(PAGE_DASHBOARD));
}

static void handle_api_state(void)
{
    String json;
    {
        JsonDocument doc;
        doc["temperature"] = s_temperature;
        doc["humidity"] = s_humidity;
        doc["gas_level"] = s_gas_level;
        doc["alarm"] = s_alarm;
        doc["battery"] = s_battery;
        doc["device_id"] = s_device_id;
        doc["device_name"] = s_device_name;
        doc["bridge_connected"] = s_bridge_connected;
        doc["ip"] = WiFi.localIP().toString();
        doc["rssi"] = WiFi.RSSI();
        doc["uptime_s"] = (millis() - s_start_time) / 1000;
        if (s_last_send_ms) doc["last_send_s"] = (millis() - s_last_send_ms) / 1000;
        serializeJson(doc, json);
    }
    s_server.send(200, "application/json", json);
}
```

- [ ] **Step 16: Write handle_serial** (combined commands from dht21 + gas)

```cpp
static void handle_serial(void)
{
    if (Serial.available() <= 0)
        return;
    char c = Serial.read();
    switch (c)
    {
    case 'R':
    case 'r':
        ESP.restart();
        break;
    case 'l':
    case 'L':
        Serial.printf("\n--- Leitura forcada ---\n");
        read_sensors();
        Serial.printf("  Temperatura: %.1f C%s\n", s_temperature, s_dht_valid ? "" : " (invalido)");
        Serial.printf("  Umidade:     %.1f %%\n", s_humidity);
        Serial.printf("  Gas:         %d %%\n", s_gas_level);
        Serial.printf("  Alarme:      %s\n", s_alarm ? "SIM - VAZAMENTO!" : "Seguro");
        Serial.printf("  DHT22:       GPIO %d\n", s_dht_pin);
        Serial.printf("  Bateria:     %d %%\n", s_battery);
        if (s_bridge_discovered && s_bridge_connected)
        {
            s_last_state_update = 0;
            send_state(true);
        }
        else
        {
            Serial.printf("  (bridge desconectado)\n");
        }
        Serial.printf("-----------------------------\n\n");
        break;
    case 'u':
    case 'U':
        Serial.printf("\n--- OTA ---\n");
        Serial.printf("  Hostname: %s.local\n", s_device_id);
        Serial.printf("  Port:     8266 (ArduinoOTA)\n");
        Serial.printf("  PlatformIO CLI:\n");
        Serial.printf("    pio run -t upload --upload-port %s.local\n", s_device_id);
        Serial.printf("  espota.py:\n");
        Serial.printf("    espota.py -i %s.local -p 8266 -f firmware.bin\n", s_device_id);
        Serial.printf("-------------\n\n");
        break;
    case 't':
    case 'T':
    {
        Serial.printf("\n--- Teste de pinos DHT22 ---\n");
        int pin = detect_dht_pin();
        if (pin >= 0)
            Serial.printf("  => DHT22 no GPIO %d\n", pin);
        else
            Serial.printf("  => DHT22 nao encontrado\n");
        Serial.printf("----------------------------\n\n");
        break;
    }
    case 'h':
    case 'H':
    case '?':
        Serial.printf("\n--- Comandos ---\n");
        Serial.printf("  l    - ler sensores agora\n");
        Serial.printf("  r    - reset\n");
        Serial.printf("  s    - status do dispositivo\n");
        Serial.printf("  t    - testar pinos do DHT22\n");
        Serial.printf("  u    - info OTA\n");
        Serial.printf("  h/?  - esta ajuda\n");
        Serial.printf("  Browser: http://%s\n", WiFi.localIP().toString().c_str());
        if (s_bridge_discovered)
            Serial.printf("  Bridge:  http://%s:%d\n", s_bridge_host, s_bridge_port);
        Serial.printf("  IP local: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("  RSSI:     %d dBm\n", WiFi.RSSI());
        Serial.printf("  Up:       %lu s\n", (millis() - s_start_time) / 1000);
        Serial.printf("----------------\n\n");
        break;
    case 's':
    case 'S':
    {
        unsigned long up = (millis() - s_start_time) / 1000;
        Serial.printf("\n--- Status ---\n");
        Serial.printf("  Dispositivo: %s\n", s_device_id);
        Serial.printf("  Nome:        %s\n", s_device_name);
        Serial.printf("  Temp ID:     %s\n", s_device_id_temp);
        Serial.printf("  Gas ID:      %s\n", s_device_id_gas);
        Serial.printf("  Temperatura: %.1f C%s\n", s_temperature, s_dht_valid ? "" : " (invalido)");
        Serial.printf("  Umidade:     %.1f %%\n", s_humidity);
        Serial.printf("  Gas:         %d %%\n", s_gas_level);
        Serial.printf("  Alarme:      %s\n", s_alarm ? "SIM" : "nao");
        Serial.printf("  Bateria:     %d %%\n", s_battery);
        Serial.printf("  Bridge:      http://%s:%d (%s)\n", s_bridge_host, s_bridge_port,
                      s_bridge_connected ? "conectado" : "desconectado");
        Serial.printf("  Browser:     http://%s\n", WiFi.localIP().toString().c_str());
        Serial.printf("  RSSI:        %d dBm\n", WiFi.RSSI());
        Serial.printf("  Uptime:      %lu s\n", up);
        Serial.printf("---------------\n\n");
        break;
    }
}
```

- [ ] **Step 17: Write handle_ota and handle_ota_upload**

```cpp
static void handle_ota(void)
{
    if (!Update.hasError())
    {
        s_server.send(200, "application/json", "{\"status\":\"ok\"}");
        delay(500);
        ESP.restart();
    }
    else
    {
        s_server.send(500, "application/json", "{\"status\":\"error\"}");
    }
}

static void handle_ota_upload(void)
{
    HTTPUpload &upload = s_server.upload();
    if (upload.status == UPLOAD_FILE_START)
    {
        Serial.printf("[%s] OTA update started: %s (%d bytes)\n", TAG, upload.filename.c_str(), upload.totalSize);
        if (!Update.begin(upload.totalSize))
            Update.printError(Serial);
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
            Update.printError(Serial);
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
        if (Update.end(true))
            Serial.printf("[%s] OTA update success: %d bytes\n", TAG, upload.totalSize);
        else
            Update.printError(Serial);
    }
}
```

- [ ] **Step 18: Write setup**

```cpp
void setup(void)
{
    Serial.begin(115200);
    delay(1000);
    s_start_time = millis();

    uint32_t chip_id = ESP.getChipId();
    snprintf(s_device_id, sizeof(s_device_id), "esp8266_%06x", chip_id);
    snprintf(s_device_id_temp, sizeof(s_device_id_temp), "esp8266_%06x_temp", chip_id);
    snprintf(s_device_id_gas, sizeof(s_device_id_gas), "esp8266_%06x_gas", chip_id);

    load_device_name();

    Serial.printf("\n");
    Serial.printf("============================================\n");
    Serial.printf("  ESP8266 DHT22 + MQ-2 v1.0\n");
    Serial.printf("  Device: %s\n", s_device_id);
    Serial.printf("  Nome:   %s\n", s_device_name);
    Serial.printf("  Temp:   %s\n", s_device_id_temp);
    Serial.printf("  Gas:    %s\n", s_device_id_gas);
    Serial.printf("============================================\n");

    randomSeed(analogRead(A0));
    init_hardware();
    Serial.printf("============================================\n");

    s_udp.begin(DISCOVERY_PORT);
    Serial.printf("[%s] UDP listener on port %d\n", TAG, DISCOVERY_PORT);

    if (!wifi_setup(false))
    {
        Serial.printf("[%s] WiFi setup failed, restarting...\n", TAG);
        delay(5000);
        ESP.restart();
    }

    s_server.on("/", handle_root);
    s_server.on("/api/state", handle_api_state);
    s_server.on("/api/ota", HTTP_POST, handle_ota, handle_ota_upload);
    s_server.begin();

    ArduinoOTA.setHostname(s_device_id);
    ArduinoOTA.onStart([]() { Serial.printf("[%s] OTA update start\n", TAG); });
    ArduinoOTA.onEnd([]() { Serial.printf("[%s] OTA update end\n", TAG); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[%s] OTA progress: %u%%\r", TAG, (progress * 100) / total);
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[%s] OTA error: %d\n", TAG, error);
    });
    ArduinoOTA.begin();
    Serial.printf("[%s] OTA ready: %s.local\n", TAG, s_device_id);

    Serial.printf("\n  => Browser: http://%s\n", WiFi.localIP().toString().c_str());
    Serial.printf("  => Terminal: 'h' comando de ajuda\n");

    if (discover_bridge())
    {
        register_device();
        s_pending_register_state = true;
    }
    else
    {
        Serial.printf("[%s] No bridge available yet, waiting for discovery\n", TAG);
    }
    Serial.printf("============================================\n");
    Serial.printf("  Pronto! Pressione 'h' para ajuda\n");
    Serial.printf("============================================\n\n");
}
```

- [ ] **Step 19: Write loop**

```cpp
void loop(void)
{
    handle_serial();
    check_config_portal_timeout();
    ArduinoOTA.handle();
    s_server.handleClient();

    if (WiFi.status() != WL_CONNECTED)
    {
        maintain_wifi_connection();
        delay(1000);
        return;
    }

    maintain_bridge_discovery();

    unsigned long now = millis();

    if (!s_bridge_connected && s_bridge_discovered && now - s_last_bridge_reconnect > 30000)
    {
        s_last_bridge_reconnect = now;
        if (register_device())
        {
            s_last_state_update = now;
            s_pending_register_state = true;
        }
    }

    if (s_pending_register_state)
    {
        s_pending_register_state = false;
        send_state(true);
    }

    if (now - s_last_telemetry_update > TELEMETRY_INTERVAL)
    {
        s_last_telemetry_update = now;
        Serial.printf("[%s] RSSI=%d dBm  up=%lus\n", TAG, WiFi.RSSI(), (millis() - s_start_time) / 1000);
        send_heartbeat();
    }

    if (now - s_last_state_update > STATE_UPDATE_INTERVAL)
    {
        s_last_state_update = now;
        read_sensors();
        send_state(false);
    }

    static unsigned long s_last_force_send = 0;
    if (now - s_last_force_send > STATE_FORCE_INTERVAL)
    {
        s_last_force_send = now;
        send_state(true);
    }

    // LED status
    #ifdef LED_PIN
    static unsigned long last_led = 0;
    if (s_wifi_configuration_mode)
    {
        digitalWrite(LED_PIN, HIGH);
    }
    else if (WiFi.status() != WL_CONNECTED)
    {
        if (now - last_led >= LED_BLINK_WIFI_MS)
        {
            last_led = now;
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        }
    }
    else if (!s_bridge_discovered)
    {
        if (now - last_led >= LED_BLINK_BRIDGE_MS)
        {
            last_led = now;
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        }
    }
    else
    {
        digitalWrite(LED_PIN, LOW);
    }
    #endif

    // Gas alert LED
    #ifdef GAS_LED_ALERT_PIN
    if (!s_sensor_error && s_gas_level < GAS_ALERT_THRESHOLD)
    {
        digitalWrite(GAS_LED_ALERT_PIN, LOW);
    }
    else if (s_gas_level >= GAS_ALERT_THRESHOLD && s_gas_level < GAS_ALARM_THRESHOLD)
    {
        static unsigned long last_alert = 0;
        if (now - last_alert >= ALARM_BLINK_SLOW_MS)
        {
            last_alert = now;
            digitalWrite(GAS_LED_ALERT_PIN, !digitalRead(GAS_LED_ALERT_PIN));
        }
    }
    else if (s_gas_level >= GAS_ALARM_THRESHOLD)
    {
        digitalWrite(GAS_LED_ALERT_PIN, HIGH);
    }
    #endif

    // Gas alarm LED
    #ifdef GAS_LED_ALARM_PIN
    static unsigned long last_alarm_led = 0;
    if (s_gas_level >= GAS_ALARM_THRESHOLD)
    {
        if (now - last_alarm_led >= ALARM_BLINK_FAST_MS)
        {
            last_alarm_led = now;
            digitalWrite(GAS_LED_ALARM_PIN, !digitalRead(GAS_LED_ALARM_PIN));
        }
    }
    else
    {
        digitalWrite(GAS_LED_ALARM_PIN, LOW);
    }
    #endif

    delay(1);
}
```

---
### Task 4: README

**Files:**
- Create: `clients/esp8266_dht_gas/README.md`

- [ ] **Step 1: Create README.md**

Create `clients/esp8266_dht_gas/README.md` with:
- Title: "ESP8266 DHT22 + MQ-2 Client"
- Hardware pin table (DHT22 GPIO4, MQ-2 analog A0, MQ-2 digital GPIO16, LED status GPIO2, LED alerta GPIO14, LED alarme GPIO12)
- Features list (temp, humidity, gas, alarm, OTA, dashboard, WiFiManager, serial commands)
- Build instructions (`pio run -t upload`)
- API local routes (`/`, `/api/state`, `/api/ota`)
- Bridge types registered: `"temperature"` and `"gas"`
- State format for each device type
- Serial commands table (l, s, r, u, h/?)
- Dependencies (ArduinoJson, WiFiManager, DHT sensor library)
