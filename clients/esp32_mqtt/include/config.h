#pragma once

// ── WiFi AP (ESP8266s connect here) ──────────────────────────────────────────
#define WIFI_AP_SSID "ESP32-MQTT-Bridge"
#define WIFI_AP_PASSWORD "12345678"
#define WIFI_AP_CHANNEL 1
#define WIFI_AP_MAX_CLIENTS 8

// ── WiFi STA (managed by WiFiManager, no hardcoded creds) ───────────────────
// WiFiManager provides a captive portal at first boot to configure WiFi.

// ── WiFiManager ──────────────────────────────────────────────────────────────
#define WM_AP_SSID "ESP32-MQTT-Setup"
#define WM_AP_PASSWORD "config123"

// ── MQTT Broker ──────────────────────────────────────────────────────────────
#define MQTT_PORT 1883
#define MQTT_MAX_CLIENTS 10
// #define MQTT_USERNAME       "bridge"
// #define MQTT_PASSWORD       "bridge123"

// ── Topic Prefix ─────────────────────────────────────────────────────────────
#define TOPIC_PREFIX "espbridge"

// Topics:
//   espbridge/register           ─ device registration (payload: {"id":"x","type":"onoff"})
//   espbridge/{id}/state         ─ device state update
//   espbridge/{id}/command       ─ send command to device

// ── HTTP Dashboard ────────────────────────────────────────────────────────────
#define HTTP_PORT 80
#define DASHBOARD_TITLE "ESP32 MQTT Bridge"

// ── Broadcast Discovery ───────────────────────────────────────────────────────
#define BROADCAST_PORT 5000
#define BROADCAST_INTERVAL_MS 10000 // anunciar a cada 10s
#define MDNS_HOSTNAME "espbridge"

// ── Timeouts ─────────────────────────────────────────────────────────────────
#define DEVICE_TIMEOUT_MS 60000  // 60s sem resposta = offline
#define CLIENT_HISTORY_MS 300000 // 5min para mostrar no dashboard
