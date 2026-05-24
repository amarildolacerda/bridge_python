#pragma once

#define WIFI_SSID            "ESP32-MQTT-Bridge"
#define WIFI_PASSWORD        "12345678"

// Broker discovery via UDP broadcast (port 5000)
// If MQTT_BROKER_IP is empty, client will auto-discover via broadcast
#define MQTT_BROKER_IP       ""     // deixe vazio para auto-descoberta
#define MQTT_BROKER_PORT     1883
#define BROADCAST_PORT       5000
#define BROADCAST_LISTEN_MS  5000   // escuta broadcast por 5s na inicializacao

#define DEVICE_ID            "luz_sala"
#define DEVICE_TYPE          "onoff"
#define DEVICE_NAME          "Luz da Sala"

#define TOPIC_PREFIX         "espbridge"

#if strcmp(DEVICE_TYPE, "onoff") == 0
#define RELAY_PIN            4
#endif
