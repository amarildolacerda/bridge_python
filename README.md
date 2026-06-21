# ESP Bridge Gateway

Gateway bridge para conectar dispositivos **ESP8266/ESP32** à **Alexa** via **ESP RainMaker** ou ao **Home Assistant** via **bridge_python**. Clientes se autodescobrem via UDP, registram-se via HTTP REST API e o bridge gerencia a comunicação bidirecional de estado e comandos.

## Firmware Variants

| Variante | Protocolo | Build | Entrada |
|---|---|---|---|
| **RainMaker Gateway** (`main/`) | ESP RainMaker (MQTT) | ESP-IDF | `idf.py build` |
| **Python Bridge** (`bridge_python/`) | MQTT Discovery (HA) | Docker / add-on HA | `python -m app.main` |

## Clientes Suportados

| Cliente | Tipo | Plataforma | Descrição |
|---|---|---|---|
| `clients/esp8266_on_off/` | Interruptor on/off | ESP8266 + PlatformIO | Relé + botão físico |
| `clients/esp8266_dht21/` | Temperatura/Umidade | ESP8266 + PlatformIO | Sensor DHT21 |
| `clients/esp8266_tanque/` | Nível d'água | ESP8266 + PlatformIO | Sensor ultrassônico HC-SR04 |
| `clients/esp8266_gas/` | Detector de gás | ESP8266 + PlatformIO | Sensor MQ-2/5/7/135 |
| `clients/esp8266_chuva/` | Sensor de chuva | ESP8266 + PlatformIO | Sensor analógico de chuva |
| `clients/esp32_mqtt/` | Broker MQTT local | ESP32 + PlatformIO | Bridge MQTT standalone |

## Arquitetura

```
┌──────────────────┐    UDP:5000     ┌──────────────────┐      RainMaker / HA       ┌─────────┐
│  ESP8266 Client   │ ◄──"esp-bridge"──►   ESP32 Bridge   │ ◄──────────────────────► │  Alexa   │
│  (on_off, dht21,  │    HTTP:80      │  ou bridge_python │      (MQTT Discovery)    │  (Echo)  │
│   tanque)         │ ◄───REST API───►                      │                          │          │
└──────────────────┘                  └──────────────────┘                          └─────────┘
```

### Fluxo de Comunicação

1. Bridge anuncia-se na rede como `espbridge.local` (mDNS) e via broadcast UDP:5000
2. Client descobre o bridge via UDP (broadcast passivo ou discovery request ativo)
3. Client registra-se via `POST /api/device/register`
4. Client envia estado via `POST /api/device/state` (apenas quando muda + heartbeat periódico)
5. Bridge encaminha comandos da Alexa para o client via `GET /api/device/commands`
6. Bridge persiste dispositivos em NVS para restaurar após reboot

## UDP Discovery

- **Porta**: 5000 UDP
- **Service name**: `"esp-bridge"`
- Bridge responde a `{"service":"esp-bridge","discover":true}`
- Bridge faz broadcast periódico com IP e uptime
- Clientes enviam discovery request ao ligar e escutam broadcasts

## HTTP REST API

### Bridge (porta 80)

| Método | Rota | Descrição |
|---|---|---|
| GET | `/` | Dashboard web |
| GET | `/dashboard.css` | Estilos do dashboard |
| POST | `/api/device/register` | Registrar dispositivo |
| POST | `/api/device/remove` | Remover dispositivo |
| POST | `/api/device/state` | Atualizar estado |
| GET/POST | `/api/device/commands?id=X` | Obter comandos pendentes |
| GET | `/api/device/info?id=X` | Informações do dispositivo |
| GET | `/api/devices` | Listar dispositivos |
| GET | `/api/gateway/info` | Status do gateway (RainMaker) |
| GET | `/api/ping` | Health check |
| POST | `/api/device/heartbeat` | Heartbeat do cliente (RainMaker) |
| POST | `/api/gateway/reset` | Reiniciar gateway |
| GET | `/ws` | WebSocket (atualizações em tempo real) |

### Cliente gas

| Método | Rota | Descrição |
|--------|------|-----------|
| GET | `/` | Dashboard local |
| GET | `/api/state` | Estado (gas_level, alarm) |

### Cliente on/off

| Método | Rota | Descrição |
|---|---|---|
| GET | `/` | Dashboard local |
| GET | `/api/state` | Estado (JSON) |
| POST | `/api/on` | Ligar relé |
| POST | `/api/off` | Desligar relé |
| POST | `/api/toggle` | Inverter estado |

## Dispositivos Suportados

| Tipo | Descrição | RainMaker |
|---|---|---|
| `onoff` | Interruptor | switch |
| `dimmable` | Luminosidade | lightbulb |
| `temperature` | Temperatura | temp_sensor + Humidity |
| `humidity` | Umidade | device_create + Humidity |
| `contact` | Sensor de contato | contact_sensor |
| `occupancy` | Sensor de presença | device_create + Occupancy |
| `light_sensor` | Luminosidade | device_create + Light |
| `tanque` | Nível d'água | device_create + Level |
| `gas` | Detector de gás | device_create + GasLevel / GasAlarm |

## Como preparar o ambiente

### Pré-requisitos

```bash
sudo apt install ccache git curl
```
> `ccache` acelera rebuilds em até 10x.

### ESP-IDF

```bash
mkdir -p ~/.espressif/v5.5.4
git clone --recursive -b v5.5.4 https://github.com/espressif/esp-idf.git ~/.espressif/v5.5.4/esp-idf
```

> Se o clone falhar por falta de internet, reconecte a rede e execute:
> ```bash
> git -C ~/.espressif/v5.5.4/esp-idf submodule update --init --recursive
> ```

### RainMaker

```bash
git clone --recursive -b v1.8.2 https://github.com/espressif/esp-rainmaker.git ~/esp/esp-rainmaker
```

### Ativar ambiente

```bash
source config.sh    # carrega IDF + RainMaker + ccache
```

Após ativar, use `idf.py build` para compilar.

## Build

### RainMaker Gateway

```bash
source config.sh
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Python Bridge (Home Assistant)

```bash
cd bridge_python
python3 -m venv venv && source venv/bin/activate
pip install -r requirements.txt
python -m app.main
```

### Clientes (PlatformIO)

```bash
cd clients/esp8266_on_off/
pio run -t upload
pio device monitor
```

## Testes

```bash
# Validar discovery UDP do bridge
python3 test/validar_bridge_udp.py

# Escutar broadcasts UDP
python3 test/descobrir_clientes_udp.py --probe

# Monitorar clientes registrados
python3 test/escutar_clientes.py --host <ip_do_bridge>

# Simular bridge para testes
python3 test/escutar_udp.py
```

## Configuração

- **RainMaker**: até 32 dispositivos bridged
- **Partition**: OTA com 2 slots de 1600K
- **mDNS**: bridge anuncia como `espbridge.local`

## Limitações

- `const char*` do cJSON invalida após `cJSON_Delete()` — sempre copiar com `strncpy`
- Cliente deve tentar registro no `loop()`, não apenas no `setup()`
- `ENFILE` (limite de sockets): configurar `CONFIG_LWIP_MAX_SOCKETS=20`, `lru_purge_enable=true`
- Sensor de temperatura: criar parâmetro `"Humidity"` manualmente
- Heartbeat separado de dados para economizar budget MQTT do RainMaker
