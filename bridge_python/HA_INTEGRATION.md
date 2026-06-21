# Integração com Home Assistant

## Visão Geral

O bridge se conecta ao Mosquitto e publica entidades no formato **MQTT Discovery**. O Home Assistant descobre automaticamente cada sensor/atuador — sem configurar nada manualmente.

## Instalação via Docker Compose

Se você usa HA + Mosquitto como containers Docker:

```yaml
# docker-compose.yml
services:
  mosquitto:
    image: eclipse-mosquitto:2
    network_mode: host

  homeassistant:
    image: ghcr.io/home-assistant/home-assistant:stable
    network_mode: host
    volumes:
      - ./config:/config

  bridge:
    build: ./bridge_python
    network_mode: host
    environment:
      MQTT_HOST: localhost
      MQTT_PORT: 1883
```

Todos os containers em `network_mode: host` se enxergam via `localhost`.

### Configuração do HA

Em `configuration.yaml`:

```yaml
mqtt:
  sensor: []
  binary_sensor: []
```

Não use `broker:`/`port:` — a conexão com o Mosquitto é configurada pela interface do HA:
**Settings → Devices & Services → Add Integration → MQTT**, informando `localhost:1883`.

## Verificando o Funcionamento

1. **Logs do bridge**: devem mostrar `"Published MQTT discovery for <device_id>"`
2. **HA → Settings → Devices & Services → MQTT**: os dispositivos aparecem como "ESP-HA Bridge"
3. **Painel web**: `http://<ip_do_bridge>:80/`

## MQTT Discovery

O bridge publica:

```
homeassistant/<sensor|binary_sensor>/<device_id>/<entity>/config   → configuração
homeassistant/<sensor|binary_sensor>/<device_id>/<entity>/state     → valor atual
```

O HA lê o `config` e cria a entidade automaticamente.

### Device Types × Entidades

| Tipo | Entidade(s) no HA |
|------|-------------------|
| `onoff` | switch `power` |
| `dimmable` | light `light` |
| `temperature` | sensor `temperature` (°C) + sensor `humidity` (%) |
| `humidity` | sensor `humidity` (%) |
| `contact` | binary_sensor `contact` (porta) |
| `occupancy` | binary_sensor `occupancy` (presença) |
| `light_sensor` | sensor `light` (lx) |
| `tanque` | sensor `level` (%) |
| `gas` | binary_sensor `alarm` (gás) + sensor `gas_level` (%) |
| `rain` | binary_sensor `rain_digital` + sensor `rain_level` (%) |
| `electricity` | sensor `current` (mA) |

### Comandos (switch/light)

Para `onoff` e `dimmable`, o bridge escuta comandos no tópico `.../set`. Quando você liga/desliga pela UI do HA, o bridge repassa ao ESP8266 via HTTP.

## Instalação como Add-on HA

Se você usa HA com Supervisor (HA OS):

```bash
./install_addon.sh install /addons
```

Depois em **Settings → Add-ons → Supervisor → Add-on Store**, clique nos 3 pontinhos → **Reload**. O add-on "ESP-HA Bridge" aparecerá na lista.

## Solução de Problemas

| Problema | Causa | Solução |
|----------|-------|---------|
| Bridge não conecta ao MQTT | Mosquitto não rodando | Verifique `docker ps` |
| Entidades não aparecem | Integração MQTT não configurada no HA | Settings → Devices & Services → Add MQTT |
| Binary sensor mostra "desconhecido" | Config desatualizada no Mosquitto | Apague o tópico `homeassistant/binary_sensor/.../config` e reinicie o bridge |
| Nenhum device registrado | Clients não descobriram o bridge | Use o botão "Broadcast" no painel web |
