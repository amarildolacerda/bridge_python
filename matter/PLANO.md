# Planejamento: ESP-Matter Bridge (ESP8266 → Matter/Alexa)

## Visão Geral

Bridge Matter para integrar dispositivos ESP8266 (WiFi) à Alexa via Matter, usando um ESP32 como dispositivo bridge.

---

## 1. Arquitetura do Sistema

```
┌─────────────────┐     WiFi     ┌──────────────────────┐    Matter     ┌─────────┐
│  ESP8266 #1     │◄───────────►│  ESP32 (Bridge)       │◄────────────►│  Alexa   │
│  (sensor/ator)  │             │                        │              │         │
└─────────────────┘             │  - Servidor WiFi      │              └─────────┘
                                │  - Bridge Matter       │
┌─────────────────┐             │  - Endpoints Bridge    │
│  ESP8266 #2     │◄───────────►│  - Agregador           │
│  (sensor/ator)  │             └──────────────────────┘
└─────────────────┘
```

## 2. Topologia de Endpoints Matter

| Endpoint | Device Type          | Descrição                          |
|----------|----------------------|------------------------------------|
| 0        | Root Node            | Nó raiz obrigatório                |
| 1        | Aggregator           | Dispositivo bridge agregador       |
| 2        | Bridged Device #1    | ESP8266 #1 (ex: OnOffLight)        |
| 3        | Bridged Device #2    | ESP8266 #2 (ex: TemperatureSensor) |
| ...      | ...                  | ...                                |

Cada dispositivo bridgeado pode ter diferentes clusters dependendo do tipo:
- **OnOffLight** (onoff): On/Off cluster
- **DimmableLight** (dimmable): On/Off + Level Control clusters
- **TemperatureSensor** (temperature): Temperature Measurement cluster
- **HumiditySensor** (humidity): Relative Humidity Measurement cluster
- **ContactSensor** (contact): BooleanState cluster
- **OccupancySensor** (occupancy): Occupancy Sensing cluster
- **LightSensor** (light_sensor): Illuminance Measurement cluster
- **Tanque** (tanque): data-only, sem endpoint Matter

## 3. Comunicação WiFi (ESP32 ↔ ESP8266)

### Opção recomendada: **HTTP REST API** (simples e amplamente suportada)

O ESP32 roda um servidor HTTP leve. ESP8266s fazem POST com seu estado.

#### Endpoints da API REST no ESP32:

```
POST /api/device/register
  Body: { "id": "esp8266_1", "type": "onoff", "name": "Luz Sala" }
  Response: { "status": "ok", "endpoint_id": 2 }

POST /api/device/state
  Body: { "id": "esp8266_1", "on": true }
  Body: { "id": "esp8266_1", "temperature": 23.5, "humidity": 60 }
  Body: { "id": "esp8266_1", "contact": true }

GET  /api/device/commands?id=esp8266_1
  Response: { "commands": [{"cluster":"onoff","command":"set_onoff","data":"1"}] }

POST /api/device/commands
  Body: { "id": "esp8266_1" }
  Response: { "commands": [...] }

GET  /api/device/info?id=esp8266_1
  Response: { "id":"...", "name":"...", "type":"onoff", "endpoint_id":2, "online":true }

GET  /api/devices
  Response: { "devices": [...] }
```

#### Alternativa: **MQTT**
- Mais robusto para múltiplos dispositivos
- Usar broker Mosquitto no ESP32 ou externo
- Tópicos: `espbridge/device/{id}/state`, `espbridge/device/{id}/command`

**Decidir após validar a POC com REST.**

## 4. Estrutura do Código (dentro de `/project/main`)

| Arquivo                  | Descrição                                    |
|--------------------------|----------------------------------------------|
| `app_main.cpp`           | Setup Matter, loops principal                |
| `app_bridge.cpp/h`       | Lógica do bridge: criar endpoints bridgeados |
| `app_wifi_server.cpp/h`  | Servidor HTTP REST para ESP8266s             |
| `app_device_registry.cpp/h` | Registro e estado dos dispositivos       |
| `app_driver.cpp`         | Stub (sem hardware local no bridge)          |
| `app_priv.h`             | Definições compartilhadas                    |

## 5. Fluxo de Funcionamento

### 5.1 Inicialização (ESP32)
1. Inicializar NVS
2. Conectar ao WiFi (STA mode) ou criar AP
3. Iniciar servidor HTTP
4. Criar nó Matter com endpoint Aggregator
5. Registrar dispositivos bridgeados dinamicamente
6. Iniciar Matter (commissioning)

### 5.2 ESP8266 envia estado
1. ESP8266 conecta no WiFi
2. Faz POST `/api/device/register` com seu tipo
3. ESP32 cria dinamicamente um endpoint Bridged Device no Matter
4. ESP8266 envia atualizações via POST `/api/device/state` com `{"id":"...", "on":true}`
5. ESP32 atualiza os atributos Matter correspondentes

### 5.3 Alexa controla dispositivo
1. Alexa envia comando Matter (ex: On/Off)
2. Attribute update callback no ESP32 é disparado
3. ESP32 enfileira comando pendente no device registry
4. ESP8266 faz polling GET `/api/device/commands?id=...` e coleta o comando
5. ESP8266 executa o comando

## 6. Implementação por Etapas

### ✅ Etapa 0: Configuração do ambiente (concluída)
- Docker + VSCode devcontainer configurado
- Projeto compilando com ESP-Matter e ESP-IDF

### ✅ Etapa 1: Servidor HTTP no ESP32 (concluída)
- Servidor REST implementado em `app_wifi_server.cpp`
- Endpoints: register, state, commands, info, devices
- Porta 80, `esp_http_server`

### ✅ Etapa 2: Device Registry (concluída)
- Array estático thread-safe com mutex
- 8 tipos de dispositivo mapeados para Matter
- lookup por ID ou endpoint_id

### ✅ Etapa 3: Bridge Matter Aggregator (concluída)
- Node Matter com endpoint Aggregator
- Bridged devices criados dinamicamente via `esp_matter_bridge::create_device()`
- Atributos WiFi → clusters Matter mapeados em `bridge_update_matter_state()`

### ✅ Etapa 4: Comunicação bidirecional (concluída)
- Polling de comandos pelos ESP8266s via `GET /api/device/commands`
- Comandos enfileirados em `device_registry_add_command()`
- Fila de até 16 comandos por dispositivo

### ✅ Etapa 5: Firmware ESP8266 (concluída)
- Cliente MQTT no `esp8266_on_off` com descoberta UDP
- Página web com botões Ligar/Desligar/Inverter
- API REST local no ESP8266: `/api/on`, `/api/off`, `/api/toggle`, `/api/state`

### 🔲 Etapa 6: Integração Alexa
- Commissioning Matter na Alexa
- Testes de controle de voz

## 7. Estratégia de Testes

1. **Teste unitário**: Servidor HTTP (curl local)
2. **Teste bridge**: ESP8266 simulado por script Python
3. **Teste Matter**: Chip-Tool para commissioning e controle
4. **Teste integrado**: Alexa + dispositivo físico

## 8. Riscos e Mitigações

| Risco                                | Mitigação                                      |
|--------------------------------------|------------------------------------------------|
| Limite de RAM para muitos endpoints  | Usar endpoints dinâmicos, limite configurável  |
| Latência WiFi → Matter               | WebSocket para push de estado                  |
| Compatibilidade Alexa + Bridged Device | Usar device types bem suportados (OnOff, Temp) |
| ESP32 sem Thread (só WiFi)           | Usar Matter-over-WiFi (já suportado)          |

## 9. Comandos Úteis

```bash
# Compilar
idf.py build

# Monitor serial
idf.py monitor

# Chip-Tool (commissioning via WiFi)
chip-tool pairing onnetwork 0x1234 20202021

# Chip-Tool (controle OnOff - endpoint 2 = primeiro bridged device)
chip-tool onoff on 0x1234 2
chip-tool onoff off 0x1234 2

# Chip-Tool (ler temperatura - endpoint 3)
chip-tool temperaturmeasurement read measured-value 0x1234 3
```
