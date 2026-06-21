#pragma once

static const char PAGE_DASHBOARD[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Sensor de Presenca</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:system-ui,-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#010102;color:#f7f8f8;min-height:100vh;display:flex;align-items:center;justify-content:center}
.card{background:#0f1011;border-radius:12px;padding:24px;max-width:360px;width:90%;text-align:center;border:1px solid #23252a}
h1{font-size:1.3rem;margin-bottom:4px;color:#5e6ad2;font-weight:700}
.sub{color:#8a8f98;font-size:.85rem;margin-bottom:20px}
.badge{display:inline-block;padding:8px 24px;border-radius:999px;font-size:1.2rem;font-weight:700;text-transform:uppercase;letter-spacing:.04em;margin-bottom:12px}
.badge.motion{background:rgba(245,166,35,0.15);color:#f5a623}
.badge.idle{background:rgba(39,166,68,0.15);color:#27a644}
.icon{font-size:4rem;margin:8px 0 16px 0}
.meta{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:20px}
.meta-item{background:#141516;border-radius:10px;padding:12px;border:1px solid #23252a}
.meta-label{font-size:.75rem;color:#8a8f98;text-transform:uppercase;letter-spacing:.04em}
.meta-value{font-size:1rem;font-weight:600;margin-top:4px;color:#f7f8f8}
.footer{margin-top:20px;color:#62666d;font-size:.8rem}
</style>
</head>
<body>
<div class="card">
<h1>Sensor de Presenca</h1>
<div class="sub" id="sub">carregando...</div>
<div id="status">
<div class="icon" id="icon">&#128064;</div>
<div class="badge" id="badge">—</div>
</div>
<div class="meta">
<div class="meta-item"><div class="meta-label">Device</div><div class="meta-value" id="dev-id">—</div></div>
<div class="meta-item"><div class="meta-label">Bridge</div><div class="meta-value" id="bridge-status">—</div></div>
<div class="meta-item"><div class="meta-label">RSSI</div><div class="meta-value" id="rssi">—</div></div>
<div class="meta-item"><div class="meta-label">Uptime</div><div class="meta-value" id="uptime">—</div></div>
</div>
<div class="footer" id="footer"></div>
</div>
<script>
async function load(){
try{
let r=await fetch('/api/state');
let d=await r.json();
let occ=d.occupancy||false;
let badge=document.getElementById('badge');
let icon=document.getElementById('icon');
if(occ){badge.textContent='MOVIMENTO';badge.className='badge motion';icon.textContent='&#128373;';}
else{badge.textContent='LIVRE';badge.className='badge idle';icon.textContent='&#128064;';}
document.getElementById('dev-id').textContent=d.device_id||'—';
document.getElementById('bridge-status').innerHTML=d.bridge_connected?'<a href="http://'+d.bridge_ip+':'+d.bridge_port+'" target="_blank" style="color:#5e6ad2;text-decoration:none">conectado</a>':'desconectado';
document.getElementById('rssi').textContent=d.rssi+' dBm';
let up=d.uptime_s||0;
document.getElementById('uptime').textContent=Math.floor(up/3600)+'h'+Math.floor((up%3600)/60)+'m';
document.getElementById('sub').textContent='IP: '+(d.ip||'—');
let ls=d.last_detection_s;let lastDet=ls==null?'nunca':ls<60?ls+'s':ls<3600?Math.floor(ls/60)+'m':Math.floor(ls/3600)+'h';
document.getElementById('footer').innerHTML=lastDet?'Ultima deteccao: '+lastDet:'';
}catch(e){document.getElementById('badge').textContent='ERRO';}
}
load();setInterval(load,3000);
</script>
</body>
</html>
)=====";
