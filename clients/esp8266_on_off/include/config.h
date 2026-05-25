// config.h
#pragma once

#include <Arduino.h>

// Device Identity
#define DEVICE_ID "esp8266_living_room"
#define DEVICE_NAME "Luz Sala"

// Device type (choose one)
#define DEVICE_TYPE_ONOFF 1
#define DEVICE_TYPE_TEMPERATURE 2
#define DEVICE_TYPE_CONTACT 3
#define DEVICE_TYPE_OCCUPANCY 4
#define DEVICE_TYPE_DIMMABLE 5

// Selecionar o tipo de dispositivo
#define DEVICE_TYPE DEVICE_TYPE_ONOFF
// #define DEVICE_TYPE DEVICE_TYPE_TEMPERATURE
// #define DEVICE_TYPE DEVICE_TYPE_CONTACT
// #define DEVICE_TYPE DEVICE_TYPE_OCCUPANCY
// #define DEVICE_TYPE DEVICE_TYPE_DIMMABLE

// Timing (milliseconds)
#define STATE_UPDATE_INTERVAL 5000 // Send state every 5 seconds
#define TELEMETRY_INTERVAL 30000   // Send telemetry every 30 seconds
#define COMMAND_POLL_INTERVAL 100

// UDP Discovery
#define DISCOVERY_PORT 5000     // Must match bridge broadcast port
#define DISCOVERY_TIMEOUT 30000 // 30 seconds timeout

// MQTT Configuration (fallback if discovery fails)
#define MQTT_PORT 1883

// Pin definitions based on device type
#if DEVICE_TYPE == DEVICE_TYPE_ONOFF || DEVICE_TYPE == DEVICE_TYPE_DIMMABLE
#define RELAY_PIN 4  // GPIO4 (D2)
#define BUTTON_PIN 5 // GPIO5 (D1)
#endif

#if DEVICE_TYPE == DEVICE_TYPE_TEMPERATURE
#define DHT_PIN 4 // GPIO4 (D2)
#endif

// Optional LED for status indication
#define LED_PIN 2 // Built-in LED on most ESP8266 boards