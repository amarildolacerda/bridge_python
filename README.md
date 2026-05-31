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

## Dashboard

Acesse `http://<ip-do-esp32>/` para o dashboard web com:

- Status do bridge (IP, uptime, heap livre)
- Códigos de comissionamento Matter (manual code, QR payload)
- Botão **Reabrir Comissionamento** — reabre janela BLE por 60s sem reboot
- Lista de dispositivos bridgeados com estado online/offline
- Atualização automática a cada 5s

## REST API

### Bridge

| Método | Rota | Descrição |
|--------|------|-----------|
| `GET` | `/api/bridge/info` | IP, uptime, heap livre |
| `GET` | `/api/bridge/commissioning` | Códigos de comissionamento (PIN, manual code, QR) |
| `POST` | `/api/bridge/commission` | Reabre janela de comissionamento por 60s |
| `POST` | `/api/bridge/reset` | Reinicia o ESP32 |

### Dispositivos (ESP8266 → ESP32)

| Método | Rota | Descrição |
|--------|------|-----------|
| `POST` | `/api/device/register` | Registra dispositivo `{"id","type","name"}` |
| `POST` | `/api/device/state` | Atualiza estado `{"id","on":true,...}` |
| `POST` | `/api/device/remove` | Remove dispositivo `{"id":"..."}` |
| `GET` | `/api/device/commands?id=X` | Poll de comandos pendentes do Matter |
| `POST` | `/api/device/commands` | Poll via body `{"id":"X"}` |
| `GET` | `/api/device/info?id=X` | Dados do dispositivo |
| `GET` | `/api/devices` | Lista todos os dispositivos |

### WebSocket

| Rota | Descrição |
|------|-----------|
| `/ws` | Streaming de estado a cada 2s (IP, uptime, heap) |

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

### Comissionamento inicial

O ESP32 inicia com BLE desligado, aguarda WiFi (STA) por até 15s. Se obtiver IP, habilita BLE commissioning automaticamente. Se não, habilita BLE direto para comissionamento via Alexa/Google Home.

```
chip-tool pairing ble-wifi 0x1234 <ssid> <pass> 20202021 3840
chip-tool onoff on 0x1234 2
chip-tool onoff off 0x1234 2
```

- Setup PIN: `20202021`
- Discriminator: `3840`

### Reabrir comissionamento (sem reboot)

Pelo dashboard: clique em **Reabrir Comissionamento** → janela BLE de 60s.

Pela API:
```bash
curl -X POST http://<esp32>/api/bridge/commission
```

Pelo serial (chip-shell):
```
matter ble start
```

## Estrutura do Projeto

```
main/
├── app_main.cpp                # Entry point, init, commissioning
├── app_bridge.cpp/h            # Lógica do bridge Matter
├── app_wifi_server.cpp/h       # Servidor HTTP REST + WebSocket + UDP discovery
├── app_device_registry.cpp/h   # Registro + estado + comandos pendentes
├── app_driver.cpp              # Stub (sem hardware local)
├── app_reset.h                 # Botão de reset físico
├── app_onboarding.h            # Acesso aos códigos de onboarding
├── app_priv.h                  # Definições compartilhadas
└── web/
    ├── dashboard.html          # Dashboard web embarcado
    └── dashboard.css           # Estilo Dashforge (leve)

clients/
├── esp8266_on_off/             # Firmware ESP8266 (on/off) — funcional
└── esp8266_dh11/               # Firmware ESP8266 (DHT11) — em desenvolvimento

test/                           # Scripts de teste Python
```

## Memória (ESP32 sem PSRAM)

RAM interna ~520KB DRAM. O projeto é otimizado para BSS mínimo:

- Compilação `-Os` (size optimization)
- Arrays estáticos grandes movidos para heap (`malloc`)
- `MAX_PENDING_COMMANDS=4`, `MAX_COMMAND_DATA_LEN=32`
- `CONFIG_SPIRAM` desabilitado

Verifique consumo:
```bash
idf.py size
```

## Compilar

```bash
# Ativar ambiente
cd esp-idf; source export.sh; cd ..
cd esp-matter; source export.sh; cd ..
export IDF_CCACHE_ENABLE=1

# Compilar
idf.py build

# Gravar + monitor
idf.py flash monitor

# Verificar memória
idf.py size
```
