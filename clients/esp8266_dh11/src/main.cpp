#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <DHT.h>
#include "config.h"
#include "pages.h"

static const char *TAG = "esp8266-dht11";

static WiFiClient s_wifi;
static HTTPClient s_http;
static WiFiUDP s_udp;
static DHT s_dht(DHT_PIN, DHT_TYPE);

static unsigned long s_last_state_update = 0;
static unsigned long s_last_telemetry_update = 0;
static unsigned long s_last_reconnect_attempt = 0;
static unsigned long s_last_broadcast_check = 0;

static float s_temperature = 0;
static float s_humidity = 0;
static int s_battery = 100;
static unsigned long s_start_time = 0;

static char s_bridge_host[64] = BRIDGE_HOST;
static uint16_t s_bridge_port = BRIDGE_PORT;
static bool s_bridge_discovered = false;

static bool s_wifi_configuration_mode = false;
static unsigned long s_wifi_config_start_time = 0;

static ESP8266WebServer s_server(80);

static const char *get_device_type_string(void)
{
#if DEVICE_TYPE == DEVICE_TYPE_ONOFF
    return "onoff";
#elif DEVICE_TYPE == DEVICE_TYPE_TEMPERATURE
    return "temperature";
#elif DEVICE_TYPE == DEVICE_TYPE_CONTACT
    return "contact";
#elif DEVICE_TYPE == DEVICE_TYPE_OCCUPANCY
    return "occupancy";
#elif DEVICE_TYPE == DEVICE_TYPE_DIMMABLE
    return "dimmable";
#else
    return "temperature";
#endif
}

static bool http_post(const char *path, const String &body)
{
    s_http.begin(s_wifi, String("http://") + s_bridge_host + ":" + s_bridge_port + path);
    s_http.addHeader("Content-Type", "application/json");
    int code = s_http.POST(body);
    bool ok = (code == 200);
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

static bool register_device(void)
{
    String body;
    {
        JsonDocument doc;
        doc["id"] = DEVICE_ID;
        doc["type"] = get_device_type_string();
        doc["name"] = DEVICE_NAME;
        doc["ip"] = WiFi.localIP().toString();
        serializeJson(doc, body);
    }
    Serial.printf("[%s] Registering device: %s\n", TAG, DEVICE_ID);
    bool ok = http_post("/api/device/register", body);
    if (ok)
    {
        Serial.printf("[%s] Device registered successfully\n", TAG);
    }
    else
    {
        Serial.printf("[%s] Failed to register device\n", TAG);
    }
    return ok;
}

static void send_state(bool force_log)
{
    String body;
    {
        JsonDocument doc;
        doc["id"] = DEVICE_ID;
        doc["temperature"] = s_temperature;
        doc["humidity"] = s_humidity;
        serializeJson(doc, body);
    }

    if (http_post("/api/device/state", body))
    {
        static float last_logged_temp = -999;
        bool changed = (abs(s_temperature - last_logged_temp) > 0.1);
        if (!force_log && !changed)
            return;
        Serial.printf("[%s] temp=%.1f hum=%.1f\n", TAG, s_temperature, s_humidity);
        last_logged_temp = s_temperature;
    }
}

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
                if (strcmp(doc["service"], "esp-matter-bridge") == 0)
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
    if (!s_bridge_discovered && now - last_active_discovery > 30000)
    {
        last_active_discovery = now;
        JsonDocument req;
        req["service"] = "esp-matter-bridge";
        req["discover"] = true;
        req["id"] = DEVICE_ID;
        String payload;
        serializeJson(req, payload);
        s_udp.beginPacket(IPAddress(255, 255, 255, 255), DISCOVERY_PORT);
        s_udp.write((const uint8_t *)payload.c_str(), payload.length());
        s_udp.endPacket();
        Serial.printf("[%s] Discovery request sent\n", TAG);
    }
}

static bool discover_bridge(void)
{
    JsonDocument req;
    req["service"] = "esp-matter-bridge";
    req["discover"] = true;
    req["id"] = DEVICE_ID;
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
            if (strcmp(doc["service"], "esp-matter-bridge") == 0)
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

    Serial.printf("[%s] Bridge discovery timeout, using configured: %s:%d\n", TAG, s_bridge_host, s_bridge_port);
    return false;
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

    if (wifiManager.startConfigPortal("ESP8266_DHT11", "password123"))
    {
        if (strlen(custom_bridge_host.getValue()) > 0)
        {
            strcpy(s_bridge_host, custom_bridge_host.getValue());
            s_bridge_port = atoi(custom_bridge_port.getValue());
            if (s_bridge_port == 0)
                s_bridge_port = BRIDGE_PORT;
        }
        s_wifi_configuration_mode = false;
        return true;
    }

    Serial.printf("[%s] Configuration portal timed out\n", TAG);
    s_wifi_configuration_mode = false;
    return false;
}

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

static void read_sensors(void)
{
    static unsigned long last_sensor_read = 0;
    if (millis() - last_sensor_read > 2000)
    {
        float temp = s_dht.readTemperature();
        float hum = s_dht.readHumidity();
        if (!isnan(temp) && !isnan(hum))
        {
            s_temperature = temp;
            s_humidity = hum;
        }
        else
        {
            Serial.printf("[%s] Failed to read DHT sensor\n", TAG);
        }
        static int counter = 0;
        counter++;
        if (counter > 100)
        {
            counter = 0;
            s_battery = max(0, s_battery - 1);
        }
        last_sensor_read = millis();
    }
}

static void init_hardware(void)
{
#ifdef DHT_PIN
    s_dht.begin();
#endif
#ifdef LED_PIN
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
#endif
}

static void check_config_portal_timeout(void)
{
    if (s_wifi_configuration_mode && (millis() - s_wifi_config_start_time > 600000))
    {
        Serial.printf("[%s] Config portal timeout. Restarting...\n", TAG);
        ESP.restart();
    }
}

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
        doc["battery"] = s_battery;
        doc["device_id"] = DEVICE_ID;
        doc["device_name"] = DEVICE_NAME;
        doc["ip"] = WiFi.localIP().toString();
        doc["rssi"] = WiFi.RSSI();
        doc["uptime_s"] = (millis() - s_start_time) / 1000;
        serializeJson(doc, json);
    }
    s_server.send(200, "application/json", json);
}

void setup(void)
{
    Serial.begin(115200);
    delay(1000);
    s_start_time = millis();

    Serial.printf("\n[%s] ESP8266 DHT11 Bridge Client v1.0\n", TAG);
    Serial.printf("[%s] Device: %s (%s)\n", TAG, DEVICE_ID, get_device_type_string());

    randomSeed(analogRead(A0));
    init_hardware();

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
    s_server.begin();
    Serial.printf("[%s] Web server at http://%s\n", TAG, WiFi.localIP().toString().c_str());

    discover_bridge();
    register_device();
    send_state(true);
    Serial.printf("[%s] Ready!\n", TAG);
}

void loop(void)
{
    check_config_portal_timeout();
    s_server.handleClient();

    if (WiFi.status() != WL_CONNECTED)
    {
        maintain_wifi_connection();
        delay(1000);
        return;
    }

    maintain_bridge_discovery();

    unsigned long now = millis();

    if (now - s_last_telemetry_update > TELEMETRY_INTERVAL)
    {
        s_last_telemetry_update = now;
        Serial.printf("[%s] RSSI=%d dBm  up=%lus\n", TAG, WiFi.RSSI(), (millis() - s_start_time) / 1000);
    }

    if (now - s_last_state_update > STATE_UPDATE_INTERVAL)
    {
        s_last_state_update = now;
        read_sensors();
        send_state(true);
    }

#ifdef LED_PIN
    static unsigned long last_led = 0;
    if (s_wifi_configuration_mode) {
        digitalWrite(LED_PIN, HIGH);
    } else if (WiFi.status() != WL_CONNECTED) {
        if (now - last_led >= 200) { last_led = now; digitalWrite(LED_PIN, !digitalRead(LED_PIN)); }
    } else if (!s_bridge_discovered) {
        if (now - last_led >= 2000) { last_led = now; digitalWrite(LED_PIN, !digitalRead(LED_PIN)); }
    } else {
        digitalWrite(LED_PIN, LOW);
    }
#endif

    delay(1);
}
