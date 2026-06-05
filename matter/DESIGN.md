# DESIGN — Padrão de Páginas HTML para Clientes ESP8266

## Filosofia

Cada byte conta (flash + RAM), mas o usuário merece uma interface limpa.
Este documento define um sistema de design único que equilibra **leveza**
(~4-6 KB por página inline) com **acabamento visual consistente**.

---

## 1. Paleta de Cores (Sistema Design)

```css
/* Tema claro pastel — base Azul Bebê (#B2CEfE) */
--bg:         #F4F7FC;   /* Azul Bebê claro  — fundo da página */
--bg-card:    #FFFFFF;   /* Branco            — fundo do card */
--bg-hover:   #E8EEF8;   /* Azul Bebê hover   — hover em linhas/btns */
--text:       #2C3E50;   /* Azul escuro       — texto principal */
--text-dim:   #7A8BA3;   /* Cinza azulado     — labels, metadados */
--text-muted: #8FA0B8;   /* Cinza azulado+    — info secundária */

--accent:     #B2CEfE;   /* Azul Bebê         — títulos, links, bordas ativas */
--accent-alt: #8BAEF8;   /* Azul Bebê profundo— variante bridge */

--success:    #2E7D32;   /* Verde escuro      — texto badge on */
--success-bg: #E8F5E9;   /* Verde pastel      — badge fundo on */
--danger:     #C62828;   /* Vermelho escuro   — texto badge off */
--danger-bg:  #FFEBEE;   /* Vermelho pastel   — badge fundo off */

--warning:    #F5C542;   /* Amarelo pastel    — alertas */
--purple:     #9FA8DA;   /* Violeta pastel    — tipo do dispositivo */
```

**Regras:**
- Não usar cores CSS customizadas por página — sempre reutilizar as variantes acima.
- Nunca usar `color: white` ou `color: black` — usar os tokens `--text` e `--bg`.
- Badge ON/OFF: usar `--success-bg`/`--danger-bg` como fundo e `--success`/`--danger` como texto.

---

## 2. CSS Base (Obrigatório em toda página)

```css
*{margin:0;padding:0;box-sizing:border-box}
body{
  font-family:system-ui,-apple-system,sans-serif;
  background:var(--bg,#F4F7FC);
  color:var(--text,#2C3E50);
}
```

- **Font:** `system-ui,-apple-system,sans-serif` (sem webfonts — zero bytes extras)
- **Box-sizing:** `border-box` sempre
- **Breakpoints:** Nenhum — design é mobile-first com `max-width` no container

---

## 3. Layout

### Container principal

```css
.container{max-width:400px;margin:0 auto;padding:16px}
/* Dashboard multi-dispositivo pode usar até 900px */
.container-wide{max-width:900px;margin:0 auto;padding:16px}
```

### Card

```css
.card{
  background:var(--bg-card,#FFFFFF);
  border-radius:12px;
  padding:16px;
  margin-bottom:12px;
}
```

### Grid de cards (multi-dispositivo)

```css
.grid{display:flex;flex-direction:column;gap:12px}
/* Em dispositivos >600px */
@media(min-width:600px){.grid{flex-direction:row;flex-wrap:wrap}}
@media(min-width:600px){.grid>*{flex:1;min-width:200px}}
```

---

## 4. Componentes

### 4.1 Badge de Status

```css
.badge{
  display:inline-block;
  padding:2px 10px;
  border-radius:99px;
  font-size:.7rem;
  font-weight:600;
}
.badge.on{background:var(--success-bg,#E8F5E9);color:var(--success,#2E7D32)}
.badge.off{background:var(--danger-bg,#FFEBEE);color:var(--danger,#C62828)}
```

### 4.2 Botões de Controle

```css
.btn{
  border:none;border-radius:10px;
  padding:.65rem 1.25rem;
  font-size:.9rem;font-weight:600;
  cursor:pointer;transition:opacity .15s;
  color:#fff;min-width:80px;
  -webkit-tap-highlight-color:transparent;
}
.btn:active{opacity:.7}
.btn-on{background:var(--success,#2E7D32)}
.btn-off{background:var(--danger,#C62828)}
.btn-accent{background:var(--accent,#B2CEfE)}
```

### 4.3 LED Indicador (Status)

```css
.led{
  display:inline-block;width:8px;height:8px;
  border-radius:50%;margin-right:6px;
  vertical-align:middle;
}
.led.on{background:var(--success,#2E7D32);box-shadow:0 0 6px var(--success,#2E7D32)}
.led.off{background:var(--text-muted,#8FA0B8)}
```

### 4.4 Estado do Dispositivo (Valor Principal)

```css
/* Para On/Off - valor grande centralizado */
.valor-status{
  font-size:4rem;text-align:center;
  margin:.5rem 0;transition:color .3s;
}
.valor-status.on{color:var(--success,#2E7D32)}
.valor-status.off{color:var(--text-muted,#8FA0B8)}

/* Para sensor numérico */
.valor-sensor{
  font-size:2.5rem;text-align:center;
  margin:.25rem 0;color:var(--accent,#B2CEfE);
}
```

### 4.5 Info Row (Bridge Dashboard)

```css
.row{display:flex;justify-content:space-between;padding:6px 0;font-size:.85rem;border-bottom:1px solid var(--bg-hover,#E8EEF8)}
.row:last-child{border:none}
.row .label{color:var(--text-dim,#7A8BA3)}
.row .value{color:var(--text,#2C3E50)}
```

### 4.6 Stats Cards (Multi-dispositivo)

```css
.stat{
  background:var(--bg-card,#FFFFFF);
  border-radius:12px;padding:12px 20px;
  text-align:center;flex:1;
}
.stat .num{font-size:1.8rem;font-weight:700;color:var(--accent,#B2CEfE)}
.stat .label{font-size:.7rem;color:var(--text-dim,#7A8BA3);text-transform:uppercase;letter-spacing:.5px}
```

### 4.7 Tabela (Multi-dispositivo)

```css
table{width:100%;border-collapse:collapse;background:var(--bg-card,#FFFFFF);border-radius:12px;overflow:hidden}
th{background:var(--bg-hover,#E8EEF8);padding:10px 12px;text-align:left;font-size:.75rem;text-transform:uppercase;letter-spacing:.5px;color:var(--text-dim,#7A8BA3)}
td{padding:10px 12px;border-top:1px solid var(--bg-hover,#E8EEF8);font-size:.82rem}
td.id{color:var(--accent,#B2CEfE);font-weight:600;font-family:monospace}
td.type{color:var(--purple,#9FA8DA)}
```

---

## 5. Tipografia

- **Título da página:** `<h1>` — `1.2rem`, `--accent`
- **Subtítulo:** `--text-dim`, `.8rem`
- **Nome do dispositivo:** `.9rem`, `font-weight:600`, `--text`
- **Metadado (tipo, endpoint):** `.75rem`, `--text-dim`
- **Label de formulário/stat:** `.75rem`, uppercase, `letter-spacing:.5px`

Sem weights customizados — apenas `400` (regular) e `600` (semibold).

---

## 6. JavaScript Padrão

### Padrão de fetch + polling

```js
async function load(){
  try{
    const r=await fetch('/api/state');
    const d=await r.json();
    // atualizar DOM com d
  }catch(e){
    // mostrar erro sem poluir console do usuário
    document.getElementById('status').textContent='Erro de conexão';
  }
}
load();
setInterval(load,3000);
```

### Regras JS:
- **Sempre `async/await`** — sem callbacks, sem promises encadeadas.
- **Sempre `try/catch`** — erro de rede não quebra a página.
- **Polling:** 3s para clientes individuais, 5s para bridge multi-dispositivo.
- **Nunca usar jQuery ou qualquer lib JS** — fetch nativo apenas.
- **IDs curtos** no DOM (ex: `status`, `temp`, `hum`, `info`, `tbody`).

---

## 7. Loading, Empty e Error States

### Loading state

O HTML inicial já deve mostrar um placeholder visual (não空白):

```html
<div class="valor-status" id="status">—</div>
<div class="label" id="label">carregando...</div>
```

### Empty state (lista vazia)

```js
if(!d.length){
  h='<tr><td colspan="5" class="empty">Nenhum dispositivo registrado</td></tr>';
}
```

```css
.empty{padding:40px 16px;text-align:center;color:var(--text-dim,#7A8BA3)}
```

### Error state

```js
catch(e){
  document.getElementById('status').textContent='⚠';
  document.getElementById('label').textContent='Erro de conexão';
}
```

- Nunca usar `alert()`.
- Nunca mostrar `e.message` bruto ao usuário.
- Apenas "Erro de conexão" ou "Erro ao carregar".

---

## 8. LEDs da Placa (Indicadores Físicos)

Conforme regra 9 do AGENTS.md:

| Condição | LED | Comportamento |
|---|---|---|
| Aguardando WiFi (AP mode) | LED_BUILTIN | Ligado fixo (sem piscar) |
| WiFi OK, sem bridge | LED_BUILTIN | Piscar lento (2000ms toggle) |
| WiFi OK, bridge conectado | LED_BUILTIN | Piscar rápido (200ms toggle) |
| Tudo normal | LED_BUILTIN | Apagado |

Implementação sugerida no `loop()`:

```cpp
static unsigned long lastLedToggle = 0;
static bool ledState = false;

if (wifiManager.getConfigPortalActive()) {
    digitalWrite(LED_PIN, HIGH); // fixo
} else if (!bridgeConnected) {
    if (millis() - lastLedToggle > 2000) { // lento
        lastLedToggle = millis();
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState);
    }
} else {
    if (millis() - lastLedToggle > 200) { // rápido
        lastLedToggle = millis();
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState);
    }
}
```

---

## 9. Estrutura de Arquivos

Conforme regra 11 do AGENTS.md, HTML deve ficar em arquivos separados:

```
clients/esp8266_<tipo>/
├── include/
│   ├── config.h           # Defines centralizados
│   ├── pages.h            # HTML inline (raw strings) — ou:
│   └── pages/             # Se houver múltiplas páginas
│       ├── dashboard.html
│       └── config.html
├── src/
│   ├── main.cpp           # Firmware
│   └── web_handlers.cpp   # Handlers HTTP (opcional)
├── platformio.ini
└── test/
```

### Arquivo `include/pages.h` — Template:

```cpp
#pragma once

static const char PAGE_DASHBOARD[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP8266 MeuDevice</title>
<style>
/* === DESIGN SYSTEM === */
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:system-ui,-apple-system,sans-serif;background:#F4F7FC;color:#2C3E50;display:flex;justify-content:center;align-items:center;min-height:100vh}
.card{background:#FFFFFF;border-radius:12px;padding:24px;text-align:center;max-width:360px;width:90%}
h1{font-size:1.3rem;color:#B2CEfE;margin-bottom:4px}
.label{color:#7A8BA3;font-size:.85rem;margin-bottom:16px}
/* === COMPONENTES === */
.valor-status{font-size:4rem;margin:.5rem 0;transition:color .3s}
.valor-status.on{color:#2E7D32}.valor-status.off{color:#8FA0B8}
.btn{border:none;border-radius:10px;padding:.65rem 1.25rem;font-size:.9rem;font-weight:600;cursor:pointer;transition:opacity .15s;color:#fff;min-width:80px;-webkit-tap-highlight-color:transparent}
.btn:active{opacity:.7}
.btn-on{background:#2E7D32}.btn-off{background:#C62828}.btn-accent{background:#B2CEfE}
.buttons{display:flex;gap:8px;justify-content:center;flex-wrap:wrap;margin-top:12px}
.info{color:#8FA0B8;font-size:.8rem;margin-top:16px;word-break:break-all}
</style>
</head>
<body>
<div class="card">
<h1>Meu Device</h1>
<div class="valor-status" id="status">—</div>
<div class="label" id="label">carregando...</div>
<div class="buttons" id="buttons"></div>
<div class="info" id="info"></div>
</div>
<script>
// === CONTROLLER ===
const el=document.getElementById('status');
const lb=document.getElementById('label');
const inf=document.getElementById('info');
const btns=document.getElementById('buttons');

async function fetchState(){
  try{
    const r=await fetch('/api/state');
    const d=await r.json();
    // atualizar conforme tipo — exemplo on/off:
    const on=!!d.state;
    el.textContent=on?'⚡':'○';
    el.className='valor-status'+(on?' on':' off');
    lb.textContent=on?'LIGADO':'DESLIGADO';
    inf.textContent='IP: '+d.ip+' | RSSI: '+d.rssi+'dBm';
  }catch(e){
    el.textContent='⚠';el.className='valor-status off';
    lb.textContent='Erro de conexão';
  }
}

async function setState(cmd){
  try{
    await fetch('/api/'+cmd,{method:'POST'});
    await fetchState();
  }catch(e){
    inf.textContent='Erro ao enviar comando';
  }
}

fetchState();
setInterval(fetchState,3000);
</script>
</body>
</html>
)rawliteral";
```

---

## 10. Checklist de Implementação

| Requisito | Obrigatório? |
|---|---|
| Paleta de cores consistente (tokens acima) | ✅ Sim |
| Viewport meta + mobile-first | ✅ Sim |
| `system-ui` font (sem webfonts) | ✅ Sim |
| `box-sizing: border-box` | ✅ Sim |
| `try/catch` em todo fetch | ✅ Sim |
| Loading state no HTML inicial | ✅ Sim |
| Error state amigável (sem `alert`) | ✅ Sim |
| Polling com `setInterval` | ✅ Sim |
| LED onboard seguindo regra 9 | ✅ Sim |
| HTML em arquivo separado (`include/pages.h`) | ✅ Sim |
| Classes de controle em arquivo dedicado (regra 12) | ✅ Sim |
| Dashboard próprio por dispositivo (regra 13) | ✅ Sim |
| Badges ON/OFF com `border-radius: 99px` | Recomendado |
| Transição `:active` em botões (`opacity`) | Recomendado |
| Shadow sutil em cards (`box-shadow: 0 4px 12px rgba(0,0,0,.2)`) | Opcional |
| Animações/transições CSS | Opcional (< 3 propriedades) |

---

## 11. Regras de Performance (ESP8266)

- **HTML inline total:** manter abaixo de 6 KB por página.
- **CSS inline total:** abaixo de 1.5 KB (minificado manualmente, sem espaços).
- **JS inline total:** abaixo de 1.5 KB (minificado manualmente).
- **Zero dependências externas** — sem CDN, sem Google Fonts, sem ícones externos.
- **Ícones:** usar caracteres Unicode (⚡ ○ ● ◇ ◆ ⟳ ⚠ ✕ ✓) ou CSS puro — nunca font icons.
- **PROGMEM:** todo HTML/CSS/JS estático deve usar `PROGMEM` (`R"rawliteral()rawliteral"` já coloca em flash).

---

## 12. Exemplos por Tipo de Dispositivo

### On/Off (comando) — 3 botões: Ligar, Desligar, Inverter

```
Card centralizado | status: ⚡/○ | label: LIGADO/DESLIGADO | 3 botões | info: IP + RSSI
```

### Sensor (leitura) — sem botões

```
Card centralizado | valor grande: 23.5°C | label: Temperatura | info: IP + RSSI + uptime
```

### Multi-dispositivo (bridge) — tabela ou grid de cards

```
Stats: Total | Online | Offline | Tabela: ID | Tipo | Nome | Status | Visto há
```
