#pragma once

static const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>)rawliteral" DASHBOARD_TITLE R"rawliteral(</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:system-ui,-apple-system,sans-serif;background:#0f172a;color:#e2e8f0;padding:20px;max-width:900px;margin:0 auto}
h1{font-size:1.5rem;margin-bottom:4px;color:#38bdf8}
.sub{color:#94a3b8;font-size:.85rem;margin-bottom:20px}
.stats{display:flex;gap:12px;margin-bottom:20px}
.stat{background:#1e293b;border-radius:12px;padding:16px 24px;flex:1;text-align:center}
.stat .num{font-size:2rem;font-weight:700;color:#38bdf8}
.stat .label{font-size:.75rem;color:#64748b;text-transform:uppercase;letter-spacing:.5px}
.refresh{text-align:right;margin-bottom:8px;font-size:.8rem;color:#64748b}
table{width:100%;border-collapse:collapse;background:#1e293b;border-radius:12px;overflow:hidden}
th{background:#334155;padding:12px 16px;text-align:left;font-size:.8rem;text-transform:uppercase;letter-spacing:.5px;color:#94a3b8}
td{padding:12px 16px;border-top:1px solid #334155;font-size:.9rem}
td.id{color:#38bdf8;font-weight:600;font-family:monospace}
td.type{color:#a78bfa}
td.name{color:#e2e8f0}
.badge{display:inline-block;padding:2px 10px;border-radius:99px;font-size:.75rem;font-weight:600}
.badge.on{background:#065f46;color:#34d399}
.badge.off{background:#7f1d1d;color:#fca5a5}
.empty{text-align:center;padding:40px 16px;color:#64748b}
</style>
</head>
<body>
<h1>📡 )rawliteral" DASHBOARD_TITLE R"rawliteral(</h1>
<div class="sub">Clientes MQTT vistos nos &uacute;ltimos 5 minutos</div>
<div class="stats">
<div class="stat"><div class="num" id="total">—</div><div class="label">Total</div></div>
<div class="stat"><div class="num" id="online">—</div><div class="label">Online</div></div>
<div class="stat"><div class="num" id="offline">—</div><div class="label">Offline</div></div>
</div>
<div class="refresh">Atualizado: <span id="ts">carregando...</span></div>
<table>
<thead><tr><th>ID</th><th>Tipo</th><th>Nome</th><th>Status</th><th>Visto</th></tr></thead>
<tbody id="tbody"></tbody>
</table>
<script>
async function load(){
try{
const r=await fetch('/api/devices');
const d=await r.json();
let h='',tot=d.length,on=d.filter(x=>x.online).length,off=tot-on;
document.getElementById('total').textContent=tot;
document.getElementById('online').textContent=on;
document.getElementById('offline').textContent=off;
if(tot===0){h='<tr><td colspan="5" class="empty">Nenhum cliente nos &uacute;ltimos 5 min</td></tr>';}
else{
d.forEach(x=>{
const ago=x.last_seen_sec<60?x.last_seen_sec+'s':Math.floor(x.last_seen_sec/60)+'m '+x.last_seen_sec%60+'s';
const st=x.online?'<span class="badge on">Online</span>':'<span class="badge off">Offline</span>';
h+='<tr><td class="id">'+x.id+'</td><td class="type">'+x.type+'</td><td class="name">'+x.name+'</td><td>'+st+'</td><td>'+ago+' atr&aacute;s</td></tr>';
});}
document.getElementById('tbody').innerHTML=h;
document.getElementById('ts').textContent=new Date().toLocaleTimeString();
}catch(e){document.getElementById('ts').textContent='Erro de conex\u00E3o'}
}
load();
setInterval(load,3000);
</script>
</body>
</html>
)rawliteral";
