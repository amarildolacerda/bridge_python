#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>
#include <WiFiManager.h>
#include <EEPROM.h>
#include "config.h"

static const char *TAG = "tanque";

static WiFiClient s_wifi;
static HTTPClient s_http;
static WiFiUDP s_udp;

static float s_dist_cm = 0;
static float s_level_pct = 0;
static const char *s_status = "unknown";
static unsigned long s_last_read = 0;
static unsigned long s_last_send = 0;
static unsigned long s_last_telem = 0;
static unsigned long s_last_bridge_reconnect = 0;
static unsigned long s_last_broadcast_check = 0;
static int s_read_errors = 0;
static unsigned long s_start_time = 0;

static char s_device_id[32];
static char s_device_name[48] = DEVICE_NAME;

static char s_bridge_host[64] = BRIDGE_HOST;
static uint16_t s_bridge_port = BRIDGE_PORT;
static bool s_bridge_discovered = false;
static bool s_bridge_connected = false;

static bool s_wifi_configuration_mode = false;
static unsigned long s_wifi_config_start_time = 0;

// ── ultrasonic ───────────────────────────────────────────────────────────────

static float measure_distance_cm()
{
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    long duration = pulseIn(ECHO_PIN, HIGH, SONAR_TIMEOUT_US);

    if (duration == 0) {
        return -1;
    }
    return duration * 0.034 / 2;
}

static float calc_level_pct(float dist_cm)
{
    if (dist_cm < 0) return -1;

    float dist_min = SENSOR_OFFSET_CM;
    float dist_max = TANK_HEIGHT_CM;

    float nivel_agua_cm = dist_max - dist_cm;
    if (nivel_agua_cm < 0) nivel_agua_cm = 0;
    if (nivel_agua_cm > dist_max) nivel_agua_cm = dist_max;

    float pct = (nivel_agua_cm / dist_max) * 100;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

static const char *level_status(float pct)
{
    if (pct < 0) return "error";
    if (pct <= LEVEL_EMPTY_MAX) return "vazio";
    if (pct <= LEVEL_LOW_MAX)   return "baixo";
    if (pct <= LEVEL_HALF_MAX)  return "metade";
    if (pct <= LEVEL_HIGH_MAX)  return "alto";
    return "cheio";
}

static void read_sensor()
{
    float d = measure_distance_cm();
    if (d < 0) {
        s_read_errors++;
        Serial.printf("[%s] Sensor error (%d)\n", TAG, s_read_errors);
        if (s_read_errors > 5) {
            s_status = "error";
        }
        return;
    }
    s_read_errors = 0;
    s_dist_cm = d;
    s_level_pct = calc_level_pct(d);
    s_status = level_status(s_level_pct);

    Serial.printf("[%s] Dist: %.1fcm  Nivel: %.1f%%  Status: %s\n",
                  TAG, s_dist_cm, s_level_pct, s_status);
}

// ── EEPROM name persistence ──────────────────────────────────────────────────

#define EEPROM_NAME_ADDR 0
#define EEPROM_NAME_MAX 48

static void save_device_name(const char *name)
{
    EEPROM.begin(128);
    EEPROM.write(EEPROM_NAME_ADDR, 0xFF);
    for (int i = 0; i < EEPROM_NAME_MAX - 1; i++) {
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
    for (int i = 0; s[i]; i++) {
        char c = s[i];
        if (c < 32 || c > 126) return false;
    }
    return true;
}

static void load_device_name(void)
{
    EEPROM.begin(128);
    uint8_t marker = EEPROM.read(EEPROM_NAME_ADDR);
    if (marker == 0xFF) {
        char buf[EEPROM_NAME_MAX];
        for (int i = 0; i < EEPROM_NAME_MAX - 1; i++) {
            buf[i] = EEPROM.read(EEPROM_NAME_ADDR + 1 + i);
            if (buf[i] == '\0') break;
        }
        buf[EEPROM_NAME_MAX - 1] = '\0';
        if (is_valid_name(buf)) {
            strncpy(s_device_name, buf, sizeof(s_device_name) - 1);
            s_device_name[sizeof(s_device_name) - 1] = '\0';
        }
    }
    EEPROM.end();
}

// ── bridge com REST ──────────────────────────────────────────────────────────

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
        Serial.printf("[%s] POST %s -> %d\n", TAG, path, code);
    }
    s_http.end();
    return ok;
}

static bool register_device()
{
    String body;
    {
        JsonDocument doc;
        doc["id"] = s_device_id;
        doc["type"] = DEVICE_TYPE;
        doc["name"] = s_device_name;
        doc["ip"] = WiFi.localIP().toString();
        serializeJson(doc, body);
    }
    Serial.printf("[%s] Registering device: %s\n", TAG, s_device_id);
    return http_post("/api/device/register", body);
}

static void send_state()
{
    if (!s_bridge_discovered || !s_bridge_connected)
        return;

    String body;
    {
        JsonDocument doc;
        doc["id"] = s_device_id;
        doc["level"] = s_level_pct;
        doc["status"] = s_status;
        doc["distance_cm"] = s_dist_cm;
        serializeJson(doc, body);
    }
    http_post("/api/device/state", body);
}

static void send_heartbeat(void)
{
    if (!s_bridge_discovered || !s_bridge_connected)
        return;

    String body = "{\"id\":\"" + String(s_device_id) + "\"}";
    s_http.begin(s_wifi, String("http://") + s_bridge_host + ":" + s_bridge_port + "/api/device/heartbeat");
    s_http.addHeader("Content-Type", "application/json");
    s_http.setTimeout(3000);
    s_http.POST(body);
    s_http.end();
}

// ── bridge discovery ─────────────────────────────────────────────────────────

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
                if (!port)
                    port = BRIDGE_PORT;
                if (host && strlen(host) > 0)
                {
                    if (strcmp(s_bridge_host, host) != 0 || s_bridge_port != port)
                    {
                        strcpy(s_bridge_host, host);
                        s_bridge_port = port;
                        s_bridge_discovered = true;
                        Serial.printf("[%s] Bridge discovered: %s:%d\n", TAG, s_bridge_host, s_bridge_port);
                    }
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

// ── wifi ─────────────────────────────────────────────────────────────────────

static void maintain_wifi_connection(void)
{
    if (WiFi.status() == WL_CONNECTED)
        return;

    unsigned long now = millis();
    if (now - s_last_bridge_reconnect < 30000)
        return;
    s_last_bridge_reconnect = now;

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
}

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

    if (wifiManager.startConfigPortal("ESP8266_Tanque", "password123"))
    {
        if (strlen(custom_bridge_host.getValue()) > 0)
        {
            strcpy(s_bridge_host, custom_bridge_host.getValue());
            s_bridge_port = atoi(custom_bridge_port.getValue());
            if (s_bridge_port == 0)
                s_bridge_port = BRIDGE_PORT;
        }
        if (strlen(custom_dev_name.getValue()) > 0 && strcmp(s_device_name, custom_dev_name.getValue()) != 0) {
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

static void check_config_portal_timeout(void)
{
    if (s_wifi_configuration_mode && (millis() - s_wifi_config_start_time > 600000))
    {
        Serial.printf("[%s] Config portal timeout. Restarting...\n", TAG);
        ESP.restart();
    }
}

// ── setup / loop ─────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    delay(1000);
    s_start_time = millis();

    uint32_t chip_id = ESP.getChipId();
    snprintf(s_device_id, sizeof(s_device_id), "esp8266_%06x", chip_id);

    load_device_name();

    Serial.printf("\n[%s] Tanque Monitor v1.0\n", TAG);
    Serial.printf("[%s] Device: %s\n", TAG, s_device_id);
    Serial.printf("[%s] Name: %s\n", TAG, s_device_name);

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    s_udp.begin(DISCOVERY_PORT);

    if (!wifi_setup(false))
    {
        Serial.printf("[%s] WiFi setup failed, restarting...\n", TAG);
        delay(5000);
        ESP.restart();
    }

    if (discover_bridge())
    {
        register_device();
        send_state();
    }
    else
    {
        Serial.printf("[%s] No bridge available yet, waiting for discovery\n", TAG);
    }

    Serial.printf("[%s] Ready!\n", TAG);
}

void loop()
{
    check_config_portal_timeout();

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
            s_last_send = 0;
            send_state();
        }
    }

    if (now - s_last_read >= READ_INTERVAL_MS) {
        s_last_read = now;
        read_sensor();
    }

    if (now - s_last_send >= SEND_INTERVAL_MS) {
        s_last_send = now;
        send_state();
    }

    if (now - s_last_telem >= 60000)
    {
        s_last_telem = now;
        Serial.printf("[%s] RSSI=%d dBm  up=%lus\n", TAG, WiFi.RSSI(), (millis() - s_start_time) / 1000);
    }
}
