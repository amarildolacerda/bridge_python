# ESP-Matter Bridge — Diretiva para Sessão IA

## Skills Disponíveis
- `guia-lora-gateway`: Gateway multi-rádio (LoRa/UDP/RF433/nRF24) — arquitetura, protocolo, fluxo de mensagens. Carregar com `skill` tool quando o assunto for o projeto LoRaGateway de amarildolacerda.

# ESP-Matter Bridge — Diretiva para Sessão IA

## Arquitetura Geral

Bridge Matter para integrar dispositivos ESP8266 (WiFi) à Alexa via Matter, usando ESP32 como bridge.

```raw
ESP8266 (sensor/ator) --WiFi--> ESP32 Bridge (HTTP REST API) --Matter--> Alexa
```

## Estrutura do Projeto

```raw
/project/
├── main/                          # Firmware ESP32 (bridge Matter)
│   ├── app_main.cpp               # Setup Matter + loops
│   ├── app_bridge.cpp/h           # Bridge Matter: endpoints bridgeados
│   ├── app_wifi_server.cpp/h      # Servidor HTTP REST para ESP8266s
│   ├── app_device_registry.cpp/h  # Registro e estado dos dispositivos
│   ├── app_driver.cpp             # Stub (sem hardware local)
│   └── app_priv.h                 # Definições compartilhadas
├── clients/
│   ├── esp8266_on_off/            # Cliente ESP8266: relé on/off (funcional)
│   └── esp8266_dh11/              # Cliente ESP8266: sensor DHT11 (em desenvolvimento)
├── sdkconfig                      # Config ESP-IDF (não commitar)
├── sdkconfig.defaults             # Config base do projeto
├── partitions.csv                 # Partições OTA (2 x ~1.9MB)
├── CMakeLists.txt                 # Build IDF
└── test/                          # Scripts de teste Python
```

## Plataforma: ESP32 (Xtensa dual-core)

- **IDF target:** `esp32` (sem PSRAM no hardware atual)
- **CPU:** 160MHz
- **PSRAM:** NÃO disponível — `# CONFIG_SPIRAM is not set`
- **RAM interna:** ~520KB DRAM (compartilhado entre .data, .bss, heap)
- **WiFi:** STA mode + servidor HTTP porta 80
- **BLE:** NimBLE habilitado para Matter commissioning

## LIMITAÇÕES CRÍTICAS DE MEMÓRIA (DRAM)

### Problema: overflow de `.dram0.bss` (19704 bytes)

**Causas raiz (já mitigadas nesta sessão):**

| Fonte | Tamanho BSS | Ação tomada |
|---|---|---|
| `s_devices[MAX_BRIDGED_DEVICES]` (11 × 3207 bytes) | ~35 KB | Reduzido `MAX_PENDING_COMMANDS` 16→4 e `MAX_COMMAND_DATA_LEN` 128→64 |
| `s_scratch[8192]` no wifi_server | 8 KB | Movido para heap dinâmico (`malloc`) |
| mbedTLS SSL buffers | 20 KB | Reduzido: `SSL_IN=16384→8192`, `SSL_OUT=4096→2048` |
| Compilação `-Og` (debug) | - | Mudado para `-Os` (size optimization) |

**Regras de memória (OBRIGATÓRIO respeitar):**

1. NUNCA adicione arrays estáticos grandes em BSS (prefira heap via `malloc`/`calloc`)
2. NUNCA use `CONFIG_COMPILER_OPTIMIZATION_DEBUG` — sempre `-Os`
3. NUNCA declare buffers grandes como `static char buf[N]` — use alocação dinâmica
4. Prefira `uint8_t`/`uint16_t` a `int` em structs grandes
5. Verifique consumo de BSS com `idf.py size` antes de fechar alterações
6. Se precisar de mais RAM, a única solução é PSRAM (mudar de hardware)

## ESP8266 Client Firmware — Padrão (REPLICAR)

### Estrutura padrão por cliente:

```raw
clients/esp8266_<tipo>/
├── include/config.h         # Defines centralizados
├── src/main.cpp             # Firmware completo
├── platformio.ini           # Dependências
├── .vscode/
│   ├── c_cpp_properties.json  # IntelliSense com defines
│   ├── extensions.json
│   └── launch.json
└── test/                    # Scripts de teste
```

### Config.h — Padrão OBRIGATÓRIO:

```c
#pragma once
#include <Arduino.h>

#define DEVICE_ID "esp8266_<local>"
#define DEVICE_NAME "<Nome Amigável>"

// Device types (numerados, NÃO strings)
#define DEVICE_TYPE_ONOFF 1
#define DEVICE_TYPE_TEMPERATURE 2
#define DEVICE_TYPE_CONTACT 3
#define DEVICE_TYPE_OCCUPANCY 4
#define DEVICE_TYPE_DIMMABLE 5

#define DEVICE_TYPE DEVICE_TYPE_<TIPO>

// Timing
#define STATE_UPDATE_INTERVAL 5000
#define TELEMETRY_INTERVAL 30000
#define COMMAND_POLL_INTERVAL 100

// UDP Discovery (sempre porta 5000)
#define DISCOVERY_PORT 5000
#define DISCOVERY_TIMEOUT 30000

#define BRIDGE_PORT 80

// Pinos específicos por tipo
#if DEVICE_TYPE == DEVICE_TYPE_ONOFF || DEVICE_TYPE == DEVICE_TYPE_DIMMABLE
#define RELAY_PIN 4
#define BUTTON_PIN 5
#endif

#if DEVICE_TYPE == DEVICE_TYPE_TEMPERATURE
#define DHT_PIN 4
#define DHT_TYPE DHT11
#endif

#define LED_PIN 2  // LED onboard
```

### Main.cpp — Componentes obrigatórios:

1. **WiFiManager** — configuração WiFi por portal captive (`tzapu/WiFiManager`)
2. **UDP Discovery** — encontra o bridge via broadcast porta 5000
3. **Web Server** — página HTML local + API REST (`/api/state`, etc.)
4. **HTTP REST Client** — comunicação via `POST /api/device/state`, `POST /api/device/register`, `GET /api/device/commands`
5. **Device Registration** — registra via `POST /api/device/register`
6. **State publish** — publica estado via `POST /api/device/state`
7. **Command polling** — busca comandos via `GET /api/device/commands?id=...`
8. **Button interrupt** — (se tiver botão) com debounce de 300ms

### Platformio.ini — Dependências padrão:

```ini
[env:esp8266]
platform = espressif8266
board = d1_mini
framework = arduino
monitor_speed = 115200
lib_deps =
    bblanchon/ArduinoJson@^7.3.1
    tzapu/WiFiManager@^2.0.0
    adafruit/DHT sensor library@^1.4.6   # se for DHT
```

### c_cpp_properties.json — SEMPRE incluir defines para IntelliSense:

```json
"defines": [
    "DEVICE_TYPE_<TIPO>=<N>",
    "DEVICE_TYPE=<N>",
    ...
]
```

## Endpoints da API REST (ESP32 Bridge)

| Method | Path | Descrição |
|--------|------|-----------|
| POST | `/api/device/register` | `{"id":"...", "type":"onoff", "name":"..."}` |
| POST | `/api/device/state` | `{"id":"...", "on":true}` |
| GET | `/api/device/commands?id=...` | Polling de comandos pendentes |
| POST | `/api/device/commands` | `{"id":"..."}` body para polling |
| GET | `/api/device/info?id=...` | Info do dispositivo |
| GET | `/api/devices` | Lista todos dispositivos |

## Tipos de Dispositivo Mapeados para Matter

| type string | device_type_t | Matter ID | Clusters |
|---|---|---|---|
| `onoff` | DEVICE_TYPE_ON_OFF | 0x0100 | OnOff |
| `dimmable` | DEVICE_TYPE_DIMMABLE | 0x0101 | OnOff + LevelControl |
| `temperature` | DEVICE_TYPE_TEMPERATURE_SENSOR | 0x0302 | TemperatureMeasurement |
| `humidity` | DEVICE_TYPE_HUMIDITY_SENSOR | 0x0307 | RelativeHumidityMeasurement |
| `contact` | DEVICE_TYPE_CONTACT_SENSOR | 0x0015 | BooleanState |
| `occupancy` | DEVICE_TYPE_OCCUPANCY_SENSOR | 0x0107 | OccupancySensing |
| `light_sensor` | DEVICE_TYPE_LIGHT_SENSOR | 0x0106 | IlluminanceMeasurement |
| `tanque` | DEVICE_TYPE_TANQUE | 0 (data-only) | Nenhum |

## Build System (ESP32)

### Comandos:
```bash
idf.py build            # Compilar
idf.py monitor          # Serial monitor
idf.py size             # Verificar uso de memória
idf.py size-components  # Detalhado por componente
```

### Config:
- `sdkconfig` gerado pelo menuconfig (não commitar)
- `sdkconfig.defaults` = configuração base do projeto (commitar)
- CMakeLists.txt já força `-Os` nas flags CXX

## Fluxo de Funcionamento

1. ESP32 inicia → WiFi → servidor HTTP → node Matter com Aggregator
2. ESP8266 conecta WiFi → descobre bridge via UDP → registra via HTTP REST (`POST /api/device/register`)
3. ESP8266 publica estado periodicamente via `POST /api/device/state`
4. ESP32 recebe via bridge → atualiza atributos Matter
5. Alexa envia comando Matter → callback no ESP32 → comando enfileirado
6. ESP8266 faz polling via `GET /api/device/commands` → executa comando

## Decisões de Design

- **HTTP REST** (não MQTT): comunicação direta via HTTP, sem broker externo
- **UDP Discovery**: ESP8266 descobre o bridge automaticamente sem config endereço
- **Polling de comandos**: ESP8266 busca comandos a cada 100ms (simples, sem WebSocket)
- **Device Registry estático**: array fixo com mutex, sem alocação dinâmica para devices (evita fragmentação)
- **Números para DEVICE_TYPE** (não strings): permite `#if` do pré-processador, IntelliSense precisa de defines auxiliares

## Próximos Passos (Etapa 6)

- [ ] Commissioning Matter na Alexa
- [ ] Testes de controle de voz
- [ ] Testar com Chip-Tool: `chip-tool onoff on 0x1234 2`
- [ ] Validar DHT11 no hardware (cliente esp8266_dh11)

## Regras Gerais para Sessão IA

1. SEMPRE ler o arquivo relevante com Read antes de editar
2. SEMPRE verificar AGENTS.md para contexto antes de começar
3. NUNCA adicionar arrays grandes em BSS estático
4. SEMPRE adicionar defines no `c_cpp_properties.json` para evitar falso cinza no IntelliSense
5. Para novos clientes ESP8266, SEMPRE replicar a estrutura de `esp8266_on_off`
6. NUNCA usar `strcmp()` em `#if` (não funciona no pré-processador)
7. (ignorar) SEMPRE executar `idf.py size` após mudanças que afetam memória
8. Usar python para escrever testes na pasta ./test
9. led interno: se o wifi nao estiver ativo, piscar mais rápido, se nao estiver falando com o bridger piscar mais lento, se estiver tudo normal não piscar (200, 2000). Se estiver aguardando configuração manter ligado, sem piscar.
10. **CRÍTICO: NUNCA fazer HTTP POST síncrono dentro de handler do web server.** Chamar `send_state()` (ou qualquer `http_post`) dentro de `handle_set_onoff()` bloqueia o servidor web — a página fica travada até o bridge responder (500ms~10s). Sempre usar flag `s_pending_state_sync` + processar no `loop()`.
11. paginas e html devem ficar em arquivos separados
12. mostrar o tempo que esta ativo no dashboard para avaliação visual sobre reset não previstos
13. preferir usar classes de controles em arquivos dedicados, evitar miturar tudo no mesmo arquivo
14. sempre que possivel o dispositivo deve ter uma dashboard proprio
15. sempre perguntar se deseja executar build, e se for executar sempre usar cached
16. a comunicação com o bridge não pode ser bloqueante
17. quando for criar ou ajustar dashboard web, reutilizar a paleta leve inspirada no Dashforge: fundo `#f4f7fc`/`#f8fbff`, surface `#ffffff`, surface-2 `#f9fbff`, texto `#24324a`, muted `#7a8ba3`, primary `#3498db`, primary-strong `#2d7dff`, border `#e6edf7`, success `#2e7d32`, danger `#c62828`; manter o CSS em arquivo separado e evitar incluir o template completo
```sh

```
