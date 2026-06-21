# ESP8266 Presence Sensor

Cliente ESP8266 para sensor de presenca PIR (HC-SR501). Envia estado de ocupacao para o bridge via HTTP REST.

## Funcionalidades

- Leitura de sensor PIR HC-SR501 (digital, GPIO 4)
- LED indicador de presenca externo (GPIO 5)
- LED indicador de status interno (GPIO 2)
- Debounce de 3s para evitar flapping ao limpar presenca
- Comandos via terminal serial (pressione 'h' para ajuda)
- Descoberta automatica do gateway via UDP
- Registro HTTP no gateway
- Heartbeat periodico
- Servidor web embarcado com dashboard
- Portal de configuracao WiFi (WiFiManager)
- OTA via ArduinoOTA

## Hardware

| Componente           | GPIO |
|----------------------|------|
| PIR HC-SR501 (OUT)   | 4    |
| LED presenca (opcional) | 5 |
| LED status           | 2    |

## Configuracao

Edite `include/config.h`:

```cpp
#define DEVICE_NAME "Sensor Presenca"
#define PIR_PIN 4
#define ALARM_LED_PIN 5
#define OCCUPANCY_DEBOUNCE_MS 3000
```

## OTA

```bash
pio run -t upload --upload-port esp8266_xxxxxx.local
```

## API bridge

Tipo registrado: `"occupancy"`

Estado enviado:
```json
{"id":"esp8266_xxxxxx","occupancy":true}
```

## API local (no ESP8266)

| Rota         | Metodo | Descricao                    |
|--------------|--------|------------------------------|
| `/`          | GET    | Dashboard web                |
| `/api/state` | GET    | Estado atual (occupancy)     |

## Atalhos de teclado (terminal serial)

- `l` — ler sensor agora
- `s` — status do dispositivo
- `r` — restart
- `u` — info OTA
- `h/?` — ajuda

## Dependencias (PlatformIO)

- `bblanchon/ArduinoJson` ^7.3.1
- `tzapu/WiFiManager` ^2.0.0
