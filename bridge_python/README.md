# ESP-HA Bridge

Bridge em Python que conecta sensores ESP8266 ao Home Assistant via MQTT Discovery. Substitui o bridge ESP32 físico.

## Como Funciona

1. Os **clients ESP8266** se registram no bridge via HTTP/UDP
2. O bridge publica **entidades MQTT Discovery** no formato do Home Assistant
3. O **Home Assistant** descobre e cria os dispositivos automaticamente

## Instalação Rápida (Docker)

```bash
docker compose up -d
```

Isso sobe Mosquitto, Home Assistant e o bridge.

## Instalação como Add-on HA

Veja [HA_INTEGRATION.md](HA_INTEGRATION.md).

## Device Types Suportados

| Tipo | O que aparece no HA | Exemplo |
|------|---------------------|---------|
| `onoff` | Switch (liga/desliga) | Relé |
| `temperature` | Temperatura + Umidade | Sensor DHT21 |
| `gas` | Alarme (sim/não) + Nível de gás | Detector de gás |
| `rain` | Chuva (sim/não) + Nível | Sensor de chuva |
| `contact` | Porta (aberta/fechada) | Sensor magnético |
| `occupancy` | Presença | Sensor PIR |
| `light_sensor` | Luminosidade (lx) | LDR |
| `tanque` | Nível de água (%) | Boia |
| `humidity` | Umidade (%) | Sensor de umidade |
| `dimmable` | Luz ajustável | Lâmpada dimerizável |
| `electricity` | Corrente (mA) | Medidor |

## Variáveis de Ambiente

| Variável | Padrão | O que faz |
|----------|--------|-----------|
| `MQTT_HOST` | `core-mosquitto` | Endereço do Mosquitto |
| `MQTT_PORT` | `1883` | Porta do Mosquitto |
| `MQTT_USER` | vazio | Usuário MQTT |
| `MQTT_PASS` | vazio | Senha MQTT |
| `HTTP_PORT` | `80` | Porta do painel web |
| `LOG_LEVEL` | `info` | Nível do log |

## Painel Web

Acesse `http://<ip_do_bridge>:80/` para ver os dispositivos em tempo real.

## API

| O que faz | Rota |
|-----------|------|
| Health check | `GET /api/ping` |
| Listar devices | `GET /api/devices` |
| Status do bridge | `GET /api/gateway/info` |

## Clientes ESP8266

Veja a pasta `clients/` para exemplos de firmware Arduino.
