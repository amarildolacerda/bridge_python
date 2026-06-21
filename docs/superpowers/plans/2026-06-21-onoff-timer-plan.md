# Timer Auto-Off Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add configurable auto-off timer to the ESP8266 onoff client

**Architecture:** Timer runs locally on ESP8266. Dropdown UI in local dashboard (`pages.h`) sets minutes. Timer starts on any ON command, resets on new ON, triggers OFF on expiry. Polling every 15s for countdown display.

**Tech Stack:** ESP8266 Arduino, ESP8266WebServer, ArduinoJson, PROGMEM HTML/JS

## Global Constraints

- Timer state is volatile (RAM only) — resets on reboot
- Timer only active when `s_timer_minutes > 0` and device is ON
- Timer values: Off (0), 5, 15, 30, 60, 120, 240, 480 minutes, plus custom
- `POST /api/timer` with minutes > 0 when device is OFF → turns ON + starts timer
- `POST /api/timer` with minutes = 0 → disables timer (no auto-off)
- Any ON (from any source) restarts timer if minutes > 0

---
### Files

- **Modify:** `clients/esp8266_on_off/src/main.cpp` — timer state vars, timer logic in loop(), new `/api/timer` handler, timer fields in `/api/state`, timer renew on all ON paths
- **Modify:** `clients/esp8266_on_off/include/pages.h` — dropdown selector, custom input, countdown display, 15s polling

### Task 1: Timer logic in main.cpp

**Files:**
- Modify: `clients/esp8266_on_off/src/main.cpp`

**Interfaces:**
- Consumes: existing `s_onoff_state`, `RELAY_PIN`, `s_server`, `send_state()`, `s_pending_state_sync`
- Produces: global variables `s_timer_minutes` (uint16_t), `s_timer_start_ms` (unsigned long), `s_timer_active` (bool); new route `/api/timer`; timer renew called from all ON paths; timer expiry calls `send_state(true)`

- [ ] **Step 1: Add timer global variables** after line 49 (`static bool s_pending_state_sync = false;`)

```c
static uint16_t s_timer_minutes = 0;
static unsigned long s_timer_start_ms = 0;
static bool s_timer_active = false;
```

- [ ] **Step 2: Add timer helper functions** before `handle_set_onoff()` (before line 582)

```c
static void timer_renew(void)
{
    if (s_timer_minutes > 0) {
        s_timer_start_ms = millis();
        s_timer_active = true;
    }
}

static void timer_cancel(void)
{
    s_timer_active = false;
}
```

- [ ] **Step 3: Modify handle_set_onoff()** to call `timer_renew()` on ON

Find `handle_set_onoff` function (line 582). After `s_pending_state_sync = true;` (line 590), add:

```c
    if (s_onoff_state)
        timer_renew();
    else
        timer_cancel();
```

So the function becomes:

```c
static void handle_set_onoff(bool new_state)
{
    s_onoff_state = new_state;
#ifdef RELAY_PIN
    digitalWrite(RELAY_PIN, s_onoff_state ? HIGH : LOW);
#endif
    Serial.printf("[%s] Web/REST: %s\n", TAG, s_onoff_state ? "ON" : "OFF");

    s_pending_state_sync = true;

    if (s_onoff_state)
        timer_renew();
    else
        timer_cancel();

    String json;
    {
        JsonDocument doc;
        doc["status"] = "ok";
        doc["state"] = s_onoff_state;
        serializeJson(doc, json);
    }
    s_server.send(200, "application/json", json);
}
```

- [ ] **Step 4: Add timer renew to poll_commands() ON path**

In `poll_commands()` (line 277-284), after `send_state(true);` when command is `set_onoff` with data=1, add:

```c
                if (s_onoff_state)
                    timer_renew();
                else
                    timer_cancel();
```

So the block becomes:

```c
            if (strcmp(cluster, "onoff") == 0 && strcmp(command, "set_onoff") == 0)
            {
                s_onoff_state = (strcmp(data, "1") == 0 || strcmp(data, "true") == 0);
#ifdef RELAY_PIN
                digitalWrite(RELAY_PIN, s_onoff_state ? HIGH : LOW);
#endif
                if (s_onoff_state)
                    timer_renew();
                else
                    timer_cancel();
                send_state(true);
            }
```

- [ ] **Step 5: Add timer renew to button handler in loop()**

In `loop()`, the button press block (lines 844-853), after `digitalWrite(...)`, add:

```c
        if (s_onoff_state)
            timer_renew();
        else
            timer_cancel();
```

So the block becomes:

```c
#ifdef BUTTON_PIN
    if (s_button_pressed)
    {
        s_button_pressed = false;
        s_onoff_state = !s_onoff_state;
#ifdef RELAY_PIN
        digitalWrite(RELAY_PIN, s_onoff_state ? HIGH : LOW);
#endif
        if (s_onoff_state)
            timer_renew();
        else
            timer_cancel();
        send_state(true);
    }
#endif
```

- [ ] **Step 6: Add timer renew to serial ON command**

In `handle_serial()`, the 'o' case (lines 641-651), after `digitalWrite(RELAY_PIN, HIGH);` and before `send_state(true);`, add:

```c
        timer_renew();
```

And in the 'f' case (lines 653-663), after `digitalWrite(RELAY_PIN, LOW);` and before `send_state(true);`, add:

```c
        timer_cancel();
```

And in the 't' case (lines 666-674), after updating state, add:

```c
        if (s_onoff_state)
            timer_renew();
        else
            timer_cancel();
```

- [ ] **Step 7: Add timer expiry check in loop()**

In `loop()`, before the LED section (before line 868), add:

```c
    if (s_timer_active && s_onoff_state)
    {
        unsigned long elapsed = millis() - s_timer_start_ms;
        if (elapsed >= (unsigned long)s_timer_minutes * 60000UL)
        {
            s_onoff_state = false;
#ifdef RELAY_PIN
            digitalWrite(RELAY_PIN, LOW);
#endif
            s_timer_active = false;
            Serial.printf("[%s] Timer auto-off\n", TAG);
            send_state(true);
        }
    }
```

- [ ] **Step 8: Add timer fields to GET /api/state**

In `handle_api_state()` (lines 610-626), after `doc["bridge_connected"] = s_bridge_connected;`, add:

```c
        doc["timer"] = s_timer_minutes;
        if (s_timer_active)
            doc["timer_remaining_s"] = (s_timer_minutes * 60) - ((millis() - s_timer_start_ms) / 1000);
        else
            doc["timer_remaining_s"] = 0;
```

- [ ] **Step 9: Add POST /api/timer handler**

Add new handler function before `handle_api_on()` (before line 602):

```c
static void handle_api_timer(void)
{
    if (!s_server.hasArg("plain"))
    {
        s_server.send(400, "application/json", "{\"status\":\"error\",\"msg\":\"no body\"}");
        return;
    }

    String body = s_server.arg("plain");
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);
    if (error || !doc.containsKey("minutes"))
    {
        s_server.send(400, "application/json", "{\"status\":\"error\",\"msg\":\"invalid json\"}");
        return;
    }

    int mins = doc["minutes"].as<int>();
    if (mins < 0) mins = 0;
    if (mins > 1440) mins = 1440;  // max 24h

    s_timer_minutes = (uint16_t)mins;

    if (mins > 0)
    {
        if (!s_onoff_state)
        {
            s_onoff_state = true;
#ifdef RELAY_PIN
            digitalWrite(RELAY_PIN, HIGH);
#endif
            Serial.printf("[%s] Timer set: %d min, turning ON\n", TAG, mins);
        }
        else
        {
            Serial.printf("[%s] Timer set: %d min, renewing\n", TAG, mins);
        }
        timer_renew();
        send_state(true);
    }
    else
    {
        timer_cancel();
        Serial.printf("[%s] Timer disabled\n", TAG);
    }

    String json;
    {
        JsonDocument resp;
        resp["status"] = "ok";
        resp["timer"] = s_timer_minutes;
        serializeJson(resp, json);
    }
    s_server.send(200, "application/json", json);
}
```

- [ ] **Step 10: Register `/api/timer` route in setup()**

In `setup()`, after `s_server.on("/api/toggle", HTTP_POST, handle_api_toggle);` (line 778), add:

```c
    s_server.on("/api/timer", HTTP_POST, handle_api_timer);
```

- [ ] **Step 11: Add timer info to serial status**

In `handle_serial()`, the 's' case (lines 676-691), after the bridge line, add:

```c
        if (s_timer_active)
        {
            unsigned long remaining = (s_timer_minutes * 60) - ((millis() - s_timer_start_ms) / 1000);
            Serial.printf("  Timer:       %u min (%lu s remaining)\n", s_timer_minutes, remaining);
        }
        else if (s_timer_minutes > 0)
        {
            Serial.printf("  Timer:       %u min (awaiting ON)\n", s_timer_minutes);
        }
```

- [ ] **Step 12: Add timer to send_state()**

In `send_state()`, after `doc["id"] = s_device_id;` (line 190), add:

```c
            doc["timer"] = s_timer_minutes;
```

This adds the timer config to the bridge state updates.

- [ ] **Step 13: Build and verify compilation**

Run: `pio run` or `idf.py build` as appropriate for the project

Expected: clean compilation with no errors or warnings

### Task 2: Timer UI in pages.h

**Files:**
- Modify: `clients/esp8266_on_off/include/pages.h`

**Interfaces:**
- Consumes: `GET /api/state` (returns `timer`, `timer_remaining_s`), `POST /api/timer {"minutes":N}`
- Produces: dropdown selector, custom input, countdown display

- [ ] **Step 1: Add timer CSS styles**

In the `<style>` block (before line 24), add:

```css
.timer-section{margin-top:16px;padding-top:12px;border-top:1px solid #23252a}
.timer-select{background:#1a1b1e;color:#f7f8f8;border:1px solid #23252a;border-radius:8px;padding:.4rem .6rem;font-size:.85rem;width:100%;margin-top:6px;outline:none}
.timer-select:focus{border-color:#5e6ad2}
.timer-custom{display:none;margin-top:6px}
.timer-custom input{background:#1a1b1e;color:#f7f8f8;border:1px solid #23252a;border-radius:8px;padding:.4rem .6rem;font-size:.85rem;width:80px;text-align:center;outline:none}
.timer-custom input:focus{border-color:#5e6ad2}
.timer-label{color:#8a8f98;font-size:.8rem;margin-top:4px}
.timer-countdown{color:#5e6ad2;font-size:.9rem;font-weight:600;margin-top:4px}
```

- [ ] **Step 2: Add timer HTML after the buttons div**

After `</div>` closing the buttons div (after line 34), add:

```html
<div class="timer-section">
  <div class="timer-label">Timer desligamento automático</div>
  <select class="timer-select" id="timerSelect" onchange="onTimerChange()">
    <option value="0">Off</option>
    <option value="5">5 min</option>
    <option value="15">15 min</option>
    <option value="30">30 min</option>
    <option value="60">1 hora</option>
    <option value="120">2 horas</option>
    <option value="240">4 horas</option>
    <option value="480">8 horas</option>
    <option value="-1">Personalizado</option>
  </select>
  <div class="timer-custom" id="timerCustom">
    <input type="number" id="timerCustomInput" min="1" max="1440" placeholder="min" onchange="onTimerCustom()">
    <button class="btn btn-accent" style="padding:.3rem .8rem;font-size:.8rem;margin-left:4px" onclick="onTimerCustom()">OK</button>
  </div>
  <div class="timer-countdown" id="timerCountdown"></div>
</div>
```

- [ ] **Step 3: Add timer JavaScript functions**

In the `<script>` block, after `fetchState();setInterval(fetchState,3000)` (line 53), replace that line and add:

```js
async function setTimer(mins){
    try{
        await fetch('/api/timer',{method:'POST',body:JSON.stringify({minutes:parseInt(mins)})});
        await fetchState();
    }catch(e){inf.textContent='Erro ao configurar timer'}
}
function onTimerChange(){
    const sel=document.getElementById('timerSelect');
    const cust=document.getElementById('timerCustom');
    if(sel.value==='-1'){
        cust.style.display='block';
    }else{
        cust.style.display='none';
        setTimer(sel.value);
    }
}
function onTimerCustom(){
    const inp=document.getElementById('timerCustomInput');
    const v=parseInt(inp.value);
    if(v&&v>0)setTimer(v);
}
function fmtCountdown(s){
    if(s<=0)return '';
    const m=Math.floor(s/60),sec=s%60;
    return 'Desliga em '+m.toString().padStart(2,'0')+':'+sec.toString().padStart(2,'0');
}
function updateTimerUI(d){
    const cd=document.getElementById('timerCountdown');
    const sel=document.getElementById('timerSelect');
    if(d.timer_remaining_s>0){
        cd.textContent=fmtCountdown(d.timer_remaining_s);
    }else{
        cd.textContent='';
    }
    if(sel&&d.timer!==undefined){
        const opt=sel.querySelector('option[value="'+d.timer+'"]');
        if(opt)sel.value=d.timer;
        else if(d.timer>0)sel.value='-1';
    }
}
const origFetch=window.fetchState;
window.fetchState=async function(){
    try{
        const r=await fetch('/api/state');const d=await r.json();
        el.textContent=d.state?'\u26A1':'\u25CB';
        el.className='valor-status'+(d.state?' on':' off');
        lb.textContent=d.state?'LIGADO':'DESLIGADO';
        let u=d.uptime_s|0,upt=Math.floor(u/86400)+'d '+Math.floor((u%86400)/3600)+'h '+Math.floor((u%3600)/60)+'m '+u%60+'s';
        let ls=d.last_send_s;let lastSend=ls==null?'nunca':ls<60?ls+'s':ls<3600?Math.floor(ls/60)+'m':Math.floor(ls/3600)+'h';
        inf.innerHTML='IP: '+d.ip+' | RSSI: '+d.rssi+'dBm<br>Up: '+upt+' | Ultima coleta: '+lastSend;
        updateTimerUI(d);
    }catch{el.textContent='\u26A0';el.className='valor-status off';lb.textContent='Erro de conex\u00E3o'}
};
fetchState();setInterval(fetchState,15000)
```

- [ ] **Step 4: Build and verify compilation**

Run: `pio run`

Expected: clean compilation with no errors or warnings
