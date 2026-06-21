# Timer Auto-Off para Dispositivo OnOff

## Objetivo
Adicionar timer de desligamento automático no cliente `esp8266_on_off`. Usuário configura um tempo no dashboard local; quando o dispositivo recebe um comando ON, o timer inicia contagem regressiva. Ao expirar, executa OFF. Se um novo ON chega, o timer reinicia. Sem timer configurado, comportamento normal.

## Localização
Dashboard local do ESP8266 (`clients/esp8266_on_off/include/pages.h`), junto aos botões ON/OFF.

## UI do Timer
- Dropdown seletor: **Off / 5min / 15min / 30min / 1h / 2h / 4h / 8h / Custom**
- Ao selecionar "Custom", exibe campo input numérico para digitar minutos
- Quando timer ativo e dispositivo ON, mostra contagem regressiva: `"Desliga em MM:SS"`

## API

### `POST /api/timer`
Body: `{"minutes": N}`
- `N = 0` → desativa timer
- `N > 0` → configura timer; se dispositivo OFF, liga + inicia contagem; se ON, reinicia contagem
- Resposta: `{"status":"ok", "timer": N}`

### `GET /api/state`
Adiciona campo:
- `timer`: minutos configurados (0 = inativo)
- `timer_remaining_s`: segundos restantes (0 se inativo)

## Lógica (main.cpp)

### Variáveis globais
- `s_timer_minutes` (uint16_t, 0 = inativo)
- `s_timer_start_ms` (unsigned long, millis() do último ON)
- `s_timer_active` (bool)

### handle_set_onoff(true)
```c
if (s_timer_minutes > 0) {
    s_timer_start_ms = millis();
    s_timer_active = true;
}
```

### handle_set_onoff(false)
```c
s_timer_active = false;
```

### loop() — a cada iteração
```c
if (s_timer_active && s_onoff_state) {
    unsigned long elapsed = millis() - s_timer_start_ms;
    if (elapsed >= (unsigned long)s_timer_minutes * 60000UL) {
        handle_set_onoff(false);
        s_timer_active = false;
    }
}
```

### POST /api/timer handler
```c
int mins = cJSON_GetObjectItem(body, "minutes")->valueint;
s_timer_minutes = mins;
if (mins > 0 && !s_onoff_state) {
    handle_set_onoff(true);  // liga + inicia timer
} else if (mins > 0 && s_onoff_state) {
    s_timer_start_ms = millis();  // reinicia
    s_timer_active = true;
} else {
    s_timer_active = false;
}
```

### send_state()
Adiciona ao JSON enviado ao bridge: `"timer": s_timer_minutes`

## Dashboard JS (pages.h)
- Dropdown onChange → `POST /api/timer`
- Polling `GET /api/state` a cada 15s
- Se `timer_remaining_s > 0`, exibe `"Desliga em MM:SS"` formatado
- Se `timer_remaining_s == 0`, não exibe contagem

## Comportamento Detalhado
| Ação | Efeito |
|------|--------|
| Timer setado com OFF | Liga dispositivo + inicia contagem |
| Timer setado com ON | Reinicia contagem |
| ON via bridge polling | Reinicia contagem (se timer ativo) |
| ON via botão físico | Reinicia contagem (se timer ativo) |
| ON via dashboard local | Reinicia contagem (se timer ativo) |
| Timer expira | Executa OFF, timer volta a inativo |
| Timer = Off (0) | Nenhum desligamento automático |

## Persistência
Volátil (RAM apenas). Timer reseta ao reiniciar o ESP8266.
