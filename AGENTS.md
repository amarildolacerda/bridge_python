# ESP32 Bridge + ESP8266 Clients — Projeto

## Branches
- `main` — estável, usado nos dispositivos em produção
- `dev` — desenvolvimento
- `main-v0.0.3` — backup do main anterior (antes do reset para dev)

## Ambiente

### Pré-requisitos
- `sudo apt install ccache git curl` — ccache acelera rebuilds
- Python 3.10+ com `venv`

### ESP-IDF (primeira instalação)
```sh
mkdir -p ~/.espressif/v5.5.4
git clone --recursive -b v5.5.4 https://github.com/espressif/esp-idf.git ~/.espressif/v5.5.4/esp-idf
```
> Se o clone falhar por falta de internet, reconecte a rede e execute:
> ```sh
> git -C ~/.espressif/v5.5.4/esp-idf submodule update --init --recursive
> ```

### RainMaker
```sh
git clone --recursive -b v1.8.2 https://github.com/espressif/esp-rainmaker.git ~/esp/esp-rainmaker
```

## Build
1. `source config.sh` carrega IDF v5.5.4 + RMAKER_PATH (ativa ccache automaticamente)
2. `idf.py build` compila bridge ESP32
3. Clientes ESP8266: compilar separadamente (Arduino IDE / PlatformIO)

## Scripts
- `build.sh` — só build
- `flash.sh [-p <port>]` — source + build + flash (porta padrão `/dev/ttyUSB0`)
- `monitor.sh` — source + monitor (saída: `Ctrl+]`)
- `monitor.py` — monitor serial Python, sai com `q` ou `Ctrl+C`
- `erase.sh` — source + erase-flash

## Arquitetura
- Bridge (ESP32/IDF): servidor HTTP REST + RainMaker + discovery UDP + terminal serial + WiFi config portal
- Clients (ESP8266/Arduino): sensores/atuadores que se registram no bridge via HTTP
- Discovery UDP: broadcast porta 5000, service name `"esp-bridge"`

## Terminal do Bridge (console serial)
- `l` — lista devices registrados (com índices numéricos)
- `s` — status do bridge (IP, total devices, uptime)
- `d <id|índice>` — detalhes de um device (aceita ID ou número da lista `l`)
- `b` — broadcast ping (envia `ping:true` via UDP, espera 3s, mostra descobertos + registrados)
- `r` — restart
- `h` / `?` — ajuda
- Usa `getchar()` single-key, prompt `bridge>` só aparece após comando executado

## Provisionamento WiFi
- Bridge usa **SoftAP** (não BLE): quando não há credenciais STA salvas, inicia AP `Bridge_Config` e servidor HTTP em `192.168.4.1`
- Página web permite digitar SSID/senha e salva via `esp_wifi_set_config()`, então reinicia
- Após reinício, RainMaker normal usa as credenciais salvas
- `app_wifi_config.h`/`.cpp` — módulo dedicado (AP + HTTP server + web form)
- `app_main.cpp` — verifica `esp_wifi_get_config(WIFI_IF_STA)` após `app_network_init()`

## Desenvolvimento
- Alterações de código devem ser feitas apenas no branch `dev`. Verifique com `git branch --show-current` antes de começar.
- `main` é estável e usado em produção — nunca commitar diretamente em `main`.

## Regras importantes
1. Device ID é dinâmico (`esp8266_<chip_id>`), não configurável
2. Device name configurável via WiFiManager, salvo em EEPROM com validação (> 32, < 127)
3. BRIDGE_HOST = "0.0.0.0" força discovery UDP (sem fallback fixo)
4. Sempre copiar cJSON `valuestring` para buffer local com `strncpy` antes de `cJSON_Delete`
5. Retry de registro no `loop()`, não só no `setup()`
6. `CONFIG_LWIP_MAX_SOCKETS` precisa ser aumentado se aparecer `ENFILE`
7. Persistir devices bridgeados em NVS para restaurar no boot
8. Clients enviam `bridge_connected` no `/api/state`
9. DHT21 client: GPIO 5, tipo DHT21, fallback `isnan()` não envia ao bridge (flag `s_dht_valid`)
10. Clients respondem a `ping:true` no UDP enviando `{"discover":true,"id":"..."}` de volta
11. Bridge broadcast (`b`): envia `ping:true` via UDP, mostra IPs descobertos + devices registrados
12. Dashboard web tem card QR code do RainMaker em `/api/qrcode`
13. economizar tokens com respostas minimas sem explicações desnecessaria 
14. manter skills enxutas
