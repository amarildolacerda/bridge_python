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
        Serial.printf("[%s] ✅ Online: %s\n", TAG, id);
    }
    else
    {
        Serial.printf("[%s] ⚠️ Device not found, will register: %s\n", TAG, id);
    }
}

static void ensure_device_registered(const char *id, const char *type = "auto", const char *name = nullptr)
{
    int idx = find_device(id);
    if (idx < 0)
    {
        char default_name[64];
        if (!name || strlen(name) == 0)
        {
            snprintf(default_name, sizeof(default_name), "Device_%s", id);
            name = default_name;
        }
        register_device(id, type, name);
        Serial.printf("[%s] 🔄 Auto-registered: %s (%s)\n", TAG, id, type);
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

            Serial.printf("[%s] 📨 %s -> %s\n", TAG, topic.c_str(), payload.c_str());

            if (topic == (std::string(TOPIC_PREFIX) + "/register"))
            {
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, payload);
                if (!err)
                {
                    const char *id = doc["id"];
                    const char *type = doc["type"];
                    const char *name = doc["name"];
                    if (id && type)
                    {
                        register_device(id, type, name);
                        Serial.printf("[%s] 📝 Explicit registration: %s (%s)\n", TAG, id, type);
                    }
                }
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

                    mark_online(dev_id.c_str());
                    Serial.printf("[%s] ✅ Device online: %s (via %s)\n", TAG, dev_id.c_str(), suffix.c_str());

                    const char *type = "auto";
                    const char *name = dev_id.c_str();

                    JsonDocument doc;
                    if (deserializeJson(doc, payload) == DeserializationError::Ok)
                    {
                        if (doc["type"].is<const char *>())
                            type = doc["type"];
                        if (doc["name"].is<const char *>())
                            name = doc["name"];
                    }

                    ensure_device_registered(dev_id.c_str(), type, name);
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

// ── wifi connection management ───────────────────────────────────────────────

static bool s_sta_connected = false;
static unsigned long s_last_sta_check = 0;
static bool s_ap_active = false;

static void stop_ap()
{
    if (s_ap_active)
    {
        WiFi.softAPdisconnect(true);
        s_ap_active = false;
        Serial.printf("[%s] 🔴 AP disabled (STA connected)\n", TAG);
    }
}

static void start_ap()
{
    if (!s_ap_active)
    {
        WiFi.mode(WIFI_AP);
        bool ap_started = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, false, WIFI_AP_MAX_CLIENTS);
        if (ap_started)
        {
            s_ap_active = true;
            Serial.printf("[%s] 🟢 AP enabled: %s | IP: %s\n", TAG, WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
        }
        else
        {
            Serial.printf("[%s] ❌ Failed to start AP!\n", TAG);
        }
    }
}

static void wifi_init_sta()
{
    Serial.printf("[%s] 🔍 Initializing WiFi...\n", TAG);

    WiFi.disconnect(true);
    delay(100);

    WiFi.mode(WIFI_STA);
    delay(100);

    Serial.printf("[%s] 📡 Attempting to connect with saved credentials...\n", TAG);

    bool connected = false;

    // Primeira tentativa
    WiFi.begin();

    unsigned long start = millis();
    while (millis() - start < STA_CONNECT_TIMEOUT_MS)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            connected = true;
            break;
        }
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    // Segunda tentativa se necessário
    if (!connected)
    {
        Serial.printf("[%s] ⚠️ First attempt failed, retrying...\n", TAG);
        WiFi.reconnect();

        start = millis();
        while (millis() - start < STA_CONNECT_TIMEOUT_MS)
        {
            if (WiFi.status() == WL_CONNECTED)
            {
                connected = true;
                break;
            }
            delay(250);
            Serial.print(".");
        }
        Serial.println();
    }

    // Se falhou, usa WiFiManager com timeout
    if (!connected)
    {
        Serial.printf("[%s] ⚠️ Saved credentials may be invalid\n", TAG);
        Serial.printf("[%s] 🔄 Starting WiFiManager with timeout protection...\n", TAG);

        WiFi.disconnect(true);
        delay(500);

        WiFiManager wm;
        wm.setConfigPortalTimeout(120);
        wm.setConnectTimeout(20);
        wm.setTitle(DASHBOARD_TITLE);

        if (wm.autoConnect(WM_AP_SSID, WM_AP_PASSWORD))
        {
            sta_ip = WiFi.localIP();
            s_sta_connected = true;
            Serial.printf("[%s] ✅ WiFiManager connected! IP: %s\n", TAG, sta_ip.toString().c_str());
            Serial.printf("[%s] 📡 RSSI: %d dBm\n", TAG, WiFi.RSSI());
            stop_ap();
            connected = true;
        }
        else
        {
            Serial.printf("[%s] ❌ WiFiManager failed\n", TAG);
            s_sta_connected = false;
            sta_ip = IPAddress(0, 0, 0, 0);
            s_ap_active = true;
        }
    }

    if (connected)
    {
        sta_ip = WiFi.localIP();
        s_sta_connected = true;
        Serial.printf("[%s] ✅ STA connected: %s\n", TAG, sta_ip.toString().c_str());
        Serial.printf("[%s] 📡 RSSI: %d dBm\n", TAG, WiFi.RSSI());
        stop_ap();
    }
}

static void check_sta_connection()
{
    unsigned long now = millis();
    if (now - s_last_sta_check < STA_RECONNECT_INTERVAL_MS)
        return;
    s_last_sta_check = now;

    if (WiFi.status() != WL_CONNECTED)
    {
        static int reconnect_attempts = 0;

        if (s_sta_connected)
        {
            Serial.printf("[%s] ⚠️ STA connection lost!\n", TAG);
            s_sta_connected = false;
            start_ap();
            reconnect_attempts = 0;
        }

        reconnect_attempts++;
        Serial.printf("[%s] 🔄 Reconnection attempt %d/3...\n", TAG, reconnect_attempts);

        if (reconnect_attempts >= 3)
        {
            Serial.printf("[%s] 🔴 Max attempts reached! Restarting WiFi...\n", TAG);

            WiFi.disconnect(true);
            delay(500);
            WiFi.mode(WIFI_STA);
            delay(500);

            WiFi.begin();

            unsigned long start = millis();
            bool connected = false;
            while (millis() - start < STA_CONNECT_TIMEOUT_MS)
            {
                if (WiFi.status() == WL_CONNECTED)
                {
                    connected = true;
                    break;
                }
                delay(250);
            }

            if (connected)
            {
                sta_ip = WiFi.localIP();
                s_sta_connected = true;
                Serial.printf("[%s] ✅ STA reconnected! IP: %s\n", TAG, sta_ip.toString().c_str());
                stop_ap();
                send_broadcast();
                reconnect_attempts = 0;
                return;
            }
            else
            {
                Serial.printf("[%s] 🔄 Starting WiFiManager...\n", TAG);
                WiFi.disconnect(true);
                delay(1000);

                WiFiManager wm;
                wm.setConfigPortalTimeout(120);
                wm.setTitle(DASHBOARD_TITLE);

                if (wm.autoConnect(WM_AP_SSID, WM_AP_PASSWORD))
                {
                    sta_ip = WiFi.localIP();
                    s_sta_connected = true;
                    Serial.printf("[%s] ✅ Reconfigured! IP: %s\n", TAG, sta_ip.toString().c_str());
                    stop_ap();
                    send_broadcast();
                    reconnect_attempts = 0;
                    return;
                }
                else
                {
                    Serial.printf("[%s] ❌ Portal timeout, AP active\n", TAG);
                    start_ap();
                    reconnect_attempts = 0;
                }
            }
        }
        else
        {
            WiFi.reconnect();

            unsigned long start = millis();
            while (millis() - start < STA_CONNECT_TIMEOUT_MS)
            {
                if (WiFi.status() == WL_CONNECTED)
                {
                    sta_ip = WiFi.localIP();
                    s_sta_connected = true;
                    Serial.printf("[%s] ✅ STA reconnected! IP: %s\n", TAG, sta_ip.toString().c_str());
                    stop_ap();
                    send_broadcast();
                    reconnect_attempts = 0;
                    return;
                }
                delay(100);
            }

            Serial.printf("[%s] ⚠️ Attempt %d failed\n", TAG, reconnect_attempts);
        }
    }
    else if (!s_sta_connected)
    {
        s_sta_connected = true;
        sta_ip = WiFi.localIP();
        Serial.printf("[%s] ✅ STA reconnected! IP: %s\n", TAG, sta_ip.toString().c_str());
        stop_ap();
    }
}

static void wifi_setup_mode()
{
    if (s_sta_connected && WiFi.status() == WL_CONNECTED)
    {
        WiFi.mode(WIFI_STA);
        Serial.printf("[%s] 📡 Mode: STA only\n", TAG);
    }
    else
    {
        WiFi.mode(WIFI_AP);
        Serial.printf("[%s] 📡 Mode: AP only (configuration mode)\n", TAG);
        start_ap();
    }
}

// ── setup / loop ─────────────────────────────────────────────────────────────

void setup()
{
#ifdef DISABLE_BROWNOUT
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    delay(3000);
    setCpuFrequencyMhz(80);
#endif

    Serial.begin(115200);
    delay(1000);
    Serial.printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    Serial.printf("[%s] 🚀 ESP32 MQTT Bridge Broker v3.0\n", TAG);
    Serial.printf("[%s] ⚡ CPU freq: %d MHz\n", TAG, getCpuFrequencyMhz());
    Serial.printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    WiFi.setTxPower(WIFI_POWER_11dBm);

    wifi_init_sta();
    delay(500);

    wifi_setup_mode();

    s_broker.init(MQTT_PORT);
    Serial.printf("[%s] 🔌 MQTT broker on port %d\n", TAG, MQTT_PORT);

    s_http.on("/", handle_root);
    s_http.on("/api/devices", handle_api_devices);
    s_http.begin();

    if (s_sta_connected)
    {
        Serial.printf("[%s] 🌐 HTTP dashboard: http://%s:%d\n", TAG, sta_ip.toString().c_str(), HTTP_PORT);
    }
    if (s_ap_active)
    {
        Serial.printf("[%s] 🌐 HTTP dashboard: http://%s:%d\n", TAG, WiFi.softAPIP().toString().c_str(), HTTP_PORT);
    }

    if (s_sta_connected)
    {
        if (MDNS.begin(MDNS_HOSTNAME))
        {
            MDNS.addService("mqtt", "tcp", MQTT_PORT);
            MDNS.addService("http", "tcp", HTTP_PORT);
            Serial.printf("[%s] 📡 mDNS: %s.local\n", TAG, MDNS_HOSTNAME);
            MDNS.setInstanceName(String(DASHBOARD_TITLE));
        }
    }

    s_udp.begin(BROADCAST_PORT);
    send_broadcast();

    Serial.printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    Serial.printf("[%s] ✅ System Ready!\n", TAG);
    if (s_sta_connected)
    {
        Serial.printf("[%s] 🌍 STA IP: %s\n", TAG, sta_ip.toString().c_str());
        Serial.printf("[%s] 🔌 MQTT: %s:%d\n", TAG, sta_ip.toString().c_str(), MQTT_PORT);
        Serial.printf("[%s] 🌐 WEB: http://%s:%d\n", TAG, sta_ip.toString().c_str(), HTTP_PORT);
    }
    if (s_ap_active)
    {
        Serial.printf("[%s] 📡 AP: %s | IP: %s\n", TAG, WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
        Serial.printf("[%s] 🔌 MQTT: %s:%d (AP mode)\n", TAG, WiFi.softAPIP().toString().c_str(), MQTT_PORT);
        Serial.printf("[%s] 🌐 WEB: http://%s:%d\n", TAG, WiFi.softAPIP().toString().c_str(), HTTP_PORT);
    }
    Serial.printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

void loop()
{
    s_broker.update();
    s_http.handleClient();
    check_timeouts();
    check_sta_connection();

    if (millis() - s_last_broadcast > BROADCAST_INTERVAL_MS)
    {
        s_last_broadcast = millis();
        send_broadcast();
    }

    static unsigned long last_status_log = 0;
    if (millis() - last_status_log > 30000)
    {
        last_status_log = millis();
        Serial.printf("[%s] 📊 Status: STA=%s, AP=%s, IP=%s, Devices=%d, FreeHeap=%d\n",
                      TAG,
                      WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected",
                      s_ap_active ? "Active" : "Inactive",
                      WiFi.localIP().toString().c_str(),
                      s_device_count,
                      ESP.getFreeHeap());

        if (s_device_count > 0)
        {
            Serial.printf("[%s] 📱 Registered devices:\n", TAG);
            for (int i = 0; i < s_device_count; i++)
            {
                Serial.printf("      %d. %s (%s) - %s, last_seen: %lu ms ago\n",
                              i + 1,
                              s_devices[i].name,
                              s_devices[i].type,
                              s_devices[i].online ? "🟢 online" : "🔴 offline",
                              millis() - s_devices[i].last_seen);
            }
        }
    }
    delay(1);
}