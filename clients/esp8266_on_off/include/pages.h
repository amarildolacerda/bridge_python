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
body{font-family:system-ui,-apple-system,sans-serif;background:#010102;color:#f7f8f8;display:flex;justify-content:center;align-items:center;min-height:100vh}
.card{background:#0f1011;border-radius:12px;padding:24px;text-align:center;max-width:360px;width:90%;border:1px solid #23252a}
h1{font-size:1.3rem;color:#5e6ad2;margin-bottom:4px}
.valor-status{font-size:4rem;margin:.5rem 0;transition:color .3s}
.valor-status.on{color:#27a644}.valor-status.off{color:#62666d}
.label{color:#8a8f98;font-size:.85rem;margin-bottom:12px}
.buttons{display:flex;gap:8px;justify-content:center;flex-wrap:wrap;margin-top:12px}
.btn{border:none;border-radius:10px;padding:.65rem 1.25rem;font-size:.9rem;font-weight:600;cursor:pointer;transition:opacity .15s;color:#fff;min-width:80px;-webkit-tap-highlight-color:transparent}
.btn:active{opacity:.7}
.btn-on{background:#27a644}.btn-off{background:#e5484d}.btn-accent{background:#5e6ad2}
.timer-section{margin-top:16px;padding-top:12px;border-top:1px solid #23252a}
.timer-select{background:#1a1b1e;color:#f7f8f8;border:1px solid #23252a;border-radius:8px;padding:.4rem .6rem;font-size:.85rem;width:100%;margin-top:6px;outline:none}
.timer-select:focus{border-color:#5e6ad2}
.timer-custom{display:none;margin-top:6px}
.timer-custom input{background:#1a1b1e;color:#f7f8f8;border:1px solid #23252a;border-radius:8px;padding:.4rem .6rem;font-size:.85rem;width:80px;text-align:center;outline:none}
.timer-custom input:focus{border-color:#5e6ad2}
.timer-label{color:#8a8f98;font-size:.8rem;margin-top:4px}
.timer-countdown{color:#5e6ad2;font-size:.9rem;font-weight:600;margin-top:4px}
.info{color:#62666d;font-size:.8rem;margin-top:16px;word-break:break-all}
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
    let u=d.uptime_s|0,upt=Math.floor(u/86400)+'d '+Math.floor((u%86400)/3600)+'h '+Math.floor((u%3600)/60)+'m '+u%60+'s';
    let ls=d.last_send_s;let lastSend=ls==null?'nunca':ls<60?ls+'s':ls<3600?Math.floor(ls/60)+'m':Math.floor(ls/3600)+'h';
    inf.innerHTML='IP: '+d.ip+' | RSSI: '+d.rssi+'dBm<br>Up: '+upt+' | Ultima coleta: '+lastSend;
    updateTimerUI(d);
}catch{el.textContent='\u26A0';el.className='valor-status off';lb.textContent='Erro de conex\u00E3o'}
}
async function setState(cmd){try{await fetch('/api/'+cmd,{method:'POST'});await fetchState()}catch{inf.textContent='Erro ao enviar comando'}}
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
fetchState();setInterval(fetchState,15000)
</script>
</body>
</html>
)rawliteral";
