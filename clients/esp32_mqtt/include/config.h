#pragma once

// ── WiFi AP (ESP8266s connect here) ──────────────────────────────────────────
#define WIFI_AP_SSID "ESP32-MQTT-Bridge"
#define WIFI_AP_PASSWORD "12345678"
#define WIFI_AP_CHANNEL 1
#define WIFI_AP_MAX_CLIENTS 20

// ── WiFi STA (managed by WiFiManager, no hardcoded creds) ───────────────────

// ── WiFiManager ──────────────────────────────────────────────────────────────
#define WM_AP_SSID WIFI_AP_SSID
#define WM_AP_PASSWORD WIFI_AP_PASSWORD

// ── MQTT Broker ──────────────────────────────────────────────────────────────
#define MQTT_PORT 1883
#define MQTT_MAX_CLIENTS 20
// #define MQTT_USERNAME       "bridge"
// #define MQTT_PASSWORD       "bridge123"

// ── Topic Prefix ─────────────────────────────────────────────────────────────
#define TOPIC_PREFIX "mqtt-bridge"

// ── HTTP Dashboard ────────────────────────────────────────────────────────────
#define HTTP_PORT 80
#define DASHBOARD_TITLE "ESP32 MQTT Bridge"

// ── Broadcast Discovery ───────────────────────────────────────────────────────
#define BROADCAST_PORT 5000
#define BROADCAST_INTERVAL_MS 10000
#define MDNS_HOSTNAME "mqtt_bridge"

// ── Timeouts ─────────────────────────────────────────────────────────────────
#define DEVICE_TIMEOUT_MS 60000
#define CLIENT_HISTORY_MS 300000

// ── WiFi Connection Timeouts ─────────────────────────────────────────────────
#define STA_RECONNECT_INTERVAL_MS 15000 // Verifica a cada 15 segundos
#define STA_CONNECT_TIMEOUT_MS 20000    // Timeout de conexão (20 segundos)