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
- **OnOffLight**: On/Off cluster
- **TemperatureSensor**: Temperature Measurement cluster
- **HumiditySensor**: Relative Humidity Measurement cluster
- **ContactSensor**: BooleanState cluster
- **DimmableLight**: On/Off + Level Control clusters

## 3. Comunicação WiFi (ESP32 ↔ ESP8266)

### Opção recomendada: **HTTP REST API** (simples e amplamente suportada)

O ESP32 roda um servidor HTTP leve. ESP8266s fazem POST com seu estado.

#### Endpoints da API REST no ESP32:

```
POST /api/device/register
  Body: { "id": "esp8266_1", "type": "onoff", "name": "Luz Sala" }

POST /api/device/{id}/state
  Body: { "on": true/false }        # para OnOff
  Body: { "temperature": 23.5 }     # para sensor
  Body: { "humidity": 60 }          # para umidade
  Body: { "contact": true/false }   # para contato

GET  /api/device/{id}/state
  Response: { "on": true, ... }

GET  /api/device/{id}/commands
  Response: { "commands": [...] }  # comandos pendentes do Matter para ESP8266
  (Long-poll ou pooling periódico)
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
| `app_wifi_server.cpp/h`  | Servidor HTTP WiFi para ESP8266s             |
| `app_device_registry.cpp/h` | Registro e estado dos dispositivos       |
| `app_driver.cpp/h`       | Drivers existentes (adaptar)                 |
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
4. ESP8266 envia atualizações via POST `/api/device/{id}/state`
5. ESP32 atualiza os atributos Matter correspondentes

### 5.3 Alexa controla dispositivo
1. Alexa envia comando Matter (ex: On/Off)
2. Attribute update callback no ESP32 é disparado
3. ESP32 atualiza estado interno
4. ESP32 expõe o comando para o ESP8266 buscar (GET `/api/device/{id}/commands`)
5. ESP8266 executa o comando

## 6. Implementação por Etapas

### ✅ Etapa 0: Configuração do ambiente (concluída)
- Docker + VSCode devcontainer configurado
- Projeto base `generic_switch` compilando

### 🔲 Etapa 1: Servidor HTTP no ESP32
- Implementar servidor REST básico
- Endpoints de registro e estado
- Testar com curl/Postman via WiFi

### 🔲 Etapa 2: Device Registry
- Estrutura de dados para múltiplos dispositivos
- Mapeamento tipo → clusters Matter
- Gerenciamento de IDs

### 🔲 Etapa 3: Bridge Matter (Aggregator)
- Substituir `generic_switch` por `aggregator`
- Criar endpoints `bridged_device` dinamicamente
- Mapear atributos WiFi → clusters Matter

### 🔲 Etapa 4: Comunicação bidirecional
- Pooling de comandos pelos ESP8266s
- Ou WebSocket para comando em tempo real

### 🔲 Etapa 5: Firmware ESP8266
- Cliente WiFi
- POST de estado para o bridge
- GET/Poll de comandos

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

# Chip-Tool (commissioning)
chip-tool pairing onnetwork 0x1234 20202021

# Chip-Tool (controle OnOff)
chip-tool onoff on 0x1234 2
chip-tool onoff off 0x1234 2
```
