# ESP8266 DHT22 + MQ-2 Client

Cliente ESP8266 com sensor DHT22 (temperatura/umidade) e MQ-2 (gás). Registra dois devices no bridge: tipo `"temperature"` e tipo `"gas"`.

## Funcionalidades

- Leitura de temperatura e umidade (DHT22)
- Leitura de gás (MQ-2) via entrada analógica
- Alarme por limiar via software + LEDs indicadores (alerta e alarme)
- LED de status (WiFi, bridge, config portal)
- Comandos via terminal serial (pressione 'h' para ajuda)
- Descoberta automática do gateway via UDP
- Registro HTTP no gateway (dois devices separados)
- Envio periódico de telemetria
- Heartbeat periódico
- Servidor web embarcado com dashboard
- Portal de configuração WiFi (WiFiManager)
- OTA via ArduinoOTA e upload HTTP (`/api/ota`)

## Hardware

| Componente     | GPIO |
|----------------|------|
| DHT22          | 4    |
| MQ-2 (analog)  | A0   |
| MQ-2 (digital) | 16   |
| LED status     | 2    |
| LED alerta     | 14   |
| LED alarme     | 12   |

## Faixas de leitura

### Gás (ADC 0–1024 mapeado para 0–100%)

| % gás   | LED alerta           | LED alarme          | Interpretação     |
|---------|----------------------|---------------------|-------------------|
| 0–14%   | Apagado              | Apagado             | Normal            |
| 15–29%  | Pisca lento (1s)     | Apagado             | Atenção           |
| 30%+    | Aceso                | Pisca rápido (200ms)| Alarme — vazamento|

## Configuração

Edite `include/config.h`:

```cpp
#define DEVICE_NAME "DHT + Gas"
#define DHT_PIN 4
#define GAS_ALERT_THRESHOLD 15
#define GAS_ALARM_THRESHOLD 30
```

## Build (PlatformIO)

```bash
pio run -t upload
pio device monitor
```

## API local (no ESP8266)

| Rota         | Método | Descrição                                         |
|--------------|--------|---------------------------------------------------|
| `/`          | GET    | Dashboard web                                     |
| `/api/state` | GET    | Estado atual (temperature, humidity, gas_level, alarm) |
| `/api/ota`   | POST   | Upload de firmware via OTA (multipart)            |

## API bridge

Tipos registrados: `"temperature"` e `"gas"`

### Device `temperature`

```json
{"id":"esp8266_xxxxxx_temp","type":"temperature","name":"DHT + Gas","ip":"192.168.1.100"}
```

Estado enviado:
```json
{"id":"esp8266_xxxxxx_temp","temperature":25.3,"humidity":65.2}
```

### Device `gas`

```json
{"id":"esp8266_xxxxxx_gas","type":"gas","name":"DHT + Gas","ip":"192.168.1.100"}
```

Estado enviado:
```json
{"id":"esp8266_xxxxxx_gas","gas_level":42,"alarm":false}
```

## OTA

Via ArduinoOTA (hostname: `{device_id}.local`, porta 8266):

```bash
pio run -t upload --upload-port esp8266_xxxxxx.local
```

Via upload HTTP (`/api/ota`):

```bash
curl -F "firmware=@.pio/build/nodemcuv2/firmware.bin" http://esp8266_xxxxxx.local/api/ota
```

## Atalhos de teclado (terminal serial)

- `l` — ler sensores agora
- `s` — status do dispositivo
- `r` — restart
- `t` — testar pinos do DHT22
- `u` — info OTA
- `h/?` — ajuda

## Dependências (PlatformIO)

- `bblanchon/ArduinoJson` ^7.3.1
- `tzapu/WiFiManager` ^2.0.0
- `adafruit/DHT sensor library` ^1.4.6
