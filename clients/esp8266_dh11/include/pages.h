#pragma once

static const char PAGE_DASHBOARD[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP8266 DHT11</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:system-ui,-apple-system,sans-serif;background:#0f172a;color:#e2e8f0;display:flex;justify-content:center;align-items:center;min-height:100vh}
.card{background:#1e293b;border-radius:12px;padding:24px;text-align:center;max-width:360px;width:90%}
h1{font-size:1.3rem;color:#38bdf8;margin-bottom:12px}
.valor-sensor{font-size:2.5rem;margin:.25rem 0;color:#38bdf8}
.label{color:#64748b;font-size:.85rem;margin-bottom:4px}
.info{color:#475569;font-size:.8rem;margin-top:16px;word-break:break-all}
</style>
</head>
<body>
<div class="card">
<h1>Temperatura Sala</h1>
<div class="label">Temperatura</div>
<div class="valor-sensor" id="temp">--.-°C</div>
<div class="label">Umidade</div>
<div class="valor-sensor" id="humid">--.-%</div>
<div class="info" id="info"></div>
</div>
<script>
const tempEl=document.getElementById('temp');
const humEl=document.getElementById('humid');
const inf=document.getElementById('info');
async function fetchState(){
try{
const r=await fetch('/api/state');const d=await r.json();
tempEl.textContent=d.temperature.toFixed(1)+'\u00B0C';
humEl.textContent=d.humidity.toFixed(1)+'%';
inf.textContent='IP: '+d.ip+' | RSSI: '+d.rssi+'dBm';
}catch{inf.textContent='Erro de conex\u00E3o'}
}
fetchState();setInterval(fetchState,3000)
</script>
</body>
</html>
)rawliteral";
