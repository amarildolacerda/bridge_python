# ESP32 Bridge + ESP8266 Clients — Projeto

## Branches
- `main` — estável, usado nos dispositivos em produção
- `dev` — desenvolvimento
- `main-v0.0.3` — backup do main anterior (antes do reset para dev)

## Build
1. `source config.sh` carrega IDF v5.5.4 + RMAKER_PATH
2. `idf.py build` compila bridge ESP32 (usa ccache automaticamente)
3. Clientes ESP8266: compilar separadamente (Arduino IDE / PlatformIO)

## Scripts
- `build.sh` — só build
- `flash.sh` — source + build + flash em `/dev/ttyUSB0`
- `monitor.sh` — source + monitor (saída: `Ctrl+]`)
- `erase.sh` — source + erase-flash

## Arquitetura
- Bridge (ESP32/IDF): servidor HTTP REST + RainMaker + discovery UDP
- Clients (ESP8266/Arduino): sensores/atuadores que se registram no bridge via HTTP
- Discovery UDP: broadcast porta 5000, service name `"esp-bridge"`

## Regras importantes
1. Device ID é dinâmico (`esp8266_<chip_id>`), não configurável
2. Device name configurável via WiFiManager, salvo em EEPROM com validação (> 32, < 127)
3. BRIDGE_HOST = "0.0.0.0" força discovery UDP (sem fallback fixo)
4. Sempre copiar cJSON `valuestring` para buffer local com `strncpy` antes de `cJSON_Delete`
5. Retry de registro no `loop()`, não só no `setup()`
6. `CONFIG_LWIP_MAX_SOCKETS` precisa ser aumentado se aparecer `ENFILE`
7. Persistir devices bridgeados em NVS para restaurar no boot
8. Clients enviam `bridge_connected` no `/api/state`
