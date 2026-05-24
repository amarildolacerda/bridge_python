#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"

static const char *TAG = "tanque";

static WiFiClient s_wifi;
static HTTPClient s_http;

static float s_dist_cm = 0;
static float s_level_pct = 0;
static const char *s_status = "unknown";
static unsigned long s_last_read = 0;
static unsigned long s_last_send = 0;
static int s_read_errors = 0;

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
        return -1;  // timeout / out of range
    }
    return duration * 0.034 / 2;
}

static float calc_level_pct(float dist_cm)
{
    if (dist_cm < 0) return -1;

    // sensor no topo do tanque medindo distancia ate a superficie da agua
    // dist_min = agua chegando no sensor (tanque cheio) ~ SENSOR_OFFSET_CM
    // dist_max = agua no fundo (tanque vazio) ~ TANK_HEIGHT_CM
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

// ── bridge com REST ──────────────────────────────────────────────────────────

static bool http_post(const char *path, const String &body)
{
    s_http.begin(s_wifi, String("http://") + BRIDGE_HOST + ":" + BRIDGE_PORT + path);
    s_http.addHeader("Content-Type", "application/json");
    int code = s_http.POST(body);
    bool ok = (code == 200);
    if (!ok) {
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
        doc["id"] = DEVICE_ID;
        doc["type"] = DEVICE_TYPE;
        doc["name"] = DEVICE_NAME;
        serializeJson(doc, body);
    }
    Serial.printf("[%s] Registering...\n", TAG);
    return http_post("/api/device/register", body);
}

static void send_state()
{
    String body;
    {
        JsonDocument doc;
        doc["id"] = DEVICE_ID;
        doc["level"] = s_level_pct;
        doc["status"] = s_status;
        doc["distance_cm"] = s_dist_cm;
        serializeJson(doc, body);
    }
    http_post("/api/device/state", body);
}

// ── wifi ─────────────────────────────────────────────────────────────────────

static void connect_wifi()
{
    Serial.printf("[%s] WiFi: %s\n", TAG, WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int tries = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if (++tries > 40) {
            Serial.printf("\n[%s] WiFi failed, restart\n", TAG);
            ESP.restart();
        }
    }
    Serial.printf("\n[%s] IP: %s\n", TAG, WiFi.localIP().toString().c_str());
}

// ── setup / loop ─────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.printf("\n[%s] Caixa D'Agua Monitor v1.0\n", TAG);

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    connect_wifi();
    register_device();
}

void loop()
{
    unsigned long now = millis();

    if (WiFi.status() != WL_CONNECTED) {
        connect_wifi();
        return;
    }

    if (now - s_last_read >= READ_INTERVAL_MS) {
        s_last_read = now;
        read_sensor();
    }

    if (now - s_last_send >= SEND_INTERVAL_MS) {
        s_last_send = now;
        send_state();
    }
}
