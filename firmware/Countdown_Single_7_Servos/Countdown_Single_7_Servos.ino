/*
  9 -> 0 | Single-digit mechanical countdown display
  ESP32 + PCA9685 + 7 micro servos

  Compatible with the previously calibrated ESP32 NVS layout:
    namespace "reloj1"; keys off0..off6 and on0..on6.
  IMPORTANT: upload with full flash erase DISABLED to preserve calibration.

  The ESP32 controls the PCA9685 over I2C. PCA9685 generates servo PWM.
  Servos are powered by a SEPARATE regulated 5 V supply.

  Reference firmware for the 7-channel single-digit layout.
  Hardware motion, load and collision safety must be validated by the builder.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <Preferences.h>

constexpr uint8_t SDA_PIN = 21;
constexpr uint8_t SCL_PIN = 22;
constexpr uint8_t PCA_ADDR = 0x40;
constexpr uint8_t N_SERVOS = 7;
constexpr uint16_t PWM_HZ = 50;
// DO NOT CHANGE when reusing previous calibration values.
constexpr int PULSE_MIN_US = 700;
constexpr int PULSE_MAX_US = 2300;
const char* AP_NAME = "RELOJ_0_9";
const char* AP_PASSWORD = "12345678";

Adafruit_PWMServoDriver pca(PCA_ADDR);
WebServer server(80);
Preferences prefs;

int onPos[N_SERVOS], offPos[N_SERVOS], currentPos[N_SERVOS];
bool hasOn[N_SERVOS], hasOff[N_SERVOS];
const uint8_t singleMap[7] = {4,6,3,2,0,1,5}; // A B C D E F G; SAME as the left digit of 99->00
const bool digitMask[10][7] = {
  {1,1,1,1,1,1,0},  // 0
  {0,1,1,0,0,0,0},  // 1
  {1,1,0,1,1,0,1},  // 2
  {1,1,1,1,0,0,1},  // 3
  {0,1,1,0,0,1,1},  // 4
  {1,0,1,1,0,1,1},  // 5
  {1,0,1,1,1,1,1},  // 6
  {1,1,1,0,0,0,0},  // 7
  {1,1,1,1,1,1,1},  // 8
  {1,1,1,1,0,1,1}   // 9
};

bool countdownActive = false;
int countValue = 0;
uint8_t countSpeed = 100;
uint32_t countIntervalMs = 1000, nextCountMs = 0;
bool danceActive = false;
uint32_t danceEndMs = 0, nextDanceMs = 0;

bool channelValid(int c) { return c >= 0 && c < N_SERVOS; }
bool fullCalibration() {
  for (uint8_t i=0; i<N_SERVOS; ++i) if (!hasOn[i] || !hasOff[i]) return false;
  return true;
}
void stopModes() { countdownActive = false; danceActive = false; }
void servoWrite(int ch, int deg) {
  if (!channelValid(ch)) return;
  deg = constrain(deg, 0, 180);
  int us = map(deg, 0, 180, PULSE_MIN_US, PULSE_MAX_US);
  uint16_t ticks = (uint32_t)us * 4096UL / (1000000UL/PWM_HZ);
  pca.setPWM(ch, 0, ticks);
}
void releaseServo(int ch) { if (channelValid(ch)) pca.setPWM(ch,0,0); }
void releaseAll() { for (int i=0;i<N_SERVOS;i++) releaseServo(i); }

void loadCalibration() {
  Serial.println("--- Stored servo calibration ---");
  for (int i=0;i<N_SERVOS;i++) {
    String ko = "off"+String(i), kn = "on"+String(i);
    int o=prefs.getInt(ko.c_str(),-1), n=prefs.getInt(kn.c_str(),-1);
    hasOff[i]=(o>=0 && o<=180);
    hasOn[i]=(n>=0 && n<=180);
    offPos[i]=hasOff[i]?o:90;
    onPos[i]=hasOn[i]?n:90;
    currentPos[i]=offPos[i];
    Serial.printf("CH%02d OFF=%d%s ON=%d%s\n",i,offPos[i],hasOff[i]?"":" (missing)",onPos[i],hasOn[i]?"":" (missing)");
  }
}
bool storePosition(int ch, bool on, int deg) {
  if (!channelValid(ch) || deg<0 || deg>180) return false;
  String k=String(on?"on":"off")+String(ch);
  if (!prefs.putInt(k.c_str(),deg)) return false;
  if (on) { onPos[ch]=deg;hasOn[ch]=true; }
  else { offPos[ch]=deg;hasOff[ch]=true; }
  return true;
}

// For 100% speed, command target directly with a small start stagger.
// Speed slider 1..99 adds software motion interpolation.
void moveOne(int ch, int goal, int speed) {
  if (!channelValid(ch)) return;
  speed=constrain(speed,1,100); goal=constrain(goal,0,180);
  if (speed==100) servoWrite(ch,goal);
  else {
    int start=currentPos[ch], step=goal>=start?1:-1;
    int ms=map(speed,1,99,30,1);
    for (int p=start;p!=goal;p+=step) { servoWrite(ch,p);delay(ms); }
    servoWrite(ch,goal);
  }
  currentPos[ch]=goal;
}
void moveAll(const int target[N_SERVOS], int speed) {
  speed=constrain(speed,1,100);
  if (speed==100) {
    int batch=0;
    for (int i=0;i<N_SERVOS;i++) {
      if (target[i]==currentPos[i]) continue;
      servoWrite(i,target[i]);currentPos[i]=target[i];
      if (++batch==3) { delay(18);batch=0; }
    }
    return;
  }
  int start[N_SERVOS], span=0;
  for (int i=0;i<N_SERVOS;i++) {
    start[i]=currentPos[i];
    span=max(span,abs(target[i]-start[i]));
  }
  if (!span) return;
  int ms=map(speed,1,99,30,1);
  for (int k=1;k<=span;k++) {
    for (int i=0;i<N_SERVOS;i++) {
      int p=start[i]+(int)((int32_t)(target[i]-start[i])*k/span);
      servoWrite(i,p);
    }
    delay(ms);
  }
  for (int i=0;i<N_SERVOS;i++) currentPos[i]=target[i];
}
void targetForDigit(int digit,const uint8_t map7[7],int target[N_SERVOS]) {
  for (int s=0;s<7;s++) {
    int ch=map7[s];target[ch]=digitMask[digit][s]?onPos[ch]:offPos[ch];
  }
}
bool showNumber(int value,int speed) {
  if (!fullCalibration()) return false;
  value=constrain(value,0,9);
  int target[N_SERVOS];
  for (int i=0;i<N_SERVOS;i++) target[i]=currentPos[i];
  targetForDigit(value,singleMap,target);
  moveAll(target,speed);
  return true;
}
bool allState(bool state,int speed) {
  if (!fullCalibration()) return false;
  int target[N_SERVOS];
  for (int i=0;i<N_SERVOS;i++) target[i]=state?onPos[i]:offPos[i];
  moveAll(target,speed);
  return true;
}
void startCountdown(int from,int speed,uint32_t seconds) {
  stopModes();
  countValue=constrain(from,0,9);
  countSpeed=constrain(speed,1,100);
  countIntervalMs=constrain(seconds,1UL,60UL)*1000UL;
  showNumber(countValue,countSpeed);
  countdownActive=(countValue>0);
  nextCountMs=millis()+countIntervalMs;
}
void tickCountdown() {
  if (!countdownActive || (int32_t)(millis()-nextCountMs)<0) return;
  if (countValue>0) --countValue;
  showNumber(countValue,countSpeed);
  if (countValue==0) countdownActive=false;
  else nextCountMs=millis()+countIntervalMs;
}
void startDance(int seconds) {
  stopModes();danceActive=true;
  nextDanceMs=millis();
  danceEndMs=millis()+constrain(seconds,3,60)*1000UL;
}
void tickDance() {
  if (!danceActive) return;
  if ((int32_t)(millis()-danceEndMs)>=0) { danceActive=false;allState(false,100);return; }
  if ((int32_t)(millis()-nextDanceMs)<0) return;
  // Only verified ON/OFF positions; no random angles beyond calibration.
  switch(random(0,6)) {
    case 0: case 1: showNumber(random(0,10),100); break;
    case 2: {int d=random(0,10);showNumber(d,100);break;}
    case 3: {
      int target[N_SERVOS];for(int i=0;i<N_SERVOS;i++)target[i]=offPos[i];
      int s=random(0,7); target[singleMap[s]]=onPos[singleMap[s]];
      moveAll(target,100);break;
    }
    case 4: allState(true,100);break;
    default: allState(false,100);break;
  }
  // Give physical servos time to settle between changes.
  nextDanceMs=millis()+random(380,760);
}

// The backup uses a plain 7-pair list. Strictly verify BEFORE overwriting NVS.
String makeBackup() {
  if (!fullCalibration()) return "";
  String out;out.reserve(70);
  for (int i=0;i<N_SERVOS;i++) {
    if (i)out+=';';
    out+=String(offPos[i])+","+String(onPos[i]);
  }
  return out;
}
bool parseDegree(const String& t,int &value) {
  if (t.isEmpty() || t.length()>3) return false;
  for (size_t i=0;i<t.length();i++) if(t[i]<'0'||t[i]>'9')return false;
  value=t.toInt();return value>=0 && value<=180;
}
bool restoreBackup(String csv) {
  csv.trim();int off[N_SERVOS],on[N_SERVOS];size_t start=0;
  for(int i=0;i<N_SERVOS;i++) {
    int end=(i==N_SERVOS-1)?(int)csv.length():csv.indexOf(';',start);
    if(end<0 || end<(int)start) return false;
    if(i==N_SERVOS-1 && csv.indexOf(';',start)>=0) return false;
    String pair=csv.substring(start,end);int sep=pair.indexOf(',');
    if(sep<0||pair.indexOf(',',sep+1)>=0) return false;
    if(!parseDegree(pair.substring(0,sep),off[i]) ||
       !parseDegree(pair.substring(sep+1),on[i]))return false;
    start=end+1;
  }
  for(int i=0;i<N_SERVOS;i++) {
    if(!storePosition(i,false,off[i]) || !storePosition(i,true,on[i])) return false;
  }
  return true;
}
bool copyCalibration(int from,int to) {
  if (!channelValid(from)||!channelValid(to)||!hasOn[from]||!hasOff[from])return false;
  return storePosition(to,false,offPos[from]) && storePosition(to,true,onPos[from]);
}

// =====================================================================
// RESPONSIVE WEB INTERFACE - EN/ES. Language is saved in browser only.
// =====================================================================
const char PAGE[] PROGMEM=R"WEB(
<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>9 to 0</title><style>
:root{color-scheme:dark;--bg:#0b0b0e;--card:#19191f;--ink:#fafafa;--muted:#a9a9b0;--red:#ff493e}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px system-ui,Arial;padding:14px}
main{max-width:650px;margin:auto}h1{text-align:center;margin:4px 0 12px}h2{font-size:19px;margin-top:3px}
.status{background:#23232a;padding:10px;border-radius:10px;text-align:center;margin:9px 0}
nav{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:5px;margin:10px 0}
button{border:0;border-radius:10px;background:#267fed;color:white;font:inherit;font-weight:650;padding:13px 8px;cursor:pointer}
nav button{font-size:12px;padding:11px 3px;background:#33333a}nav .chosen{background:#267fed}
.card{display:none;background:var(--card);border-radius:16px;padding:18px}.card.active{display:block}
input[type=number],select,textarea{width:100%;padding:12px;border:1px solid #494952;border-radius:9px;background:#292932;color:white;font:inherit}
input[type=range]{width:100%}.rangeValue{text-align:center;font-size:25px;margin:8px}
.num{background:#080809;border:1px solid #382323;border-radius:18px;color:var(--red);font:bold clamp(76px,22vw,130px) monospace;letter-spacing:.07em;text-align:center;text-shadow:0 0 14px #d12424aa;margin:12px 0;padding:12px 3px;font-variant-numeric:tabular-nums}
.row{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin:9px 0}.digits{display:grid;grid-template-columns:repeat(5,1fr);gap:6px;margin-top:9px}
.green{background:#24864b}.grey{background:#44444d}.red{background:#a53232}.amber{background:#b98519;color:#050505}.purple{background:#6b45ac}
label{display:block;color:#c9c9cc;margin:14px 0 5px}small,.note{color:var(--muted);line-height:1.45}.separate{border-top:1px solid #494952;margin:22px 0}
#knob{position:fixed;inset:0;background:#000;z-index:30;display:none;align-items:center;justify-content:center;flex-direction:column;padding:35px 12px}
#knob.open{display:flex}#knobValue{font:bold clamp(100px,32vw,190px) monospace;color:var(--red);text-shadow:0 0 18px #bf332e8c;font-variant-numeric:tabular-nums}
#knobDisk{width:min(70vw,300px);height:min(70vw,300px);min-width:205px;min-height:205px;position:relative;border-radius:50%;border:3px solid #303037;background:radial-gradient(circle,#1a1a1a,#080808);touch-action:none;user-select:none;margin-top:18px}
#knobPointer{position:absolute;inset:0;pointer-events:none}#knobPointer:before{content:"";position:absolute;top:13px;left:calc(50% - 3px);width:6px;height:24px;background:var(--red);border-radius:6px}
#knobCore{position:absolute;inset:21%;border-radius:50%;background:#050505;border:1px solid #333;display:grid;place-items:center;pointer-events:none;letter-spacing:2px;font-weight:bold}
#closeKnob{position:absolute;top:16px;left:16px;width:48px;height:48px;border-radius:50%;background:#202024;font-size:24px}
#knobHint{color:#8f8f98;font-size:14px;text-align:center}#knobState{margin-top:20px;color:#aaa}
@media(max-width:520px){nav{grid-template-columns:repeat(3,1fr)}.card{padding:14px}#knobValue{font-size:30vw}}
</style></head><body><main><h1>9 → 0</h1><div class="status" id="systemStatus">…</div>
<div class="row"><select id="language"><option value="en">English</option><option value="es">Español</option></select>
<button class="grey" onclick="refresh()" data-i="refresh">Refresh</button></div>
<nav id="tabs"><button onclick="tab('numbers')" class="chosen" data-i="numbers">Numbers</button><button onclick="tab('counter')" data-i="counter">Countdown</button><button onclick="openKnob()" data-i="knob">Dial</button><button onclick="tab('dance')" data-i="dance">Dance</button><button onclick="tab('cal')" data-i="cal">Calibration</button></nav>
<section id="numbers" class="card active"><h2 data-i="numbers">Numbers</h2><div class="num" id="numDisplay">0</div>
<label data-i="value">Number (0–9)</label><input id="numberInput" type="number" min="0" max="9" value="0">
<label data-i="speed">Motion speed (100 = direct)</label><input id="numberSpeed" type="range" min="1" max="100" value="100"><div class="rangeValue" id="nsVal">100%</div>
<button onclick="showNum()" data-i="show">Show number</button><div class="digits" id="digitButtons"></div>
<div class="row"><button class="green" onclick="allOn()" data-i="allOn">All ON / 8</button><button class="grey" onclick="allOff()" data-i="allOff">All OFF</button></div>
<button class="red" onclick="releaseAll()" data-i="releaseAll">Release servo signals</button><div class="status" id="numberMsg">–</div></section>
<section id="counter" class="card"><h2 data-i="counter">Countdown</h2><div class="num" id="counterDisplay">0</div>
<label data-i="initial">Starting number</label><input id="initial" type="number" min="0" max="9" value="9">
<label data-i="speed">Motion speed (100 = direct)</label><input id="counterSpeed" type="range" min="1" max="100" value="100"><div class="rangeValue" id="csVal">100%</div>
<label data-i="seconds">Seconds per step</label><input id="seconds" type="number" min="1" max="60" value="1">
<div class="row"><button class="green" onclick="startCount()" data-i="start">Start</button><button class="red" onclick="stopCount()" data-i="stop">Stop</button></div><div class="status" id="counterMsg">–</div></section>
<section id="dance" class="card"><h2 data-i="dance">Dance</h2><p class="note" data-i="danceNote">Uses stored ON/OFF positions only. Mechanical clearance must still be checked.</p>
<label data-i="duration">Duration (seconds)</label><input id="duration" type="range" min="3" max="60" value="15"><div class="rangeValue" id="durationVal">15 s</div>
<div class="row"><button class="purple" onclick="danceStart()" data-i="start">Start</button><button class="red" onclick="danceStop()" data-i="stop">Stop</button></div><div class="status" id="danceMsg">–</div></section>
<section id="cal" class="card"><h2 data-i="cal">Calibration</h2><p class="note" data-i="calNote">Existing values are loaded from NVS. Saving or copying changes them; moving does not.</p>
<label data-i="channel">Channel</label><select id="servo" onchange="loadServo()"></select>
<label data-i="angle">Manual position</label><input id="angle" type="range" min="0" max="180" value="90"><div class="rangeValue" id="angleVal">90°</div>
<label data-i="speed">Motion speed (100 = direct)</label><input id="calSpeed" type="range" min="1" max="100" value="40"><div class="rangeValue" id="calSpeedVal">40%</div>
<button onclick="manualMove()" data-i="move">Move selected servo</button>
<div class="row"><div class="status">OFF: <b id="offSaved">–</b></div><div class="status">ON: <b id="onSaved">–</b></div></div>
<div class="row"><button class="grey" onclick="save('off')" data-i="saveOff">Save as OFF</button><button class="green" onclick="save('on')" data-i="saveOn">Save as ON</button></div>
<div class="row"><button class="amber" onclick="gotoState('off')" data-i="testOff">Test OFF</button><button class="amber" onclick="gotoState('on')" data-i="testOn">Test ON</button></div>
<div class="separate"></div><h2 data-i="copy">Copy calibration</h2>
<button class="grey" onclick="copyPrev()" data-i="copyPrev">Copy previous channel to this one</button>
<button class="grey" onclick="copyNext()" data-i="copyNext">Copy this channel to the next</button>
<button class="red" onclick="copyAll()" data-i="copyAll">Overwrite all 7 with this channel</button>
<p class="note" data-i="copyWarning">Caution: copying may be mechanically unsafe for channels mounted in the opposite direction. Check each travel before moving.</p>
<div class="separate"></div><h2 data-i="backup">Calibration backup</h2>
<textarea id="backupData" spellcheck="false" placeholder="OFF,ON;OFF,ON;... (7 pairs)"></textarea>
<button class="grey" onclick="backup()" data-i="export">Generate backup text</button>
<button class="amber" onclick="restore()" data-i="restore">Restore pasted backup</button>
<p class="note" data-i="backupNote">Keep the backup outside the ESP32. Restoring overwrites current calibration.</p>
<button class="red" onclick="releaseOne()" data-i="releaseOne">Release selected servo signal</button><div class="status" id="calMsg">–</div></section></main>
<div id="knob"><button id="closeKnob" onclick="closeKnob()">×</button><small data-i="counter">Countdown</small><div id="knobValue">9</div><p id="knobHint" data-i="turn">Turn to select; tap to start</p>
<div id="knobDisk"><div id="knobPointer"></div><div id="knobCore" data-i="tap">TAP · START</div></div><div id="knobState">–</div></div>
<script>
const $=id=>document.getElementById(id);
const dict={
 en:{refresh:'Refresh',numbers:'Numbers',counter:'Countdown',knob:'Dial',dance:'Dance',cal:'Calibration',value:'Number (0–9)',speed:'Motion speed (100 = direct)',show:'Show number',allOn:'All ON / 8',allOff:'All OFF',releaseAll:'Release servo signals',initial:'Starting number',seconds:'Seconds per step',start:'Start',stop:'Stop',danceNote:'Only stored ON/OFF positions are used. Check mechanical clearance before enabling animation.',duration:'Duration (seconds)',calNote:'Values come from ESP32 memory. Only saving or copying changes them.',channel:'Channel',angle:'Manual position',move:'Move selected servo',saveOff:'Save OFF',saveOn:'Save ON',testOff:'Test OFF',testOn:'Test ON',copy:'Copy calibration',copyPrev:'Copy previous channel into selected channel',copyNext:'Copy selected channel to next',copyAll:'Overwrite all 7 with selected calibration',copyWarning:'Warning: copied positions can be unsafe for mechanically mirrored channels. Verify before moving.',backup:'Calibration backup',export:'Generate backup text',restore:'Restore pasted backup',backupNote:'Keep a copy outside the ESP32. Restoring replaces all saved positions.',releaseOne:'Release selected signal',turn:'Turn to select; tap to start',tap:'TAP · START',missing:'Missing calibration',ready:'7 channels calibrated',done:'Done',stopped:'Stopped',active:'Running'},
 es:{refresh:'Actualizar',numbers:'Números',counter:'Cuenta atrás',knob:'Mando',dance:'Baile',cal:'Calibración',value:'Número (0–9)',speed:'Velocidad (100 = directo)',show:'Mostrar número',allOn:'Todos ON / 8',allOff:'Todos OFF',releaseAll:'Liberar señales de servos',initial:'Número inicial',seconds:'Segundos por paso',start:'Iniciar',stop:'Detener',danceNote:'Solo usa ON/OFF guardados. Comprueba que no haya choques antes de activar las animaciones.',duration:'Duración (segundos)',calNote:'Los datos vienen de la memoria del ESP32. Solo guardar o copiar modifica la calibración.',channel:'Canal',angle:'Posición manual',move:'Mover servo seleccionado',saveOff:'Guardar OFF',saveOn:'Guardar ON',testOff:'Probar OFF',testOn:'Probar ON',copy:'Copiar calibración',copyPrev:'Copiar canal anterior al seleccionado',copyNext:'Copiar canal seleccionado al siguiente',copyAll:'Sobrescribir los 7 con esta calibración',copyWarning:'Atención: copiar posiciones puede ser peligroso en servos montados al revés. Comprueba el recorrido.',backup:'Copia de seguridad',export:'Generar texto de copia',restore:'Restaurar copia pegada',backupNote:'Guarda una copia fuera del ESP32. Restaurar sustituye todos los valores.',releaseOne:'Liberar señal del servo',turn:'Gira para elegir; pulsa para iniciar',tap:'PULSAR · START',missing:'Falta calibración',ready:'7 canales calibrados',done:'Terminado',stopped:'Detenido',active:'Activo'}
};
let lang=localStorage.getItem('lang9900')||'en';
function tr(k){return (dict[lang]||dict.en)[k]||k;}
function i18n(){document.documentElement.lang=lang;$('language').value=lang;document.querySelectorAll('[data-i]').forEach(x=>x.textContent=tr(x.dataset.i));}
$('language').addEventListener('change',e=>{lang=e.target.value;localStorage.setItem('lang9900',lang);i18n();fillChannels();});
const names=['E','F','D','C','A','G','B'];
function fillChannels(){let ch=$('servo').value||'0';$('servo').innerHTML='';names.forEach((n,i)=>{let o=document.createElement('option');o.value=i;o.textContent='CH'+i+' · '+(lang==='es'?n.replace('Left','Izquierdo').replace('Right','Derecho'):n);$('servo').append(o)});$('servo').value=ch;}
for(let i=0;i<10;i++){let b=document.createElement('button');b.textContent=i;b.onclick=()=>{$('numberInput').value=i;showNum()};$('digitButtons').append(b)}
for(const [input,output,suffix] of [['numberSpeed','nsVal','%'],['counterSpeed','csVal','%'],['calSpeed','calSpeedVal','%'],['angle','angleVal','°'],['duration','durationVal',' s']]) {$(input).addEventListener('input',()=>$(output).textContent=$(input).value+suffix)}
function tab(id){document.querySelectorAll('.card').forEach(e=>e.classList.remove('active'));$(id).classList.add('active');document.querySelectorAll('#tabs button').forEach(b=>b.classList.remove('chosen'));let ix={numbers:0,counter:1,dance:3,cal:4}[id];if(ix!==undefined)document.querySelectorAll('#tabs button')[ix].classList.add('chosen');if(id==='cal')loadServo();}
async function api(path,options){let r=await fetch(path,options);let data=await r.json();if(!r.ok||data.ok===false)throw Error(data.error||'Request failed');return data;}
function msg(id,s){$(id).textContent=s}
async function refresh(){try{let s=await api('/status');msg('systemStatus',s.calibrated?tr('ready'):tr('missing'));}catch(e){msg('systemStatus',e.message)}}
function val(id,min,max){let n=parseInt($(id).value,10);return Math.max(min,Math.min(max,isNaN(n)?min:n))}
async function showNum(){let n=val('numberInput',0,9);try{await api('/show?n='+n+'&speed='+val('numberSpeed',1,100));msg('numberMsg',String(n).padStart(1,'0'));$('numDisplay').textContent=String(n).padStart(1,'0')}catch(e){msg('numberMsg',e.message)}}
async function allOn(){try{await api('/all?state=on&speed='+val('numberSpeed',1,100));$('numDisplay').textContent='8'}catch(e){msg('numberMsg',e.message)}}
async function allOff(){try{await api('/all?state=off&speed='+val('numberSpeed',1,100));$('numDisplay').textContent='--'}catch(e){msg('numberMsg',e.message)}}
async function releaseAll(){try{await api('/releaseAll');msg('numberMsg',tr('done'))}catch(e){msg('numberMsg',e.message)}}
async function startCount(){try{await api('/counterStart?n='+val('initial',0,9)+'&speed='+val('counterSpeed',1,100)+'&seconds='+val('seconds',1,60));msg('counterMsg',tr('active'))}catch(e){msg('counterMsg',e.message)}}
async function stopCount(){try{await api('/counterStop');msg('counterMsg',tr('stopped'))}catch(e){msg('counterMsg',e.message)}}
async function danceStart(){try{await api('/danceStart?seconds='+val('duration',3,60));msg('danceMsg',tr('active'))}catch(e){msg('danceMsg',e.message)}}
async function danceStop(){try{await api('/danceStop');msg('danceMsg',tr('stopped'))}catch(e){msg('danceMsg',e.message)}}
function selected(){return val('servo',0,6)}
async function loadServo(){try{let x=await api('/servo?ch='+selected());$('angle').value=x.current;$('angleVal').textContent=x.current+'°';$('offSaved').textContent=x.hasOff?x.off+'°':'—';$('onSaved').textContent=x.hasOn?x.on+'°':'—'}catch(e){msg('calMsg',e.message)}}
async function manualMove(){try{await api('/move?ch='+selected()+'&deg='+val('angle',0,180)+'&speed='+val('calSpeed',1,100));msg('calMsg',tr('done'))}catch(e){msg('calMsg',e.message)}}
async function save(type){try{await api('/save?ch='+selected()+'&type='+type+'&deg='+val('angle',0,180));await loadServo();await refresh();msg('calMsg',tr('done'))}catch(e){msg('calMsg',e.message)}}
async function gotoState(type){try{let x=await api('/goto?ch='+selected()+'&type='+type+'&speed='+val('calSpeed',1,100));$('angle').value=x.position;$('angleVal').textContent=x.position+'°'}catch(e){msg('calMsg',e.message)}}
async function releaseOne(){try{await api('/release?ch='+selected());msg('calMsg',tr('done'))}catch(e){msg('calMsg',e.message)}}
async function copyPrev(){let ch=selected();if(ch===0)return;try{await api('/copy?from='+(ch-1)+'&to='+ch);await loadServo();msg('calMsg',tr('done'))}catch(e){msg('calMsg',e.message)}}
async function copyNext(){let ch=selected();if(ch>=6)return;try{await api('/copy?from='+ch+'&to='+(ch+1));$('servo').value=ch+1;await loadServo();msg('calMsg',tr('done'))}catch(e){msg('calMsg',e.message)}}
async function copyAll(){if(!confirm(lang==='es'?'¿Sustituir la calibración de los 7 servos?':'Overwrite all 7 servo calibrations?'))return;try{await api('/copyAll?from='+selected());await loadServo();await refresh();msg('calMsg',tr('done'))}catch(e){msg('calMsg',e.message)}}
async function backup(){try{let r=await fetch('/backup');if(!r.ok)throw Error('Calibration incomplete');$('backupData').value=await r.text();$('backupData').select();try{await navigator.clipboard.writeText($('backupData').value)}catch(e){}msg('calMsg',tr('done'))}catch(e){msg('calMsg',e.message)}}
async function restore(){if(!confirm(lang==='es'?'¿Sobrescribir la calibración guardada?':'Overwrite stored calibration?'))return;try{await api('/restore',{method:'POST',headers:{'Content-Type':'text/plain'},body:$('backupData').value});await loadServo();await refresh();msg('calMsg',tr('done'))}catch(e){msg('calMsg',e.message)}}
let knobNumber=9,down=false,previous=0,travel=0,accum=0,rot=0,downAt=0,knobRunning=false;
function knobAngle(e){let r=$('knobDisk').getBoundingClientRect();return Math.atan2(e.clientY-(r.top+r.height/2),e.clientX-(r.left+r.width/2))*180/Math.PI;}
function updateKnob(){$('knobValue').textContent=String(knobNumber).padStart(1,'0')}
function openKnob(){$('knob').classList.add('open');updateKnob()}
function closeKnob(){$('knob').classList.remove('open')}
$('knobDisk').addEventListener('pointerdown',e=>{if(knobRunning)return;down=true;previous=knobAngle(e);travel=accum=0;downAt=Date.now();$('knobDisk').setPointerCapture(e.pointerId);e.preventDefault()});
$('knobDisk').addEventListener('pointermove',e=>{if(!down)return;let a=knobAngle(e),delta=a-previous;if(delta>180)delta-=360;if(delta< -180)delta+=360;previous=a;accum+=delta;travel+=Math.abs(delta);rot+=delta;$('knobPointer').style.transform='rotate('+rot+'deg)';while(accum>=14){accum-=14;knobNumber=Math.min(9,knobNumber+1);updateKnob()}while(accum<=-14){accum+=14;knobNumber=Math.max(0,knobNumber-1);updateKnob()}e.preventDefault()});
$('knobDisk').addEventListener('pointerup',()=>{if(!down)return;down=false;if(travel<12 && Date.now()-downAt<900)knobStart()});
$('knobDisk').addEventListener('pointercancel',()=>down=false);
async function knobStart(){if(knobNumber<=0||knobRunning)return;knobRunning=true;try{await api('/counterStart?n='+knobNumber+'&speed=100&seconds=1');msg('knobState',tr('active'))}catch(e){knobRunning=false;msg('knobState',e.message)}}
async function tickUI(){try{let x=await api('/counterStatus');let two=String(x.value).padStart(1,'0');$('counterDisplay').textContent=two;if(x.active)msg('counterMsg',tr('active'));else if(x.value===0)msg('counterMsg',tr('done'));if($('knob').classList.contains('open')&&(x.active||knobRunning)){knobNumber=x.value;updateKnob();knobRunning=x.active;msg('knobState',x.active?tr('active'):tr('done'))}}catch(e){}}
fillChannels();i18n();refresh();loadServo();updateKnob();setInterval(tickUI,500);
</script></body></html>
)WEB";

void ok(const String& extra="") { server.send(200,"application/json",extra.isEmpty()?"{\"ok\":true}":"{\"ok\":true,"+extra+"}"); }
void fail(int code,const char* reason) {server.send(code,"application/json",String("{\"ok\":false,\"error\":\"")+reason+"\"}");}
bool argInt(const char* name,int low,int high,int& n) {
  if(!server.hasArg(name))return false;
  String v=server.arg(name);if(v.isEmpty())return false;
  for(size_t i=0;i<v.length();i++)if(v[i]<'0'||v[i]>'9')return false;
  long x=v.toInt();if(x<low||x>high)return false;n=(int)x;return true;
}
void registerRoutes() {
  server.on("/",HTTP_GET,[](){server.send_P(200,"text/html; charset=utf-8",PAGE);});
  server.on("/status",HTTP_GET,[](){ok(String("\"calibrated\":")+(fullCalibration()?"true":"false"));});
  server.on("/servo",HTTP_GET,[](){int ch;if(!argInt("ch",0,6,ch))return fail(400,"Invalid channel");
    ok("\"off\":"+String(offPos[ch])+",\"on\":"+String(onPos[ch])+",\"current\":"+String(currentPos[ch])+
       ",\"hasOff\":"+String(hasOff[ch]?"true":"false")+",\"hasOn\":"+String(hasOn[ch]?"true":"false"));});
  server.on("/move",HTTP_GET,[](){int ch,deg,s;if(!argInt("ch",0,6,ch)||!argInt("deg",0,180,deg)||!argInt("speed",1,100,s))return fail(400,"Invalid values");stopModes();moveOne(ch,deg,s);ok();});
  server.on("/save",HTTP_GET,[](){int ch,deg;if(!argInt("ch",0,6,ch)||!argInt("deg",0,180,deg)||!server.hasArg("type"))return fail(400,"Invalid values");
    String t=server.arg("type");if(t!="on"&&t!="off")return fail(400,"Invalid type");stopModes();if(!storePosition(ch,t=="on",deg))return fail(500,"NVS write failed");ok();});
  server.on("/goto",HTTP_GET,[](){int ch,s;if(!argInt("ch",0,6,ch)||!argInt("speed",1,100,s))return fail(400,"Invalid values");String t=server.arg("type");if(t!="off"&&t!="on")return fail(400,"Invalid type");if(t=="on"&&!hasOn[ch])return fail(409,"ON not calibrated");if(t=="off"&&!hasOff[ch])return fail(409,"OFF not calibrated");stopModes();int deg=t=="on"?onPos[ch]:offPos[ch];moveOne(ch,deg,s);ok("\"position\":"+String(deg));});
  server.on("/show",HTTP_GET,[](){int n,s;if(!argInt("n",0,9,n)||!argInt("speed",1,100,s))return fail(400,"Invalid values");stopModes();if(!showNumber(n,s))return fail(409,"Calibration incomplete");ok();});
  server.on("/all",HTTP_GET,[](){int s;if(!argInt("speed",1,100,s))return fail(400,"Invalid speed");String state=server.arg("state");if(state!="off"&&state!="on")return fail(400,"Invalid state");stopModes();if(!allState(state=="on",s))return fail(409,"Calibration incomplete");ok();});
  server.on("/release",HTTP_GET,[](){int ch;if(!argInt("ch",0,6,ch))return fail(400,"Invalid channel");stopModes();releaseServo(ch);ok();});
  server.on("/releaseAll",HTTP_GET,[](){stopModes();releaseAll();ok();});
  server.on("/counterStart",HTTP_GET,[](){int n,s,sec;if(!argInt("n",0,9,n)||!argInt("speed",1,100,s)||!argInt("seconds",1,60,sec))return fail(400,"Invalid counter values");if(!fullCalibration())return fail(409,"Calibration incomplete");startCountdown(n,s,sec);ok();});
  server.on("/counterStop",HTTP_GET,[](){countdownActive=false;ok();});
  server.on("/counterStatus",HTTP_GET,[](){ok(String("\"value\":")+String(countValue)+",\"active\":"+(countdownActive?"true":"false"));});
  server.on("/danceStart",HTTP_GET,[](){int sec;if(!argInt("seconds",3,60,sec))return fail(400,"Invalid duration");if(!fullCalibration())return fail(409,"Calibration incomplete");startDance(sec);ok();});
  server.on("/danceStop",HTTP_GET,[](){danceActive=false;if(fullCalibration())allState(false,100);ok();});
  server.on("/copy",HTTP_GET,[](){int from,to;if(!argInt("from",0,6,from)||!argInt("to",0,6,to))return fail(400,"Invalid channels");stopModes();if(!copyCalibration(from,to))return fail(409,"Copy failed");ok();});
  server.on("/copyAll",HTTP_GET,[](){int from;if(!argInt("from",0,6,from))return fail(400,"Invalid channel");if(!hasOn[from]||!hasOff[from])return fail(409,"Source not calibrated");stopModes();for(int i=0;i<N_SERVOS;i++)if(!copyCalibration(from,i))return fail(500,"NVS write failed");ok();});
  server.on("/backup",HTTP_GET,[](){if(!fullCalibration())return server.send(409,"text/plain","Calibration incomplete");server.send(200,"text/plain",makeBackup());});
  server.on("/restore",HTTP_POST,[](){stopModes();if(!restoreBackup(server.arg("plain")))return fail(400,"Invalid backup or NVS write failure");ok();});
  server.onNotFound([](){fail(404,"Not found");});
}

void setup() {
  Serial.begin(115200);delay(500);
  if(!prefs.begin("reloj1",false))Serial.println("ERROR: cannot open Preferences NVS");
  loadCalibration();
  Wire.begin(SDA_PIN,SCL_PIN);
  if(!pca.begin()) Serial.println("ERROR: PCA9685 did not respond at 0x40");
  pca.setPWMFreq(PWM_HZ);delay(20);
  if(fullCalibration()) {for(int i=0;i<N_SERVOS;i++){servoWrite(i,offPos[i]);delay(25);}}
  else releaseAll();
  randomSeed(micros());WiFi.mode(WIFI_AP);
  if(!WiFi.softAP(AP_NAME,AP_PASSWORD))Serial.println("ERROR: AP start failed");
  registerRoutes();server.begin();
  Serial.print("WiFi: ");Serial.println(AP_NAME);
  Serial.print("IP: ");Serial.println(WiFi.softAPIP());
  Serial.println("Open http://192.168.4.1 - EN/ES language selector");
}
void loop() {server.handleClient();tickCountdown();tickDance();delay(2);}
