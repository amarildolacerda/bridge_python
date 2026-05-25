# ESP32 MQTT Bridge - Message Exchange Documentation

This document explains how devices should communicate with the ESP32 MQTT Bridge to ensure proper registration, discovery, and message routing.

## Overview

The ESP32 MQTT Bridge functions as:
1. **MQTT Broker** - Accepts MQTT client connections and routes messages
2. **Device Registry** - Tracks connected devices and their status
3. **HTTP Dashboard** - Provides web interface for monitoring
4. **Broadcast Service** - Announces its presence on the network

## Message Exchange Patterns

### 1. Device Registration

Devices can register with the bridge in two ways:

#### Explicit Registration (Recommended)
Publish to: `mqtt-bridge/register`
Payload (JSON):
```json
{
  "id": "device_unique_id",
  "type": "device_type_string",
  "name": "Human readable device name"
}
```

Example:
```json
{
  "id": "esp8266_001",
  "type": "sensor",
  "name": "Temperature Sensor Kitchen"
}
```

#### Implicit Registration
Any device publishing to topics under the bridge prefix will be automatically registered:
Publish to: `mqtt-bridge/<device_id>/<suffix>`
Example: `mqtt-bridge/esp8266_001/temperature`

The bridge will extract the device ID from the topic and register it with type "auto".

### 2. Topic Structure

All bridge-related topics use the prefix defined in `config.h`:
```
#define TOPIC_PREFIX "mqtt-bridge"
```

#### Device Telemetry/State
Publish from device to bridge:
```
mqtt-bridge/<device_id>/<sensor_type>
```
Examples:
- `mqtt-bridge/esp8266_001/temperature`
- `mqtt-bridge/esp8266_001/humidity`
- `mqtt-bridge/esp8266_001/switch/state`

Payload: Can be any format (string, number, JSON) - the bridge is format-agnostic for regular telemetry.

#### Device Commands
Publish from bridge/controller to device:
```
mqtt-bridge/<device_id>/cmd/<command_type>
```
Examples:
- `mqtt-bridge/esp8266_001/cmd/reboot`
- `mqtt-bridge/esp8266_001/cmd/set_threshold`
- `mqtt-bridge/esp8266_001/cmd/configure`

### 3. Message Flow Examples

#### Scenario 1: Sensor Reporting Temperature
1. ESP8266 sensor publishes: `mqtt-bridge/kitchen_sensor/temperature` with payload "23.5"
2. Bridge receives message, extracts device ID "kitchen_sensor"
3. Bridge auto-registers device if not seen before (type: "auto")
4. Bridge marks device as online
5. Message is routed to any subscribers of that topic

#### Scenario 2: Controlling a Switch
1. Controller publishes: `mqtt-bridge/living_room_switch/cmd/set_state` with payload "ON"
2. Bridge receives message, extracts device ID "living_room_switch"
3. Bridge ensures device is registered (creates if needed)
4. Message is delivered to the living_room_switch device if connected
5. Device acts on the command

#### Scenario 3: Device Registration with Metadata
1. Device publishes to: `mqtt-bridge/register`
   Payload: `{"id": "sensor_001", "type": "bme280", "name": "Office Environment"}`
2. Bridge processes registration, stores device metadata
3. Device begins normal telemetry: `mqtt-bridge/sensor_001/temperature`, etc.
4. Bridge uses stored metadata for dashboard display

### 4. Device Status Tracking

The bridge tracks:
- **Online/Offline Status**: Based on last message timestamp (timeout: 60 seconds)
- **Last Seen**: Timestamp of last received message
- **Device Count**: Total registered devices
- **Recent Activity**: Devices seen in last 5 minutes (configurable)

### 5. HTTP API Endpoints

The bridge provides these HTTP endpoints on port 80:

#### GET `/`
- Returns HTML dashboard showing all registered devices
- Visual indication of online/offline status
- Last seen timestamps

#### GET `/api/devices`
- Returns JSON array of all devices
- Each device object contains:
  ```json
  {
    "id": "device_unique_id",
    "type": "device_type",
    "name": "device_name",
    "online": true/false,
    "last_seen_sec": seconds_since_last_message
  }
  ```

### 6. Bridge Discovery

The bridge supports two discovery mechanisms:

#### 6.1 Active Discovery Request (Recommended)

Clients can send a UDP broadcast request to discover the bridge instantly:

**Request** (client → broadcast on port 5000):
```json
{
  "service": "mqtt-bridge",
  "discover": true
}
```

**Response** (bridge → client):
```json
{
  "service": "mqtt-bridge",
  "name": "ESP32 MQTT Bridge",
  "mqtt_port": 1883,
  "http_port": 80,
  "ip_sta": "192.168.1.100",
  "device_count": 5
}
```

This eliminates the wait for periodic broadcasts. Clients should:
1. Send the discovery request on startup
2. Wait 3-5 seconds for a response
3. Fall back to passive listening if no response received

#### 6.2 Periodic Broadcast (Passive)

The bridge also periodically broadcasts its presence via UDP:
- Port: 5000 (BROADCAST_PORT)
- Interval: 10 seconds (BROADCAST_INTERVAL_MS)
- Payload: Same JSON format as the active response

### 7. MQTT Connection Details

- **Port**: 1883 (standard MQTT)
- **Authentication**: None by default (can be enabled in config.h)
- **Keep Alive**: Standard MQTT keep alive applies
- **QoS Levels**: Supports QoS 0, 1, 2 (handled by sMQTTBroker library)

### 8. Best Practices for Clients

1. **Unique Device IDs**: Use MAC address or chip ID as basis for unique IDs
2. **Consistent Topics**: Follow the `mqtt-bridge/<device_id>/<suffix>` pattern
3. **Regular Telemetry**: Publish sensor data at regular intervals to maintain "online" status
4. **Handle Disconnections**: Implement reconnection logic for unstable networks
5. **Use Registration**: For devices with complex metadata, use explicit registration
6. **Respect Timeouts**: Understand that devices silent for >60s will be marked offline

### 9. Example Client Pseudocode

```cpp
// Setup
String deviceId = getChipId();  // Unique identifier
String deviceType = "temperature_sensor";
String deviceName = "Living Room Temp";

// Register explicitly (optional but recommended)
mqttClient.publish("mqtt-bridge/register", 
  "{\"id\":\"" + deviceId + "\",\"type\":\"" + deviceType + "\",\"name\":\"" + deviceName + "\"}");

// Regular telemetry
void loop() {
  float temperature = readSensor();
  
  // Publish to device-specific topic
  String topic = "mqtt-bridge/" + deviceId + "/temperature";
  mqttClient.publish(topic, String(temperature).c_str());
  
  delay(30000);  // Report every 30 seconds
}

// Handle incoming commands
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String topicStr = topic;
  if (topicStr.endsWith("/cmd/set_threshold")) {
    float threshold = atof((char*)payload);
    setTemperatureThreshold(threshold);
  }
}
```

## Configuration Notes

Key settings in `config.h`:
- `WIFI_AP_SSID` / `WIFI_AP_PASSWORD`: For devices connecting directly to bridge AP
- `MQTT_PORT`: Standard 1883 (change if needed)
- `TOPIC_PREFIX`: Change if you want different topic namespace
- `DEVICE_TIMEOUT_MS`: How long before device considered offline (default 60000ms = 60s)
- `CLIENT_HISTORY_MS`: How long to keep device in history (default 300000ms = 5min)

## Troubleshooting

1. **Device Not Appearing in Dashboard**
   - Check if device is publishing to correct topic format
   - Verify MQTT connection to bridge IP:1883
   - Ensure device ID doesn't contain slashes or special chars that break topics

2. **Commands Not Received**
   - Verify device is subscribed to `mqtt-bridge/<device_id>/cmd/#`
   - Check bridge logs for subscription messages
   - Ensure device maintains MQTT connection

3. **Bridge Not Discoverable**
   - Confirm bridge is connected to WiFi (STA mode) or AP is active
   - Check UDP broadcast on port 5000
   - Verify no network isolation/broadcast blocking

## Security Considerations

1. **No Authentication by Default**: For trusted networks only
2. **Topic Hijacking**: Devices can publish as any ID - implement validation if needed
3. **Network Exposure**: MQTT broker is accessible to all network clients
4. **Consider Enabling**: Uncomment MQTT_USERNAME/MQTT_PASSWORD in config.h for basic auth

For production deployments, consider:
- Enabling MQTT authentication
- Using network segmentation
- Implementing message validation on device side
- Adding encryption via MQTT over TLS (requires library changes)