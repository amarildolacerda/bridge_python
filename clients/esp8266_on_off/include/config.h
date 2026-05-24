#pragma once

#include <Arduino.h>

// WiFi Credentials
#define WIFI_SSID "kcasa"
#define WIFI_PASSWORD "3938373635"

// Bridge Configuration
#define BRIDGE_HOST "192.168.1.100"
#define BRIDGE_PORT 80

// Device Identity
#define DEVICE_ID "esp8266_1"
#define DEVICE_TYPE_ONOFF 1
#define DEVICE_TYPE_TEMPERATURE 2
#define DEVICE_TYPE DEVICE_TYPE_ONOFF // escolha aqui

#define DEVICE_NAME "Luz Sala"

// Update interval in milliseconds
#define STATE_UPDATE_INTERVAL 5000
#define COMMAND_POLL_INTERVAL 2000

// Pin definitions (customize per device)
#if DEVICE_TYPE == DEVICE_TYPE_ONOFF
#define RELAY_PIN 4  // GPIO4 (D2)
#define BUTTON_PIN 5 // GPIO5 (D1)
#elif DEVICE_TYPE == DEVICE_TYPE_TEMPERATURE
#define DHT_PIN 4 // GPIO4 (D2)
#define DHT_TYPE DHT22
#endif
