# ESP8266 DHT22 + MQ-2 Client Design

## Overview

Cliente ESP8266 combinando sensor DHT22 (temperatura/umidade) e MQ-2 (gás) no mesmo dispositivo. Registra dois devices separados no bridge: um tipo `"temperature"` e um tipo `"gas"`.

## Hardware

| Componente | GPIO | Notas |
|---|---|---|
| DHT22 | 4 (D2) | Temperatura + Umidade |
| MQ-2 analógico | A0 | Nível de gás (0-100%) |
| MQ-2 digital | 16 (D0) | Alarme digital do módulo |
| LED status | 2 (D4) | Onboard |
| LED alerta gás | 14 (D5) | Pisca lento em atenção |
| LED alarme gás | 12 (D6) | Pisca rápido em vazamento |

## Arquitetura

### Device IDs

- `esp8266_<chip_id>_temp` — device temperatura
- `esp8266_<chip_id>_gas` — device gás
- `esp8266_<chip_id>` — base ID para heartbeat único

### Registro no Bridge

POST `/api/device/register` × 2:

```json
{"id":"esp8266_xxxxxx_temp","type":"temperature","name":"<name>","ip":"<ip>"}
{"id":"esp8266_xxxxxx_gas","type":"gas","name":"<name>","ip":"<ip>"}
```

### Estado

**Temperatura:** `POST /api/device/state` com `id: esp8266_xxxxxx_temp`
```json
{"id":"esp8266_xxxxxx_temp","temperature":25.3,"humidity":60.1}
```

**Gás:** `POST /api/device/state` com `id: esp8266_xxxxxx_gas`
```json
{"id":"esp8266_xxxxxx_gas","gas_level":12,"alarm":false}
```

### Heartbeat

2 heartbeats por intervalo de telemetria (um pra cada device registrado).

## Funcionalidades

- Detecção automática de pino DHT (herdado do `esp8266_dht21`)
- OTA via ArduinoOTA + HTTP upload `/api/ota` (herdado do `esp8266_dht21`)
- Dashboard web único com 3 cards (temp, umidade, gás)
- WiFiManager com campos: bridge_host, bridge_port, dev_name
- EEPROM para nome do device
- LED status onboard (padrão existente)
- LEDs indicadores de gás (alerta/alarme)
- Comandos serial: `l`, `s`, `r`, `u`, `t`, `h`/`?`

## Estrutura do Projeto

```
clients/esp8266_dht_gas/
├── platformio.ini
├── README.md
├── include/
│   ├── config.h
│   └── pages.h
└── src/
    └── main.cpp
```

## Dependências (PlatformIO)

- `bblanchon/ArduinoJson` ^7.3.1
- `tzapu/WiFiManager` ^2.0.0
- `adafruit/DHT sensor library` ^1.4.6

## Pinagem (NodeMCU)

| Pin | Função |
|---|---|
| D2 (GPIO4) | DHT22 data |
| A0 | MQ-2 analógico |
| D0 (GPIO16) | MQ-2 digital (alarme do módulo) |
| D4 (GPIO2) | LED status onboard |
| D5 (GPIO14) | LED alerta gás |
| D6 (GPIO12) | LED alarme gás |
