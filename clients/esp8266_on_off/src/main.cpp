#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"

static const char *TAG = "esp8266-bridge-client";

static WiFiClient s_wifi_client;
static HTTPClient s_http;

static unsigned long s_last_state_update = 0;
static unsigned long s_last_command_poll = 0;

// Device state (customize per type)
static bool s_onoff_state = false;
static float s_temperature = 25.0;
static float s_humidity = 50.0;

static bool wifi_connect(void)
{
    Serial.printf("[%s] Connecting to WiFi: %s\n", TAG, WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if (++attempts > 40) {
            Serial.printf("\n[%s] WiFi connection failed\n", TAG);
            return false;
        }
    }
    Serial.printf("\n[%s] Connected, IP: %s\n", TAG, WiFi.localIP().toString().c_str());
    return true;
}

static bool http_post(const char *path, const String &body)
{
    s_http.begin(s_wifi_client, String("http://") + BRIDGE_HOST + ":" + BRIDGE_PORT + path);
    s_http.addHeader("Content-Type", "application/json");

    int code = s_http.POST(body);
    bool ok = (code == 200);
    if (!ok) {
        Serial.printf("[%s] POST %s failed: %d\n", TAG, path, code);
    }
    s_http.end();
    return ok;
}

static String http_get(const char *path)
{
    s_http.begin(s_wifi_client, String("http://") + BRIDGE_HOST + ":" + BRIDGE_PORT + path);
    int code = s_http.GET();
    String response = "";
    if (code == 200) {
        response = s_http.getString();
    } else {
        Serial.printf("[%s] GET %s failed: %d\n", TAG, path, code);
    }
    s_http.end();
    return response;
}

static bool register_device(void)
{
    String body;
    {
        JsonDocument doc;
        doc["id"] = DEVICE_ID;
        doc["type"] = DEVICE_TYPE;
        doc["name"] = DEVICE_NAME;
        serializeJson(doc, body);
    }

    Serial.printf("[%s] Registering device: %s\n", TAG, DEVICE_ID);
    bool ok = http_post("/api/device/register", body);
    if (ok) {
        Serial.printf("[%s] Device registered successfully\n", TAG);
    }
    return ok;
}

static void send_state(void)
{
    String body;
    {
        JsonDocument doc;
        doc["id"] = DEVICE_ID;

        if (strcmp(DEVICE_TYPE, "onoff") == 0) {
            doc["on"] = s_onoff_state;
        } else if (strcmp(DEVICE_TYPE, "temperature") == 0) {
            doc["temperature"] = s_temperature;
            doc["humidity"] = s_humidity;
        } else if (strcmp(DEVICE_TYPE, "contact") == 0) {
            doc["contact"] = s_onoff_state;
        } else if (strcmp(DEVICE_TYPE, "occupancy") == 0) {
            doc["occupancy"] = s_onoff_state;
        } else if (strcmp(DEVICE_TYPE, "dimmable") == 0) {
            doc["on"] = s_onoff_state;
            doc["level"] = 128;
        }

        serializeJson(doc, body);
    }

    http_post("/api/device/state", body);
}

static void poll_commands(void)
{
    String path = String("/api/device/commands?id=") + DEVICE_ID;
    String response = http_get(path.c_str());
    if (response.length() == 0) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, response);
    if (err) {
        Serial.printf("[%s] JSON parse error: %s\n", TAG, err.c_str());
        return;
    }

    JsonArray commands = doc["commands"].as<JsonArray>();
    for (JsonObject cmd : commands) {
        const char *cluster = cmd["cluster"];
        const char *command = cmd["command"];
        const char *data = cmd["data"];

        Serial.printf("[%s] Command: %s/%s = %s\n", TAG, cluster, command, data);

        if (strcmp(cluster, "onoff") == 0 && strcmp(command, "set_onoff") == 0) {
            s_onoff_state = (strcmp(data, "1") == 0 || strcmp(data, "true") == 0);
            digitalWrite(RELAY_PIN, s_onoff_state ? HIGH : LOW);
            Serial.printf("[%s] Relay: %s\n", TAG, s_onoff_state ? "ON" : "OFF");
        }
        else if (strcmp(cluster, "levelcontrol") == 0 && strcmp(command, "set_level") == 0) {
            int level = atoi(data);
            analogWrite(RELAY_PIN, map(level, 0, 254, 0, 1023));
            Serial.printf("[%s] Level: %d\n", TAG, level);
        }
    }
}

#ifdef BUTTON_PIN
static ICACHE_RAM_ATTR void button_isr(void)
{
    static unsigned long last_interrupt = 0;
    unsigned long now = millis();
    if (now - last_interrupt > 300) {
        s_onoff_state = !s_onoff_state;
        digitalWrite(RELAY_PIN, s_onoff_state ? HIGH : LOW);
        last_interrupt = now;
    }
}
#endif

static void read_sensors(void)
{
    if (strcmp(DEVICE_TYPE, "temperature") == 0) {
        // Read temperature/humidity sensor
        // Example with DHT:
        // float h = dht.readHumidity();
        // float t = dht.readTemperature();
        // if (!isnan(h)) s_humidity = h;
        // if (!isnan(t)) s_temperature = t;
        s_temperature = 25.0 + (random(-50, 50) / 10.0);
        s_humidity = 50.0 + (random(-100, 100) / 10.0);
    }
}

static void init_hardware(void)
{
    if (strcmp(DEVICE_TYPE, "onoff") == 0 || strcmp(DEVICE_TYPE, "dimmable") == 0) {
        pinMode(RELAY_PIN, OUTPUT);
        digitalWrite(RELAY_PIN, LOW);

#ifdef BUTTON_PIN
        pinMode(BUTTON_PIN, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), button_isr, FALLING);
#endif
    }
}

void setup(void)
{
    Serial.begin(115200);
    delay(1000);
    Serial.printf("\n[%s] ESP8266 Bridge Client starting...\n", TAG);
    Serial.printf("[%s] Device: %s (%s)\n", TAG, DEVICE_ID, DEVICE_TYPE);

    init_hardware();

    if (!wifi_connect()) {
        ESP.restart();
    }

    if (!register_device()) {
        Serial.printf("[%s] Device registration failed, retrying...\n", TAG);
    }
}

void loop(void)
{
    unsigned long now = millis();

    // Check WiFi connection
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[%s] WiFi disconnected, reconnecting...\n", TAG);
        wifi_connect();
        delay(3000);
        return;
    }

    // Poll for commands from bridge
    if (now - s_last_command_poll > COMMAND_POLL_INTERVAL) {
        s_last_command_poll = now;
        poll_commands();
    }

    // Read sensors
    read_sensors();

    // Send state update to bridge
    if (now - s_last_state_update > STATE_UPDATE_INTERVAL) {
        s_last_state_update = now;
        send_state();
    }
}
