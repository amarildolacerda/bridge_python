#pragma once

static const char PAGE_DASHBOARD[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP8266 On/Off</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:system-ui,-apple-system,sans-serif;background:#0f172a;color:#e2e8f0;display:flex;justify-content:center;align-items:center;min-height:100vh}
.card{background:#1e293b;border-radius:12px;padding:24px;text-align:center;max-width:360px;width:90%}
h1{font-size:1.3rem;color:#38bdf8;margin-bottom:4px}
.valor-status{font-size:4rem;margin:.5rem 0;transition:color .3s}
.valor-status.on{color:#34d399}.valor-status.off{color:#475569}
.label{color:#64748b;font-size:.85rem;margin-bottom:12px}
.buttons{display:flex;gap:8px;justify-content:center;flex-wrap:wrap;margin-top:12px}
.btn{border:none;border-radius:10px;padding:.65rem 1.25rem;font-size:.9rem;font-weight:600;cursor:pointer;transition:opacity .15s;color:#fff;min-width:80px;-webkit-tap-highlight-color:transparent}
.btn:active{opacity:.7}
.btn-on{background:#34d399}.btn-off{background:#f87171}.btn-accent{background:#38bdf8}
.info{color:#475569;font-size:.8rem;margin-top:16px;word-break:break-all}
</style>
</head>
<body>
<div class="card">
<h1>Luz Sala</h1>
<div class="valor-status" id="status">—</div>
<div class="label" id="label">carregando...</div>
<div class="buttons">
<button class="btn btn-on" onclick="setState('on')">Ligar</button>
<button class="btn btn-off" onclick="setState('off')">Desligar</button>
<button class="btn btn-accent" onclick="setState('toggle')">Inverter</button>
</div>
<div class="info" id="info"></div>
</div>
<script>
const el=document.getElementById('status');
const lb=document.getElementById('label');
const inf=document.getElementById('info');
async function fetchState(){
try{
const r=await fetch('/api/state');const d=await r.json();
el.textContent=d.state?'\u26A1':'\u25CB';
el.className='valor-status'+(d.state?' on':' off');
lb.textContent=d.state?'LIGADO':'DESLIGADO';
inf.textContent='IP: '+d.ip+' | RSSI: '+d.rssi+'dBm';
}catch{el.textContent='\u26A0';el.className='valor-status off';lb.textContent='Erro de conex\u00E3o'}
}
async function setState(cmd){try{await fetch('/api/'+cmd,{method:'POST'});await fetchState()}catch{inf.textContent='Erro ao enviar comando'}}
fetchState();setInterval(fetchState,3000)
</script>
</body>
</html>
)rawliteral";
