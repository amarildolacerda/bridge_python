# ESP-Matter Bridge

Bridge Matter para integrar dispositivos ESP8266 (WiFi) à Alexa via Matter, usando um ESP32 como dispositivo bridge.

## Arquitetura

```
┌─────────────────┐     WiFi     ┌──────────────────────┐    Matter     ┌─────────┐
│  ESP8266 #1     │◄───────────►│  ESP32 (Bridge)       │◄────────────►│  Alexa   │
│  (sensor/ator)  │             │                        │              │         │
└─────────────────┘             │  - REST API (porta 80) │              └─────────┘
                                │  - Bridge Matter       │
┌─────────────────┐             │  - Aggregator          │
│  ESP8266 #N     │◄───────────►│  - Device Registry     │
│  (sensor/ator)  │             └──────────────────────┘
└─────────────────┘
```

## REST API (ESP32 → ESP8266)

| Método | Rota | Descrição |
|--------|------|-----------|
| `POST` | `/api/device/register` | Registra dispositivo `{"id","type","name"}` |
| `POST` | `/api/device/state` | Atualiza estado `{"id","on":true,...}` |
| `GET` | `/api/device/commands?id=X` | Poll de comandos pendentes do Matter |
| `POST` | `/api/device/commands` | Poll via body `{"id":"X"}` |
| `GET` | `/api/device/info?id=X` | Dados do dispositivo |
| `GET` | `/api/devices` | Lista todos os dispositivos |

## Device Types Suportados

| Tipo | Matter | Clusters |
|------|--------|----------|
| `onoff` | OnOff Light (0x0100) | On/Off |
| `dimmable` | Dimmable Light (0x0101) | On/Off + Level Control |
| `temperature` | Temperature Sensor (0x0302) | Temperature Measurement |
| `humidity` | Humidity Sensor (0x0307) | Relative Humidity Measurement |
| `contact` | Contact Sensor (0x0015) | BooleanState |
| `occupancy` | Occupancy Sensor (0x0107) | Occupancy Sensing |
| `light_sensor` | Light Sensor (0x0106) | Illuminance Measurement |
| `tanque` | — (data only) | Sem endpoint Matter |

## Commissioning

```
chip-tool pairing onnetwork 0x1234 20202021
chip-tool onoff on 0x1234 2
chip-tool onoff off 0x1234 2
```

- Setup PIN: `20202021`
- Discriminator: `3840`

## Estrutura do Projeto

```
main/
├── app_main.cpp              # Entry point, init
├── app_bridge.cpp/h          # Lógica do bridge Matter
├── app_wifi_server.cpp/h     # Servidor HTTP REST
├── app_device_registry.cpp/h # Registro de dispositivos
├── app_driver.cpp            # Stub (sem hardware local)
└── app_priv.h                # Definições compartilhadas

clients/
└── esp8266_on_off/           # Firmware ESP8266 (on/off)
    ├── src/main.cpp          # WiFi, REST API, web page
    ├── include/config.h      # Configuração do dispositivo
    └── test/                 # Testes da API REST
```

## Compilar

```bash
idf.py build
idf.py monitor
```
