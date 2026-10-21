// Pagina de control servida por el ESP32 en http://<ip>/
// El APK Android solo abre esta pagina en un WebView: la UI viaja con el firmware.
#pragma once

static const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="es"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no,viewport-fit=cover">
<title>ZYC0076</title>
<style>
*{box-sizing:border-box;-webkit-user-select:none;user-select:none;-webkit-tap-highlight-color:transparent}
html,body{margin:0;height:100%;background:#101214;color:#eee;font:14px system-ui,sans-serif;overflow:hidden;touch-action:none}
#wrap{display:flex;flex-direction:column;height:100%}
#top{position:relative;background:#000;flex:0 0 44vh;display:flex;align-items:center;justify-content:center;overflow:hidden}
#v{max-width:100%;max-height:100%;display:block}
#hud{position:absolute;top:6px;left:8px;font-size:12px;color:#3f6;text-shadow:0 0 4px #000}
#ver{position:absolute;top:6px;left:50%;transform:translateX(-50%);font-size:11px;color:#667}
#dst{position:absolute;top:22px;left:50%;transform:translateX(-50%);font-size:13px;color:#8a9199;background:rgba(0,0,0,.45);padding:2px 8px;border-radius:6px}
#net{position:absolute;top:48px;left:50%;transform:translateX(-50%);font-size:11px;color:#6b747c}
#cl{position:absolute;bottom:8px;right:8px;padding:10px 14px;border:0;border-radius:8px;background:#2a7;color:#041;font-weight:700}
#led{position:absolute;bottom:8px;left:8px;padding:10px 14px;border:0;border-radius:8px;background:#31373b;color:#eee;font-weight:700}
#led.on{background:#fc3;color:#420}
#slp{position:absolute;top:6px;right:8px;padding:10px 14px;border:0;border-radius:8px;background:#31373b;color:#eee;font-weight:700}
#ctl{flex:1;display:flex;align-items:center;justify-content:space-between;padding:10px 14px;gap:8px}
.stick{position:relative;width:126px;height:126px;border-radius:50%;background:radial-gradient(#232629,#171a1c);border:2px solid #33383c}
.knob{position:absolute;left:50%;top:50%;width:52px;height:52px;margin:-26px 0 0 -26px;border-radius:50%;background:#3b4145;border:2px solid #565e63}
.lab{text-align:center;font-size:11px;color:#8a9199;margin-top:6px;letter-spacing:.5px}
#mid{display:flex;flex-direction:column;gap:10px;align-items:center}
.b{padding:12px 16px;border:0;border-radius:10px;background:#31373b;color:#eee;font-size:17px;font-weight:600}
#stop{background:#c8352f;color:#fff;padding:16px 24px;font-size:18px}
.row{display:flex;gap:10px}
.mod{padding:8px 10px;border:0;border-radius:8px;background:#31373b;color:#eee;font-size:12px;font-weight:600}
.mod.on{background:#2a7;color:#041}
#cfg{position:fixed;inset:0;background:rgba(8,10,12,.97);display:none;flex-direction:column;padding:12px 14px;overflow:auto;z-index:9}
#cfg.on{display:flex}
#cfg h3{margin:0 0 8px;font-size:14px;color:#8a9199;font-weight:600;letter-spacing:.5px}
#cfg label{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:7px 0;border-bottom:1px solid #1d2124;font-size:13px}
#cfg select,#cfg input[type=range]{background:#22262a;color:#eee;border:1px solid #3a4045;border-radius:6px;padding:4px 6px}
#cfg input[type=range]{width:52%}
#cfg .ck{width:20px;height:20px}
#cfgx{margin-top:12px;padding:12px;border:0;border-radius:10px;background:#2a7;color:#041;font-weight:700;font-size:15px}
</style></head>
<body>
<div id=wrap>
  <div id=top>
    <img id=v alt="">
    <div id=hud>camara apagada</div>
    <div id=ver>v%V%</div>
    <div id=dst>--</div>
    <div id=net></div>
    <button id=led>LUZ OFF</button>
    <button id=slp>REPOSO</button>
    <button id=cl>CAMARA ON</button>
  </div>
  <div id=ctl>
    <div><div class=stick id=s1><div class=knob></div></div><div class=lab>MOVER</div></div>
    <div id=mid>
      <div class=row><button class=b id=ccw>&#8634;</button><button class=b id=cw>&#8635;</button></div>
      <button class=b id=stop>STOP</button>
      <div class=row><button class="b mod" data-m=1>MANUAL</button><button class="b mod" data-m=2>OBST</button></div>
      <div class=row><button class="b mod" data-m=3>SEGUIR</button><button class="b mod" data-m=4>LINEA</button></div>
      <div class=row><button class=b id=shot>FOTO</button><button class=b id=cfgo>AJUSTES</button></div>
    </div>
    <div><div class=stick id=s2><div class=knob></div></div><div class=lab>CAMARA</div></div>
  </div>
  <div id=cfg>
    <h3>AJUSTES DE CAMARA</h3>
    <label>Resolucion <select id=c_framesize><option value=5>QVGA 320x240</option><option value=8>VGA 640x480</option><option value=9>SVGA 800x600</option></select></label>
    <label>Calidad <input type=range id=c_quality min=10 max=40></label>
    <label>Brillo <input type=range id=c_brightness min=-2 max=2></label>
    <label>Contraste <input type=range id=c_contrast min=-2 max=2></label>
    <label>Saturacion <input type=range id=c_saturation min=-2 max=2></label>
    <label>Exposicion (valor) <input type=range id=c_aec_value min=0 max=1200></label>
    <label>Ganancia AGC <input type=range id=c_agc_gain min=0 max=30></label>
    <label>Espejo <input type=checkbox class=ck id=c_hmirror></label>
    <label>Volteo <input type=checkbox class=ck id=c_vflip></label>
    <label>Balance de blancos <input type=checkbox class=ck id=c_whitebal></label>
    <label>Ganancia AWB <input type=checkbox class=ck id=c_awb_gain></label>
    <label>Exposicion auto <input type=checkbox class=ck id=c_aec></label>
    <label>Ganancia auto <input type=checkbox class=ck id=c_gain></label>
    <button id=cfgx>CERRAR</button>
  </div>
</div>
<script>
var O={stop:0,fwd:92,back:163,left:149,right:106,ccw:172,cw:83,svl:230,svr:231,svc:232};
var cur=-1,hud=document.getElementById('hud'),v=document.getElementById('v');
function post(o){fetch('/c?o='+o,{cache:'no-store'})['catch'](function(){});}
function go(o){if(o===cur)return;cur=o;post(o);}
setInterval(function(){if(cur>=0)post(cur);},250);

function stick(el,cb){
  var k=el.querySelector('.knob'),id=null,ox=0,oy=0,R=50;
  function set(e){
    var dx=e.clientX-ox,dy=e.clientY-oy,d=Math.hypot(dx,dy);
    if(d>R){dx*=R/d;dy*=R/d;}
    k.style.transform='translate('+dx+'px,'+dy+'px)';
    cb(dx/R,dy/R);
  }
  el.addEventListener('pointerdown',function(e){
    id=e.pointerId;el.setPointerCapture(id);
    var r=el.getBoundingClientRect();ox=r.left+r.width/2;oy=r.top+r.height/2;
    set(e);e.preventDefault();
  });
  el.addEventListener('pointermove',function(e){if(e.pointerId===id){set(e);e.preventDefault();}});
  function end(e){if(e.pointerId!==id)return;id=null;k.style.transform='';cb(0,0);}
  el.addEventListener('pointerup',end);
  el.addEventListener('pointercancel',end);
}

stick(document.getElementById('s1'),function(x,y){
  if(y<-.35)go(O.fwd); else if(y>.35)go(O.back);
  else if(x<-.35)go(O.left); else if(x>.35)go(O.right);
  else go(O.stop);
});
stick(document.getElementById('s2'),function(x){
  if(x<-.35)go(O.svl); else if(x>.35)go(O.svr); else go(O.svc);
});

function hold(id,o){
  var b=document.getElementById(id);
  b.addEventListener('pointerdown',function(e){go(o);e.preventDefault();});
  b.addEventListener('pointerup',function(){go(O.stop);});
  b.addEventListener('pointercancel',function(){go(O.stop);});
}
hold('ccw',O.ccw);hold('cw',O.cw);
document.getElementById('stop').addEventListener('pointerdown',function(){go(O.stop);});
document.getElementById('cl').addEventListener('click',function(){
  hud.textContent='conectando...';
  v.src='http://'+location.hostname+':81/stream?t='+Date.now();
});
var led=document.getElementById('led'),ledOn=false;
led.addEventListener('click',function(){
  ledOn=!ledOn;
  fetch('/led'+(ledOn?'on':'off'),{cache:'no-store'})['catch'](function(){});
  led.textContent=ledOn?'LUZ ON':'LUZ OFF';
  led.className=ledOn?'on':'';
});
document.getElementById('slp').addEventListener('click',function(){
  if(!confirm('Dormir 10 minutos? La camara se apaga.'))return;
  hud.textContent='durmiendo...';v.src='';
  fetch('/sleep?m=10',{cache:'no-store'})['catch'](function(){});
});
v.onload=function(){hud.textContent='camara ok';};
v.onerror=function(){hud.textContent='error de camara';};
// Sonar y sensores de linea: el UNO los manda por la UART y el ESP32 los publica en /sens.
var dst=document.getElementById('dst');
setInterval(function(){
  fetch('/sens',{cache:'no-store'}).then(function(r){return r.text();}).then(function(t){
    var p=t.split(',');
    var d=(p[0]>=0)?(p[0]+' cm'):'--';
    dst.textContent=(p.length>3 && p[1]>=0)?(d+' | L'+p[1]+' C'+p[2]+' R'+p[3]):d;
  })['catch'](function(){});
},1000);
// Red del robot: con varios repetidores no hay forma de saber a cual se engancha
fetch('/info',{cache:'no-store'}).then(function(r){return r.text();}).then(function(t){
  document.getElementById('net').textContent=t;
})['catch'](function(){});
// Modos del UNO (25-28): se mandan UNA vez (el keepalive vuelve a O_STOP) y el
// modo elegido se resalta. El UNO no reporta el modo, lo recuerda el ESP32.
var mb=document.querySelectorAll('.mod');
function pintaModo(m){for(var i=0;i<mb.length;i++)mb[i].className='b mod'+((i+1===m)?' on':'');}
for(var mi=0;mi<mb.length;mi++)(function(b,m){
  b.addEventListener('click',function(){post(24+m);cur=-1;pintaModo(m);});
})(mb[mi],mi+1);
document.getElementById('shot').addEventListener('click',function(){window.open('/capture','_blank');});
// Panel de ajustes del sensor: se rellena con /status y cada cambio va a /control
var AJ=['framesize','quality','brightness','contrast','saturation','aec_value','agc_gain','hmirror','vflip','whitebal','awb_gain','aec','gain'];
var cfg=document.getElementById('cfg');
document.getElementById('cfgo').addEventListener('click',function(){cfg.className='on';});
document.getElementById('cfgx').addEventListener('click',function(){cfg.className='';});
function ctl(v,x){fetch('/control?v='+v+'&x='+x,{cache:'no-store'})['catch'](function(){});}
for(var ai=0;ai<AJ.length;ai++)(function(n){
  var e=document.getElementById('c_'+n);
  e.addEventListener('change',function(){ctl(n,e.type==='checkbox'?(e.checked?1:0):e.value);});
})(AJ[ai]);
fetch('/status',{cache:'no-store'}).then(function(r){return r.json();}).then(function(s){
  for(var i=0;i<AJ.length;i++){
    var n=AJ[i],e=document.getElementById('c_'+n),val=s[n];
    if(n==='whitebal')val=s.awb;
    if(n==='gain')val=s.agc;
    if(val===undefined||val===null)continue;
    if(e.type==='checkbox')e.checked=!!val;else e.value=val;
  }
  if(s.modo!==undefined)pintaModo(s.modo+1);
})['catch'](function(){});
</script>
</body></html>)HTML";
