#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"

static const char *TAG = "mqtt";

static WiFiClient s_wifi;
static WiFiUDP s_udp;
static PubSubClient s_mqtt(s_wifi);

static char s_broker_ip[16] = "";
static bool s_onoff = false;
static unsigned long s_last_state = 0;

// ── broadcast discovery ──────────────────────────────────────────────────────

static bool discover_broker()
{
    if (strlen(MQTT_BROKER_IP) > 0)
    {
        strncpy(s_broker_ip, MQTT_BROKER_IP, sizeof(s_broker_ip) - 1);
        Serial.printf("[%s] Using static broker: %s\n", TAG, s_broker_ip);
        return true;
    }

    Serial.printf("[%s] Listening for broker broadcast on port %d...\n",
                  TAG, BROADCAST_PORT);

    s_udp.begin(BROADCAST_PORT);

    unsigned long start = millis();
    while (millis() - start < BROADCAST_LISTEN_MS)
    {
        int len = s_udp.parsePacket();
        if (len > 0)
        {
            char buf[256];
            int n = s_udp.read(buf, sizeof(buf) - 1);
            if (n > 0)
            {
                buf[n] = '\0';
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, buf);
                if (!err)
                {
                    const char *service = doc["service"];
                    if (service && strcmp(service, "mqtt-bridge") == 0)
                    {
                        const char *ip = doc["ip_sta"] | "";
                        if (strlen(ip) == 0)
                            ip = doc["ip_ap"];
                        if (strlen(ip) > 0)
                        {
                            strncpy(s_broker_ip, ip, sizeof(s_broker_ip) - 1);
                            Serial.printf("[%s] Discovered broker at %s\n",
                                          TAG, s_broker_ip);
                            s_udp.stop();
                            return true;
                        }
                    }
                }
            }
        }
        delay(10);
    }

    s_udp.stop();
    Serial.printf("[%s] Broker discovery failed\n", TAG);
    Serial.printf("[%s] Set MQTT_BROKER_IP in config.h\n", TAG);
    return false;
}

// ── helpers ──────────────────────────────────────────────────────────────────

static void make_topic(char *buf, size_t len, const char *suffix)
{
    snprintf(buf, len, "%s/%s/%s", TOPIC_PREFIX, DEVICE_ID, suffix);
}

// ── wifi ─────────────────────────────────────────────────────────────────────

static void connect_wifi()
{
    Serial.printf("[%s] WiFi: %s\n", TAG, WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int tries = 0;
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
        if (++tries > 40)
        {
            Serial.printf("\n[%s] WiFi failed, restart\n", TAG);
            ESP.restart();
        }
    }
    Serial.printf("\n[%s] IP: %s\n", TAG, WiFi.localIP().toString().c_str());
}

// ── mqtt ─────────────────────────────────────────────────────────────────────

static void on_message(char *topic, byte *payload, unsigned int len)
{
    char buf[256];
    unsigned int n = (len < sizeof(buf) - 1) ? len : sizeof(buf) - 1;
    memcpy(buf, payload, n);
    buf[n] = '\0';

    Serial.printf("[%s] Cmd %s: %s\n", TAG, topic, buf);

    JsonDocument doc;
    if (deserializeJson(doc, buf))
        return;

    const char *cmd = doc["command"];
    if (!cmd)
        return;

    if (strcmp(cmd, "on") == 0)
    {
        s_onoff = true;
        digitalWrite(RELAY_PIN, HIGH);
    }
    else if (strcmp(cmd, "off") == 0)
    {
        s_onoff = false;
        digitalWrite(RELAY_PIN, LOW);
    }
}

static void connect_mqtt()
{
    char topic_cmd[64];
    make_topic(topic_cmd, sizeof(topic_cmd), "command");

    while (!s_mqtt.connected())
    {
        Serial.printf("[%s] MQTT %s:%d...\n", TAG, s_broker_ip, MQTT_BROKER_PORT);

        String id = String(DEVICE_ID) + "_" + String(random(0xFFFF), HEX);
        if (s_mqtt.connect(id.c_str()))
        {
            Serial.printf("[%s] Connected\n", TAG);
            s_mqtt.subscribe(topic_cmd);
            Serial.printf("[%s] Subscribed: %s\n", TAG, topic_cmd);
        }
        else
        {
            Serial.printf("[%s] Failed rc=%d\n", TAG, s_mqtt.state());
            delay(3000);
        }
    }
}

static void register_device()
{
    JsonDocument doc;
    doc["id"] = DEVICE_ID;
    doc["type"] = DEVICE_TYPE;
    doc["name"] = DEVICE_NAME;
    char buf[128];
    serializeJson(doc, buf, sizeof(buf));
    s_mqtt.publish(TOPIC_PREFIX "/register", buf, false);
    Serial.printf("[%s] Registered: %s\n", TAG, buf);
}

static void send_state()
{
    char topic[64];
    make_topic(topic, sizeof(topic), "state");

    JsonDocument doc;
    doc["on"] = s_onoff;
    char buf[128];
    serializeJson(doc, buf, sizeof(buf));
    s_mqtt.publish(topic, buf, false);
}

// ── setup / loop ─────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.printf("\n[%s] ESP8266 MQTT v1.0\n", TAG);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);

    connect_wifi();

    if (!discover_broker())
    {
        Serial.printf("[%s] No broker found, restarting...\n", TAG);
        delay(5000);
        ESP.restart();
    }

    s_mqtt.setServer(s_broker_ip, MQTT_BROKER_PORT);
    s_mqtt.setCallback(on_message);
    connect_mqtt();
    register_device();
}

void loop()
{
    if (WiFi.status() != WL_CONNECTED)
        connect_wifi();
    if (!s_mqtt.connected())
        connect_mqtt();
    s_mqtt.loop();

    if (millis() - s_last_state > 5000)
    {
        s_last_state = millis();
        send_state();
    }
}
