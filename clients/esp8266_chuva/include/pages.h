#pragma once

static const char PAGE_DASHBOARD[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Sensor de Chuva</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#1a1d23;color:#e8eaed;min-height:100vh;display:flex;align-items:center;justify-content:center}
.card{background:#282c34;border-radius:16px;padding:32px;max-width:400px;width:90%;text-align:center;box-shadow:0 8px 32px rgba(0,0,0,.3)}
h1{font-size:1.5rem;margin-bottom:8px;color:#8ab4f8}
.sub{color:#7A8BA3;font-size:.9rem;margin-bottom:24px}
.level{font-size:4rem;font-weight:700;margin:16px 0}
.level.dry{color:#4CAF50}
.level.light{color:#FF9800}
.level.wet{color:#1976D2}
.level.heavy{color:#C62828}
.badge{display:inline-block;padding:4px 16px;border-radius:20px;font-size:.85rem;font-weight:600;margin-bottom:16px}
.badge.dry{background:#1b5e20;color:#81c784}
.badge.light{background:#e65100;color:#ffcc80}
.badge.wet{background:#0d47a1;color:#90caf9}
.badge.heavy{background:#b71c1c;color:#ef9a9a}
.meta{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:24px}
.meta-item{background:#1a1d23;border-radius:12px;padding:12px}
.meta-label{font-size:.75rem;color:#7A8BA3;text-transform:uppercase}
.meta-value{font-size:1.1rem;font-weight:600;margin-top:4px}
.footer{margin-top:24px;color:#7A8BA3;font-size:.8rem}
</style>
</head>
<body>
<div class="card">
<h1>Sensor de Chuva</h1>
<div class="sub" id="sub">carregando...</div>
<div id="status">
<div class="badge" id="badge">—</div>
<div class="level" id="level">—</div>
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
let lvl=d.rain_level||0;
let el=document.getElementById('level');
el.textContent=lvl+'%';
el.className='level';
let badge=document.getElementById('badge');
if(lvl>=90){el.classList.add('dry');badge.textContent='SECO';badge.className='badge dry';}
else if(lvl>=60){el.classList.add('light');badge.textContent='CHUVISCO';badge.className='badge light';}
else if(lvl>=30){el.classList.add('wet');badge.textContent='CHUVENDO';badge.className='badge wet';}
else{badge.textContent='CHUVA FORTE';badge.className='badge heavy';el.classList.add('heavy');}
document.getElementById('dev-id').textContent=d.device_id||'—';
document.getElementById('bridge-status').textContent=d.bridge_connected?'conectado':'desconectado';
document.getElementById('rssi').textContent=d.rssi+' dBm';
let up=d.uptime_s||0;
document.getElementById('uptime').textContent=Math.floor(up/3600)+'h'+Math.floor((up%3600)/60)+'m';
document.getElementById('sub').textContent='IP: '+(d.ip||'—');
 let ls=d.last_send_s;let lastSend=ls==null?'nunca':ls<60?ls+'s':ls<3600?Math.floor(ls/60)+'m':Math.floor(ls/3600)+'h';
 document.getElementById('footer').innerHTML=lastSend?'Ultima coleta: '+lastSend:'';
}catch(e){document.getElementById('level').textContent='ERRO';}
}
load();setInterval(load,5000);
</script>
</body>
</html>
)=====";
