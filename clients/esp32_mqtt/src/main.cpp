#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <sMQTTBroker.h>
#include <ArduinoJson.h>
#include "config.h"
#include "dashboard.h"

// ── workaround brownout ──────────────────────────────────────────────────────
#define DISABLE_BROWNOUT
#ifdef DISABLE_BROWNOUT
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#endif

static const char *TAG = "mqtt-bridge";
IPAddress sta_ip;

// ── device registry ──────────────────────────────────────────────────────────

typedef struct
{
    char id[64];
    char type[32];
    char name[64];
    bool online;
    unsigned long last_seen;
} BridgedDevice;

static BridgedDevice s_devices[WIFI_AP_MAX_CLIENTS];
static int s_device_count = 0;

static int find_device(const char *id)
{
    for (int i = 0; i < s_device_count; i++)
    {
        if (strcmp(s_devices[i].id, id) == 0)
            return i;
    }
    return -1;
}

static void register_device(const char *id, const char *type, const char *name)
{
    int idx = find_device(id);
    if (idx >= 0)
    {
        s_devices[idx].last_seen = millis();
        s_devices[idx].online = true;
        return;
    }
    if (s_device_count >= WIFI_AP_MAX_CLIENTS)
    {
        Serial.printf("[%s] Device limit reached\n", TAG);
        return;
    }

    BridgedDevice *d = &s_devices[s_device_count++];
    strncpy(d->id, id, sizeof(d->id) - 1);
    strncpy(d->type, type, sizeof(d->type) - 1);
    strncpy(d->name, name ? name : id, sizeof(d->name) - 1);
    d->online = true;
    d->last_seen = millis();
    Serial.printf("[%s] Registered: %s (%s)\n", TAG, id, type);
}

static void mark_online(const char *id)
{
    int idx = find_device(id);
    if (idx >= 0)
    {
        s_devices[idx].last_seen = millis();
        s_devices[idx].online = true;
    }
}

static void check_timeouts()
{
    unsigned long now = millis();
    for (int i = 0; i < s_device_count; i++)
    {
        if (s_devices[i].online && (now - s_devices[i].last_seen > DEVICE_TIMEOUT_MS))
        {
            s_devices[i].online = false;
            Serial.printf("[%s] Offline: %s\n", TAG, s_devices[i].id);
        }
    }
}

// ── broker subclass ──────────────────────────────────────────────────────────

class BridgeBroker : public sMQTTBroker
{
public:
    bool onEvent(sMQTTEvent *event) override
    {
        switch (event->Type())
        {
        case NewClient_sMQTTEventType:
        {
            auto *e = (sMQTTNewClientEvent *)event;
#ifdef MQTT_USERNAME
            if (e->Login() != MQTT_USERNAME || e->Password() != MQTT_PASSWORD)
            {
                Serial.printf("[%s] Auth failed: %s\n", TAG, e->Login().c_str());
                return false;
            }
#endif
            Serial.printf("[%s] New client connected\n", TAG);
            return true;
        }
        case RemoveClient_sMQTTEventType:
            Serial.printf("[%s] Client disconnected\n", TAG);
            break;
        case Public_sMQTTEventType:
        {
            auto *e = (sMQTTPublicClientEvent *)event;
            std::string topic = e->Topic();
            std::string payload = e->Payload();

            Serial.printf("[%s] %s -> %s\n", TAG, topic.c_str(), payload.c_str());

            if (topic == (std::string(TOPIC_PREFIX) + "/register"))
            {
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, payload);
                if (err)
                    break;
                const char *id = doc["id"];
                const char *type = doc["type"];
                const char *name = doc["name"];
                if (id && type)
                    register_device(id, type, name);
                break;
            }

            std::string prefix = std::string(TOPIC_PREFIX) + "/";
            if (topic.substr(0, prefix.size()) == prefix)
            {
                std::string rest = topic.substr(prefix.size());
                auto slash = rest.find('/');
                if (slash != std::string::npos)
                {
                    std::string dev_id = rest.substr(0, slash);
                    std::string suffix = rest.substr(slash + 1);
                    if (suffix == "state")
                    {
                        mark_online(dev_id.c_str());
                    }
                }
            }
            break;
        }
        case LostConnect_sMQTTEventType:
            Serial.printf("[%s] WiFi lost, reconnecting...\n", TAG);
            WiFi.reconnect();
            break;
        case Subscribe_sMQTTEventType:
        case UnSubscribe_sMQTTEventType:
            break;
        }
        return true;
    }
};

// ── HTTP server + broadcast ───────────────────────────────────────────────────

static WebServer s_http(HTTP_PORT);
static BridgeBroker s_broker;
static WiFiUDP s_udp;
static unsigned long s_last_broadcast = 0;

static void send_broadcast()
{
    IPAddress ap_ip = WiFi.softAPIP();

    JsonDocument doc;
    doc["service"] = "mqtt-bridge";
    doc["name"] = MDNS_HOSTNAME;
    doc["mqtt_port"] = MQTT_PORT;
    doc["http_port"] = HTTP_PORT;

    doc["ip_sta"] = sta_ip.toString();
    doc["ip_ap"] = ap_ip.toString();

    String payload;
    serializeJson(doc, payload);

    s_udp.beginPacket(IPAddress(255, 255, 255, 255), BROADCAST_PORT);
    s_udp.write((const uint8_t *)payload.c_str(), payload.length());
    s_udp.endPacket();

    Serial.printf("[%s] Broadcast: %s\n", TAG, payload.c_str());
}

static int recent_client_count()
{
    unsigned long cutoff = millis() - CLIENT_HISTORY_MS;
    int count = 0;
    for (int i = 0; i < s_device_count; i++)
    {
        if (s_devices[i].last_seen >= cutoff)
            count++;
    }
    return count;
}

static void handle_api_devices()
{
    unsigned long cutoff = millis() - CLIENT_HISTORY_MS;

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < s_device_count; i++)
    {
        if (s_devices[i].last_seen < cutoff)
            continue;

        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = s_devices[i].id;
        obj["type"] = s_devices[i].type;
        obj["name"] = s_devices[i].name;
        obj["online"] = s_devices[i].online;

        unsigned long age = millis() - s_devices[i].last_seen;
        obj["last_seen_sec"] = age / 1000;
    }

    String buf;
    serializeJson(doc, buf);
    s_http.send(200, "application/json", buf);
}

static void handle_root()
{
    s_http.send(200, "text/html", DASHBOARD_HTML);
}

// ── wifi ─────────────────────────────────────────────────────────────────────

static void wifi_init_ap()
{
    Serial.printf("[%s] AP: %s\n", TAG, WIFI_AP_SSID);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, false, WIFI_AP_MAX_CLIENTS);
    Serial.printf("[%s] AP IP: %s\n", TAG, WiFi.softAPIP().toString().c_str());
}

static void wifi_init_sta()
{
    WiFi.mode(WIFI_AP_STA);

    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    wm.setTitle(DASHBOARD_TITLE);

    wm.setSTAStaticIPConfig(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));

    if (!wm.autoConnect(WM_AP_SSID, WM_AP_PASSWORD))
    {
        Serial.printf("[%s] WiFiManager failed to connect, rebooting...\n", TAG);
        delay(3000);
        ESP.restart();
    }
    sta_ip = WiFi.localIP();

    Serial.printf("[%s] STA connected: %s  http://%s \n", TAG, WiFi.localIP().toString().c_str(), WiFi.localIP().toString().c_str());
}

// ── setup / loop ─────────────────────────────────────────────────────────────

void setup()
{
#ifdef DISABLE_BROWNOUT
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
#endif

    delay(3000);
    setCpuFrequencyMhz(80);

    Serial.begin(115200);
    delay(1000);
    Serial.printf("\n[%s] ESP32 MQTT Broker v1.0\n", TAG);
    Serial.printf("[%s] CPU freq: %d MHz\n", TAG, getCpuFrequencyMhz());

    WiFi.setTxPower(WIFI_POWER_11dBm);

    wifi_init_sta();
    delay(500);
    wifi_init_ap();

    s_broker.init(MQTT_PORT);
    Serial.printf("[%s] MQTT broker on port %d\n", TAG, MQTT_PORT);

    s_http.on("/", handle_root);
    s_http.on("/api/devices", handle_api_devices);
    s_http.begin();
    Serial.printf("[%s] HTTP dashboard on port %d\n", TAG, HTTP_PORT);
    if (MDNS.begin(MDNS_HOSTNAME))
    {
        MDNS.addService("mqtt", "tcp", MQTT_PORT);
        MDNS.addService("http", "tcp", HTTP_PORT);
        Serial.printf("[%s] mDNS: %s.local\n", TAG, MDNS_HOSTNAME);
        MDNS.setInstanceName(String(DASHBOARD_TITLE));
    }

    s_udp.begin(BROADCAST_PORT);
    send_broadcast();

    Serial.printf("[%s] AP: %s | MQTT: %s:%d | WEB: http://%s | mDNS: %s.local\n",
                  TAG, WIFI_AP_SSID,
                  WiFi.softAPIP().toString().c_str(), MQTT_PORT,
                  WiFi.softAPIP().toString().c_str(), MDNS_HOSTNAME);
}

void loop()
{
    s_broker.update();
    s_http.handleClient();
    check_timeouts();

    if (millis() - s_last_broadcast > BROADCAST_INTERVAL_MS)
    {
        s_last_broadcast = millis();
        send_broadcast();
    }

    static unsigned long last_print = 0;
    if (millis() - last_print > 15000)
    {
        last_print = millis();
        if (s_device_count > 0)
        {
            Serial.printf("[%s] Devices: %d\n", TAG, s_device_count);
            for (int i = 0; i < s_device_count; i++)
            {
                Serial.printf("  %s (%s) %s\n",
                              s_devices[i].id, s_devices[i].type,
                              s_devices[i].online ? "online" : "offline");
            }
        }
    }
}
