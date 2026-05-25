#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <WiFiUdp.h>
#include "config.h"

static const char *TAG = "esp8266-mqtt-bridge";

static WiFiClient s_wifi_client;
static PubSubClient s_mqtt_client(s_wifi_client);
static WiFiUDP s_udp;

static unsigned long s_last_state_update = 0;
static unsigned long s_last_telemetry_update = 0;
static unsigned long s_last_reconnect_attempt = 0;
static unsigned long s_last_broadcast_check = 0;

// Device state
static bool s_onoff_state = false;
static float s_temperature = 25.0;
static float s_humidity = 50.0;
static int s_dimmer_level = 128;
static int s_battery = 100;
static unsigned long s_start_time = 0;

// MQTT Broker info
static char s_mqtt_broker[64] = "192.168.1.100";
static uint16_t s_mqtt_port = MQTT_PORT;
static bool s_mqtt_connected = false;
static bool s_bridge_discovered = false;

// WiFi state
static bool s_wifi_configuration_mode = false;
static unsigned long s_wifi_config_start_time = 0;

// Button handling
volatile bool s_button_pressed = false;
volatile unsigned long s_last_interrupt = 0;

// Web server
static ESP8266WebServer s_server(80);

// Forward declarations
static void button_isr(void);
static void send_state(void);
static void send_telemetry(void);
static void send_event(const char *event, const char *severity);
static bool send_discovery_request(void);
static void maintain_bridge_discovery(void);

// Convert device type number to string (compatible with bridge)
static const char *get_device_type_string(void)
{
#if DEVICE_TYPE == DEVICE_TYPE_ONOFF
    return "light_switch";
#elif DEVICE_TYPE == DEVICE_TYPE_TEMPERATURE
    return "temperature_sensor";
#elif DEVICE_TYPE == DEVICE_TYPE_CONTACT
    return "door_sensor";
#elif DEVICE_TYPE == DEVICE_TYPE_OCCUPANCY
    return "motion_sensor";
#elif DEVICE_TYPE == DEVICE_TYPE_DIMMABLE
    return "light_dimmer";
#else
    return "sensor";
#endif
}

// MQTT Callback for incoming commands
static void mqtt_callback(char *topic, byte *payload, unsigned int length)
{
    String message;
    for (unsigned int i = 0; i < length; i++)
    {
        message += (char)payload[i];
    }

    Serial.printf("[%s] 📨 Command received on topic: %s\n", TAG, topic);
    Serial.printf("[%s] Message: %s\n", TAG, message.c_str());

    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, message);

    if (error)
    {
        Serial.printf("[%s] ❌ JSON parse error: %s\n", TAG, error.c_str());
        return;
    }

    // Handle different command formats
    const char *action = doc["action"];

    if (action != nullptr)
    {
        // New format from ping_mqtt.py
        if (strcmp(action, "reboot") == 0)
        {
            Serial.printf("[%s] 🔄 Reboot command received\n", TAG);
            send_event("device_rebooting", "warning");
            delay(500);
            ESP.restart();
        }
        else if (strcmp(action, "set_interval") == 0)
        {
            int interval = doc["params"]["interval"];
            Serial.printf("[%s] ⏰ Set interval command: %d\n", TAG, interval);
        }
        else if (strcmp(action, "calibrate") == 0)
        {
            const char *type = doc["params"]["type"];
            Serial.printf("[%s] 🔧 Calibrate command: %s\n", TAG, type);
        }
        else if (strcmp(action, "set_onoff") == 0)
        {
            bool new_state = doc["params"]["state"];
            s_onoff_state = new_state;

#ifdef RELAY_PIN
            digitalWrite(RELAY_PIN, s_onoff_state ? HIGH : LOW);
#endif

            Serial.printf("[%s] 💡 Switch turned: %s\n", TAG, s_onoff_state ? "ON" : "OFF");
            send_state();
            send_event(s_onoff_state ? "device_turned_on" : "device_turned_off", "info");
        }
        else if (strcmp(action, "set_level") == 0)
        {
            s_dimmer_level = doc["params"]["level"];

#ifdef RELAY_PIN
            analogWriteRange(1023);
            analogWrite(RELAY_PIN, map(s_dimmer_level, 0, 254, 0, 1023));
#endif

            Serial.printf("[%s] 🎚️ Dimmer level: %d\n", TAG, s_dimmer_level);
            send_state();
        }
    }
    else
    {
        // Legacy format
        const char *command = doc["command"];
        const char *cluster = doc["cluster"];

        if (cluster != nullptr && command != nullptr)
        {
            if (strcmp(cluster, "onoff") == 0 && strcmp(command, "set_onoff") == 0)
            {
                s_onoff_state = doc["data"];

#ifdef RELAY_PIN
                digitalWrite(RELAY_PIN, s_onoff_state ? HIGH : LOW);
#endif

                Serial.printf("[%s] 💡 Relay turned: %s\n", TAG, s_onoff_state ? "ON" : "OFF");
                send_state();
            }
            else if (strcmp(cluster, "levelcontrol") == 0 && strcmp(command, "set_level") == 0)
            {
                s_dimmer_level = doc["data"];

#ifdef RELAY_PIN
                analogWriteRange(1023);
                analogWrite(RELAY_PIN, map(s_dimmer_level, 0, 254, 0, 1023));
#endif

                Serial.printf("[%s] 🎚️ Dimmer level: %d\n", TAG, s_dimmer_level);
                send_state();
            }
            else if (strcmp(cluster, "system") == 0 && strcmp(command, "restart") == 0)
            {
                Serial.printf("[%s] 🔄 Restart command received\n", TAG);
                send_event("device_rebooting", "warning");
                delay(500);
                ESP.restart();
            }
        }
    }
}

// Send device registration to bridge
static bool register_device(void)
{
    if (!s_mqtt_connected)
    {
        return false;
    }

    String topic = "mqtt-bridge/register";

    DynamicJsonDocument doc(256);
    doc["id"] = DEVICE_ID;
    doc["type"] = get_device_type_string();
    doc["name"] = DEVICE_NAME;
    doc["timestamp"] = time(nullptr);

    // Add metadata based on device type
    JsonObject metadata = doc.createNestedObject("metadata");
    metadata["firmware_version"] = "1.0.0";
    metadata["chip_id"] = ESP.getChipId();
    metadata["mac"] = WiFi.macAddress();

#ifdef RELAY_PIN
    metadata["relay_pin"] = RELAY_PIN;
#endif

    String output;
    serializeJson(doc, output);

    Serial.printf("[%s] 📝 Registering device: %s\n", TAG, DEVICE_ID);

    if (s_mqtt_client.publish(topic.c_str(), output.c_str(), true))
    {
        Serial.printf("[%s] ✅ Device registered successfully\n", TAG);
        return true;
    }
    else
    {
        Serial.printf("[%s] ❌ Failed to register device\n", TAG);
        return false;
    }
}

// Send device state to bridge
static void send_state(void)
{
    if (!s_mqtt_connected)
    {
        Serial.printf("[%s] ⚠️ Cannot send state, MQTT not connected\n", TAG);
        return;
    }

    String topic = String("mqtt-bridge/") + DEVICE_ID + "/state";

    DynamicJsonDocument doc(256);
    doc["device_id"] = DEVICE_ID;
    doc["timestamp"] = time(nullptr);

    const char *type = get_device_type_string();

    if (strcmp(type, "light_switch") == 0)
    {
        doc["state"] = s_onoff_state ? "ON" : "OFF";
        doc["power"] = s_onoff_state ? 5.2 : 0.1;
    }
    else if (strcmp(type, "light_dimmer") == 0)
    {
        doc["state"] = s_onoff_state ? "ON" : "OFF";
        doc["brightness"] = map(s_dimmer_level, 0, 254, 0, 100);
        doc["level"] = s_dimmer_level;
    }
    else if (strcmp(type, "temperature_sensor") == 0)
    {
        doc["temperature"] = s_temperature;
        doc["humidity"] = s_humidity;
        doc["battery"] = s_battery;
    }
    else if (strcmp(type, "door_sensor") == 0)
    {
        doc["open"] = s_onoff_state;
        doc["contact"] = !s_onoff_state;
    }
    else if (strcmp(type, "motion_sensor") == 0)
    {
        doc["motion"] = s_onoff_state;
        doc["occupancy"] = s_onoff_state;
        if (s_onoff_state)
        {
            doc["since"] = time(nullptr);
        }
    }

    String output;
    serializeJson(doc, output);

    if (s_mqtt_client.publish(topic.c_str(), output.c_str()))
    {
        Serial.printf("[%s] 📤 State published\n", TAG);
    }
    else
    {
        Serial.printf("[%s] ❌ Failed to publish state\n", TAG);
    }
}

// Send telemetry data to bridge
static void send_telemetry(void)
{
    if (!s_mqtt_connected)
    {
        return;
    }

    String topic = String("mqtt-bridge/") + DEVICE_ID + "/telemetry";

    DynamicJsonDocument doc(256);
    doc["device_id"] = DEVICE_ID;
    doc["timestamp"] = time(nullptr);
    doc["rssi"] = WiFi.RSSI();
    doc["uptime"] = (millis() - s_start_time) / 1000;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["free_sketch_space"] = ESP.getFreeSketchSpace();
    doc["reset_reason"] = ESP.getResetReason();

    String output;
    serializeJson(doc, output);

    if (s_mqtt_client.publish(topic.c_str(), output.c_str()))
    {
        Serial.printf("[%s] 📡 Telemetry published (RSSI: %d dBm)\n", TAG, WiFi.RSSI());
    }
}

// Send event to bridge
static void send_event(const char *event, const char *severity)
{
    if (!s_mqtt_connected)
    {
        return;
    }

    String topic = String("mqtt-bridge/") + DEVICE_ID + "/event";

    DynamicJsonDocument doc(256);
    doc["device_id"] = DEVICE_ID;
    doc["timestamp"] = time(nullptr);
    doc["event"] = event;
    doc["severity"] = severity;

    String output;
    serializeJson(doc, output);

    if (s_mqtt_client.publish(topic.c_str(), output.c_str()))
    {
        Serial.printf("[%s] ⚡ Event published: %s (%s)\n", TAG, event, severity);
    }
}

// Subscribe to command topic
static void subscribe_to_commands(void)
{
    String topic = String("mqtt-bridge/") + DEVICE_ID + "/command";

    if (s_mqtt_client.subscribe(topic.c_str()))
    {
        Serial.printf("[%s] ✅ Subscribed to: %s\n", TAG, topic.c_str());
    }
    else
    {
        Serial.printf("[%s] ❌ Failed to subscribe to: %s\n", TAG, topic.c_str());
    }
}

// Maintain bridge discovery by listening to UDP broadcasts and re-requesting periodically
static void maintain_bridge_discovery(void)
{
    unsigned long now = millis();

    if (now - s_last_broadcast_check < 100)
    {
        return;
    }
    s_last_broadcast_check = now;

    // Check for incoming UDP packets
    int packet_size = s_udp.parsePacket();
    if (packet_size)
    {
        char buffer[512];
        int len = s_udp.read(buffer, sizeof(buffer) - 1);
        buffer[len] = '\0';

        String response = String(buffer);

        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, response);

        if (!error && doc.containsKey("service"))
        {
            if (strcmp(doc["service"], "mqtt-bridge") == 0)
            {
                const char *mqtt_ip = doc["ip_sta"];
                int mqtt_port = doc["mqtt_port"];

                if (mqtt_ip != nullptr && strlen(mqtt_ip) > 0 && mqtt_port > 0)
                {
                    bool info_changed = (strcmp(s_mqtt_broker, mqtt_ip) != 0) || (s_mqtt_port != mqtt_port);

                    if (info_changed || !s_bridge_discovered)
                    {
                        strcpy(s_mqtt_broker, mqtt_ip);
                        s_mqtt_port = mqtt_port;
                        s_bridge_discovered = true;

                        Serial.printf("\n[%s] 🔄 Bridge info updated:\n", TAG);
                        Serial.printf("[%s]    IP: %s\n", TAG, s_mqtt_broker);
                        Serial.printf("[%s]    MQTT Port: %d\n", TAG, s_mqtt_port);

                        if (doc.containsKey("name"))
                            Serial.printf("[%s]    Name: %s\n", TAG, doc["name"].as<const char *>());
                        if (doc.containsKey("device_count"))
                            Serial.printf("[%s]    Devices: %d\n", TAG, doc["device_count"].as<int>());

                        if (s_mqtt_connected)
                        {
                            s_mqtt_client.disconnect();
                            s_mqtt_connected = false;
                        }
                    }
                }
            }
        }
    }

    // Periodic active re-discovery if bridge not found or MQTT keeps failing
    static unsigned long last_active_discovery = 0;
    if (!s_mqtt_connected && now - last_active_discovery > 30000)
    {
        last_active_discovery = now;
        send_discovery_request();
    }
}

static bool parse_bridge_response(const char *buffer, int len)
{
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, buffer);

    if (error)
        return false;
    if (!doc.containsKey("service") || strcmp(doc["service"], "mqtt-bridge") != 0)
        return false;

    const char *mqtt_ip = doc["ip_sta"];
    int mqtt_port = doc["mqtt_port"];

    if (!mqtt_ip || strlen(mqtt_ip) == 0 || mqtt_port <= 0)
        return false;

    strcpy(s_mqtt_broker, mqtt_ip);
    s_mqtt_port = mqtt_port;
    s_bridge_discovered = true;

    Serial.printf("\n[%s] ✅ MQTT Bridge discovered!\n", TAG);
    Serial.printf("[%s] =========================================\n", TAG);
    Serial.printf("[%s] Bridge IP: %s\n", TAG, s_mqtt_broker);
    Serial.printf("[%s] MQTT Port: %d\n", TAG, s_mqtt_port);
    if (doc.containsKey("name"))
        Serial.printf("[%s] Bridge Name: %s\n", TAG, doc["name"].as<const char *>());
    if (doc.containsKey("http_port"))
        Serial.printf("[%s] HTTP Port: %d\n", TAG, doc["http_port"].as<int>());
    if (doc.containsKey("device_count"))
        Serial.printf("[%s] Devices count: %d\n", TAG, doc["device_count"].as<int>());
    Serial.printf("[%s] =========================================\n\n", TAG);
    return true;
}

static bool send_discovery_request()
{
    DynamicJsonDocument req(128);
    req["service"] = "mqtt-bridge";
    req["discover"] = true;
    String payload;
    serializeJson(req, payload);

    s_udp.beginPacket(IPAddress(255, 255, 255, 255), DISCOVERY_PORT);
    s_udp.write((const uint8_t *)payload.c_str(), payload.length());
    s_udp.endPacket();

    Serial.printf("[%s] 🔍 Active discovery request sent\n", TAG);
    return true;
}

static bool await_discovery_response(unsigned long timeout_ms)
{
    unsigned long start = millis();
    while (millis() - start < timeout_ms)
    {
        int packet_size = s_udp.parsePacket();
        if (packet_size)
        {
            char buffer[512];
            int len = s_udp.read(buffer, sizeof(buffer) - 1);
            buffer[len] = '\0';
            if (parse_bridge_response(buffer, len))
                return true;
        }
        delay(10);
    }
    return false;
}

// UDP discovery for MQTT bridge (active request + passive listen)
static bool discover_mqtt_broker(void)
{
    // 1. Send active discovery request and wait for response
    send_discovery_request();
    if (await_discovery_response(5000))
    {
        return true;
    }

    // 2. Fall back to passive listening for periodic broadcasts
    Serial.printf("[%s] 🔍 No response to active request, listening for broadcasts on port %d...\n", TAG, DISCOVERY_PORT);

    unsigned long start_time = millis();
    int attempts = 0;

    while (millis() - start_time < (unsigned long)DISCOVERY_TIMEOUT)
    {
        int packet_size = s_udp.parsePacket();
        if (packet_size)
        {
            char buffer[512];
            int len = s_udp.read(buffer, sizeof(buffer) - 1);
            buffer[len] = '\0';
            if (parse_bridge_response(buffer, len))
                return true;
        }

        // Re-send active request every 5 seconds
        if (millis() - start_time > (attempts + 1) * 5000)
        {
            send_discovery_request();
        }

        delay(100);
        attempts++;

        if (attempts % 20 == 0)
        {
            Serial.printf("[%s] ⏳ Waiting for bridge broadcast... (%d/%d seconds)\n",
                          TAG, (millis() - start_time) / 1000, DISCOVERY_TIMEOUT / 1000);
        }
    }

    Serial.printf("[%s] ❌ MQTT bridge discovery timeout after %d ms\n",
                  TAG, DISCOVERY_TIMEOUT);
    return false;
}

// Maintain MQTT connection
static bool maintain_mqtt_connection(void)
{
    if (s_mqtt_client.connected())
    {
        s_mqtt_client.loop();
        return true;
    }

    unsigned long now = millis();
    if (now - s_last_reconnect_attempt < 5000)
    {
        return false;
    }
    s_last_reconnect_attempt = now;

    Serial.printf("[%s] 🔌 Connecting to MQTT broker %s:%d...\n", TAG, s_mqtt_broker, s_mqtt_port);

    s_mqtt_client.setServer(s_mqtt_broker, s_mqtt_port);
    s_mqtt_client.setCallback(mqtt_callback);
    s_mqtt_client.setBufferSize(1024);

    String client_id = String(DEVICE_ID) + "_" + String(ESP.getChipId());

    if (s_mqtt_client.connect(client_id.c_str()))
    {
        Serial.printf("[%s] ✅ MQTT Connected!\n", TAG);
        s_mqtt_connected = true;

        // Register device and subscribe to commands
        register_device();
        subscribe_to_commands();

        // Send initial events
        send_event("device_started", "info");
        send_telemetry();
        send_state();

        return true;
    }
    else
    {
        Serial.printf("[%s] ❌ MQTT connection failed, rc=%d\n", TAG, s_mqtt_client.state());
        s_mqtt_connected = false;
        return false;
    }
}

// WiFi Manager setup
static bool wifi_setup(bool force_config_portal = false)
{
    WiFiManager wifiManager;
    wifiManager.setConnectTimeout(20);

    if (!force_config_portal && WiFi.SSID() != "")
    {
        wifiManager.setTimeout(180);
        wifiManager.setConnectRetries(3);

        Serial.printf("[%s] 📡 Connecting to saved WiFi: %s\n", TAG, WiFi.SSID().c_str());

        if (wifiManager.autoConnect())
        {
            Serial.printf("[%s] ✅ WiFi connected! IP: %s\n", TAG, WiFi.localIP().toString().c_str());
            s_wifi_configuration_mode = false;
            return true;
        }

        Serial.printf("[%s] Failed to connect to saved WiFi\n", TAG);
    }

    Serial.printf("[%s] 🎯 Starting configuration portal...\n", TAG);
    s_wifi_configuration_mode = true;
    s_wifi_config_start_time = millis();

    wifiManager.setConfigPortalTimeout(300);

    WiFiManagerParameter custom_mqtt_broker("mqtt_broker", "MQTT Broker IP", s_mqtt_broker, 64);
    WiFiManagerParameter custom_mqtt_port("mqtt_port", "MQTT Port", String(s_mqtt_port).c_str(), 6);
    wifiManager.addParameter(&custom_mqtt_broker);
    wifiManager.addParameter(&custom_mqtt_port);

    if (wifiManager.startConfigPortal("ESP8266_MQTT_Bridge", "password123"))
    {
        if (strlen(custom_mqtt_broker.getValue()) > 0)
        {
            strcpy(s_mqtt_broker, custom_mqtt_broker.getValue());
            s_mqtt_port = atoi(custom_mqtt_port.getValue());
            Serial.printf("[%s] Manual MQTT config: %s:%d\n", TAG, s_mqtt_broker, s_mqtt_port);
        }

        s_wifi_configuration_mode = false;
        return true;
    }

    Serial.printf("[%s] Configuration portal timed out\n", TAG);
    s_wifi_configuration_mode = false;
    return false;
}

// Maintain WiFi connection
static void maintain_wifi_connection(void)
{
    if (WiFi.status() == WL_CONNECTED)
    {
        return;
    }

    unsigned long now = millis();
    if (now - s_last_reconnect_attempt < 30000)
    {
        return;
    }
    s_last_reconnect_attempt = now;

    Serial.printf("[%s] 📡 WiFi disconnected. Reconnecting...\n", TAG);
    WiFi.begin();

    unsigned long connect_start = millis();
    while (millis() - connect_start < 15000)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            IPAddress localIp = WiFi.localIP();
            Serial.printf("[%s] ✅ Reconnected! IP: %d.%d.%d.%d\n", TAG,
                          localIp[0], localIp[1], localIp[2], localIp[3]);
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

// Read sensors
static void read_sensors(void)
{
#if DEVICE_TYPE == DEVICE_TYPE_TEMPERATURE
    static unsigned long last_sensor_read = 0;
    static float last_temp = 0;

    if (millis() - last_sensor_read > 2000)
    {
        // Simulate temperature/humidity readings
        s_temperature = 22.0 + (random(-30, 30) / 10.0);
        s_humidity = 50.0 + (random(-100, 100) / 10.0);

        // Simulate battery drain slowly
        static int counter = 0;
        counter++;
        if (counter > 100)
        {
            counter = 0;
            s_battery = max(0, s_battery - 1);
        }

        // Send event on significant temperature change
        if (abs(s_temperature - last_temp) > 1.0)
        {
            send_event("temperature_changed", "info");
            last_temp = s_temperature;
        }

        last_sensor_read = millis();
    }
#endif
}

// Button interrupt handler
#ifdef BUTTON_PIN
static IRAM_ATTR void button_isr(void)
{
    unsigned long now = millis();
    if (now - s_last_interrupt > 300)
    {
        s_button_pressed = true;
        s_last_interrupt = now;
    }
}
#endif

// Initialize hardware
static void init_hardware(void)
{
#if DEVICE_TYPE == DEVICE_TYPE_ONOFF || DEVICE_TYPE == DEVICE_TYPE_DIMMABLE
#ifdef RELAY_PIN
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
#endif

#ifdef BUTTON_PIN
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), button_isr, FALLING);
#endif
#endif

#ifdef LED_PIN
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
#endif
}

// Check config portal timeout
static void check_config_portal_timeout(void)
{
    if (s_wifi_configuration_mode && (millis() - s_wifi_config_start_time > 600000))
    {
        Serial.printf("[%s] ⏰ Config portal timeout. Restarting...\n", TAG);
        ESP.restart();
    }
}

// ── Web server handlers ───────────────────────────────────────────────────────

static void handle_set_onoff(bool new_state)
{
    s_onoff_state = new_state;

#ifdef RELAY_PIN
    digitalWrite(RELAY_PIN, s_onoff_state ? HIGH : LOW);
#endif

    Serial.printf("[%s] 💡 Web/REST: %s\n", TAG, s_onoff_state ? "ON" : "OFF");

    if (s_mqtt_connected)
    {
        send_state();
        send_event(s_onoff_state ? "device_turned_on" : "device_turned_off", "info");
    }

    String json;
    {
        DynamicJsonDocument doc(128);
        doc["status"] = "ok";
        doc["state"] = s_onoff_state;
        serializeJson(doc, json);
    }
    s_server.send(200, "application/json", json);
}

static void handle_api_on(void)
{
    handle_set_onoff(true);
}

static void handle_api_off(void)
{
    handle_set_onoff(false);
}

static void handle_api_toggle(void)
{
    handle_set_onoff(!s_onoff_state);
}

static void handle_api_state(void)
{
    String json;
    {
        DynamicJsonDocument doc(256);
        doc["state"] = s_onoff_state;
        doc["device_id"] = DEVICE_ID;
        doc["device_name"] = DEVICE_NAME;
        doc["ip"] = WiFi.localIP().toString();
        doc["rssi"] = WiFi.RSSI();
        doc["uptime_s"] = (millis() - s_start_time) / 1000;
        serializeJson(doc, json);
    }
    s_server.send(200, "application/json", json);
}

static void handle_root(void)
{
    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP8266 On/Off</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;background:#1a1a2e;color:#eee;display:flex;justify-content:center;align-items:center;min-height:100vh}
.card{background:#16213e;border-radius:20px;padding:2rem;text-align:center;box-shadow:0 8px 32px rgba(0,0,0,.4);max-width:360px;width:90%}
h1{font-size:1.4rem;margin-bottom:.5rem;color:#e94560}
.status{font-size:5rem;margin:1rem 0;transition:.3s}.status.on{color:#4ecca3}.status.off{color:#666}
.label{font-size:.9rem;color:#aaa;margin-bottom:1.5rem}
.buttons{display:flex;gap:.5rem;justify-content:center;flex-wrap:wrap}
.btn{border:none;border-radius:12px;padding:.7rem 1.5rem;font-size:1rem;cursor:pointer;transition:.2s;flex:1;min-width:80px;color:#fff;font-weight:600}
.btn:active{transform:scale(.95)}
.btn-on{background:#4ecca3}.btn-on:hover{background:#3db88e}
.btn-off{background:#e94560}.btn-off:hover{background:#d63852}
.btn-toggle{background:#0f3460}.btn-toggle:hover{background:#1a4a8a}
.info{font-size:.8rem;color:#666;margin-top:1.5rem;word-break:break-all}
</style>
</head>
<body>
<div class="card">
<h1>Luz Sala</h1>
<div class="status" id="status">⟳</div>
<div class="label" id="label">carregando...</div>
<div class="buttons">
<button class="btn btn-on" onclick="setState('on')">Ligar</button>
<button class="btn btn-off" onclick="setState('off')">Desligar</button>
<button class="btn btn-toggle" onclick="setState('toggle')">Inverter</button>
</div>
<div class="info" id="info"></div>
</div>
<script>
const el=document.getElementById('status');
const lb=document.getElementById('label');
const inf=document.getElementById('info');
function update(s){el.textContent=s?'🔴':'⚪';el.className='status'+(s?' on':' off');lb.textContent=s?'LIGADO':'DESLIGADO'}
async function fetchState(){try{const r=await fetch('/api/state');const d=await r.json();update(d.state);inf.textContent='IP: '+d.ip+' | RSSI: '+d.rssi+'dBm'}catch{inf.textContent='Erro de conexão'}}
async function setState(cmd){try{await fetch('/api/'+cmd,{method:'POST'});await fetchState()}catch{inf.textContent='Erro ao enviar comando'}}
fetchState();setInterval(fetchState,3000)
</script>
</body>
</html>
)rawliteral";

    s_server.send(200, "text/html", html);
}

// ── setup ─────────────────────────────────────────────────────────────────────

void setup(void)
{
    Serial.begin(115200);
    delay(1000);
    s_start_time = millis();

    Serial.printf("\n[%s] ╔══════════════════════════════════════════════════╗\n", TAG);
    Serial.printf("[%s] ║     ESP8266 MQTT Bridge Client v1.0              ║\n", TAG);
    Serial.printf("[%s] ╠══════════════════════════════════════════════════╣\n", TAG);
    Serial.printf("[%s] ║ Device ID: %-32s ║\n", TAG, DEVICE_ID);
    Serial.printf("[%s] ║ Device Name: %-30s ║\n", TAG, DEVICE_NAME);
    Serial.printf("[%s] ║ Device Type: %-30s ║\n", TAG, get_device_type_string());
    Serial.printf("[%s] ╚══════════════════════════════════════════════════╝\n\n", TAG);

    randomSeed(analogRead(A0));
    init_hardware();

    // Setup UDP for listening on broadcast port
    s_udp.begin(DISCOVERY_PORT);
    Serial.printf("[%s] 📻 UDP listener started on port %d\n", TAG, DISCOVERY_PORT);

    // Setup WiFi
    if (!wifi_setup(false))
    {
        Serial.printf("[%s] WiFi setup failed, restarting...\n", TAG);
        delay(5000);
        ESP.restart();
    }

    // Start web server
    s_server.on("/", handle_root);
    s_server.on("/api/state", handle_api_state);
    s_server.on("/api/on", HTTP_POST, handle_api_on);
    s_server.on("/api/off", HTTP_POST, handle_api_off);
    s_server.on("/api/toggle", HTTP_POST, handle_api_toggle);
    s_server.begin();
    Serial.printf("[%s] 🌐 Web server started on http://%s\n", TAG, WiFi.localIP().toString().c_str());

    // Discover MQTT bridge by listening to broadcasts
    if (discover_mqtt_broker())
    {
        Serial.printf("[%s] ✅ Bridge discovered successfully!\n", TAG);
    }
    else
    {
        Serial.printf("[%s] ⚠️ Bridge discovery failed, using fallback config\n", TAG);
    }

    // Connect to MQTT
    maintain_mqtt_connection();

    Serial.printf("[%s] 🚀 System ready!\n\n", TAG);
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

    // Keep listening for bridge announcements
    maintain_bridge_discovery();

    // Maintain MQTT connection
    maintain_mqtt_connection();

// Handle button press
#ifdef BUTTON_PIN
    if (s_button_pressed)
    {
        s_button_pressed = false;
        s_onoff_state = !s_onoff_state;

#ifdef RELAY_PIN
        digitalWrite(RELAY_PIN, s_onoff_state ? HIGH : LOW);
#endif

        Serial.printf("[%s] 🔘 Button pressed: %s\n", TAG, s_onoff_state ? "ON" : "OFF");
        send_state();
        send_event(s_onoff_state ? "button_pressed_on" : "button_pressed_off", "info");
    }
#endif

    unsigned long now = millis();

    // Periodic telemetry
    if (now - s_last_telemetry_update > TELEMETRY_INTERVAL)
    {
        s_last_telemetry_update = now;
        send_telemetry();
    }

    // Periodic state update
    if (now - s_last_state_update > STATE_UPDATE_INTERVAL)
    {
        s_last_state_update = now;
        read_sensors();
        send_state();
    }

// Blink LED to show activity (optional)
#ifdef LED_PIN
    static unsigned long last_blink = 0;
    if (s_mqtt_connected && (now - last_blink > 1000))
    {
        last_blink = now;
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
#endif

    delay(1);
}