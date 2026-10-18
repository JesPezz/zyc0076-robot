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
</style></head>
<body>
<div id=wrap>
  <div id=top>
    <img id=v alt="">
    <div id=hud>camara apagada</div>
    <div id=ver>v%V%</div>
    <button id=led class=on>LUZ ON</button>
    <button id=slp>REPOSO</button>
    <button id=cl>CAMARA ON</button>
  </div>
  <div id=ctl>
    <div><div class=stick id=s1><div class=knob></div></div><div class=lab>MOVER</div></div>
    <div id=mid>
      <div class=row><button class=b id=ccw>&#8634;</button><button class=b id=cw>&#8635;</button></div>
      <button class=b id=stop>STOP</button>
    </div>
    <div><div class=stick id=s2><div class=knob></div></div><div class=lab>CAMARA</div></div>
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
var led=document.getElementById('led'),ledOn=true;
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
</script>
</body></html>)HTML";
