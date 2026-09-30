#include <Arduino.h>
#include <DNSServer.h>
#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  #include <ESP8266mDNS.h>
  #include <EEPROM.h>
  #include <Updater.h>
#else
  #include <WiFi.h>
  #include <WebServer.h>
  #include <WiFiUdp.h>
  #include <ESPmDNS.h>
  #include <Preferences.h>
  #include <Update.h>
#endif

#ifndef UPDATE_SIZE_UNKNOWN
  #define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF
#endif
#ifndef DEFAULT_OUTPUT_PIN
  #define DEFAULT_OUTPUT_PIN 2
#endif
#ifndef STATUS_LED_PIN
  #ifdef LED_BUILTIN
    #define STATUS_LED_PIN LED_BUILTIN
  #else
    #define STATUS_LED_PIN 2
  #endif
#endif
#ifndef STATUS_LED_INVERTED
  #define STATUS_LED_INVERTED 0
#endif

constexpr uint16_t ARTNET_PORT = 6454;
constexpr uint8_t MAX_OUTPUTS = 24;
constexpr uint8_t OUTPUT_DIGITAL = 0;
constexpr uint8_t OUTPUT_PWM = 1;

#if defined(ESP8266)
  constexpr const char* DEVICE_PROFILE = "ESP8266 / Wemos D1 mini";
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  constexpr const char* DEVICE_PROFILE = "ESP32-C3 SuperMini";
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
  constexpr const char* DEVICE_PROFILE = "ESP32-C6 SuperMini";
#elif defined(CONFIG_IDF_TARGET_ESP32C2)
  constexpr const char* DEVICE_PROFILE = "ESP32-C2 DevKitM-1";
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
  constexpr const char* DEVICE_PROFILE = "ESP32-S2 Saola-1";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  constexpr const char* DEVICE_PROFILE = "ESP32-S3 DevKitC-1";
#else
  constexpr const char* DEVICE_PROFILE = "ESP32 DevKit / WROOM";
#endif

struct PinDef { uint8_t gpio; bool pwm; };
#if defined(ESP8266)
const PinDef BOARD_PINS[] = {{2,true},{4,true},{5,true},{12,true},{13,true},{14,true},{16,false}};
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
// GPIO2/8/9 are boot strapping and GPIO12-17 are flash. GPIO8 is the SuperMini LED.
const PinDef BOARD_PINS[] = {{0,true},{1,true},{3,true},{4,true},{5,true},{6,true},{7,true},{8,true},{10,true}};
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
// GPIO4/5/8/9/15 are strapping. GPIO15 stays listed solely because it is the
// SuperMini's built-in LED and is therefore reserved; USB is GPIO12/13 and
// flash is GPIO24-30. The RGB LED data pin (GPIO8) is intentionally excluded.
const PinDef BOARD_PINS[] = {{0,true},{1,true},{2,true},{3,true},{6,true},{7,true},{10,true},{11,true},{15,true},{18,true},{19,true},{20,true},{21,true},{22,true},{23,true}};
#elif defined(CONFIG_IDF_TARGET_ESP32C2)
const PinDef BOARD_PINS[] = {{0,true},{1,true},{2,true},{3,true},{4,true},{5,true},{6,true},{7,true},{10,true}};
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
const PinDef BOARD_PINS[] = {{1,true},{2,true},{3,true},{4,true},{5,true},{6,true},{7,true},{8,true},{9,true},{10,true},{11,true},{12,true},{13,true},{14,true},{15,true},{16,true},{17,true},{18,true},{21,true}};
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
const PinDef BOARD_PINS[] = {{1,true},{2,true},{4,true},{5,true},{6,true},{7,true},{8,true},{9,true},{10,true},{11,true},{12,true},{13,true},{14,true},{15,true},{16,true},{17,true},{18,true},{21,true},{38,true},{39,true},{40,true},{41,true},{42,true},{47,true},{48,true}};
#else
// ESP32-WROOM/DevKit: no flash GPIO6-11, UART0, boot straps, or input-only GPIO34-39.
const PinDef BOARD_PINS[] = {{2,true},{4,true},{5,true},{12,true},{13,true},{14,true},{16,true},{17,true},{18,true},{19,true},{21,true},{22,true},{23,true},{25,true},{26,true},{27,true},{32,true},{33,true}};
#endif
constexpr uint8_t BOARD_PIN_COUNT = sizeof(BOARD_PINS) / sizeof(BOARD_PINS[0]);

struct OutputConfig {
  int8_t pin = -1;
  uint16_t channel = 1;
  uint8_t mode = OUTPUT_DIGITAL;
  bool inverted = false;
  uint8_t threshold = 128;
};

struct Config {
  String wifiSsid;
  String wifiPassword;
  bool staticIpEnabled = false;
  String staticIp;
  String gateway;
  String subnet = "255.255.255.0";
  String name = "ArtNet Devices";
  uint16_t universe = 0;
  OutputConfig outputs[MAX_OUTPUTS];
};

Config config;
#if defined(ESP8266)
ESP8266WebServer server(80);
#else
Preferences prefs;
WebServer server(80);
#endif
WiFiUDP artnet;
DNSServer captiveDns;
bool accessPointMode = false;
bool networkConnecting = false;
uint32_t networkStartedAt = 0;
uint8_t outputValues[MAX_OUTPUTS] = {};

const char INDEX_HTML[] PROGMEM = R"html(
<!doctype html><html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ArtNet Devices</title><style>
:root{--a:#ff9f1c;--b:#10151d;--p:#1d2633;--l:#38465a;--t:#edf4ff;--m:#aebacc;--g:#40da88;--r:#ff7180}*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at top,#293958,#10151d 55%);color:var(--t);font:15px system-ui,sans-serif}.bar{display:flex;gap:12px;align-items:center;padding:15px max(18px,calc((100% - 1060px)/2));border-bottom:1px solid var(--l);background:#151d29dd}.logo{width:32px;height:32px;border-radius:9px;display:grid;place-items:center;background:var(--a);color:#17202a;font-weight:900}.bar h1{font-size:18px;margin:0}.tag{margin-left:auto;color:var(--m);font-size:12px}main{max-width:1060px;margin:auto;padding:24px 18px 50px}.tabs{display:flex;gap:8px;margin-bottom:16px;flex-wrap:wrap}.tabs button,.button{border:1px solid var(--l);border-radius:8px;padding:9px 13px;background:#1a2431;color:var(--t);font:inherit;cursor:pointer}.tabs .on,.primary{background:var(--a);border-color:var(--a);color:#17202a;font-weight:750}.page{display:none}.page.on{display:block}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(285px,1fr));gap:16px}.card{background:linear-gradient(145deg,#202b3a,#19212c);border:1px solid var(--l);border-radius:14px;padding:19px}.card h2{font-size:17px;margin:0 0 13px}.wide{grid-column:1/-1}.state{font-size:27px;font-weight:800}.muted,.note{color:var(--m);line-height:1.55}.note{font-size:13px;background:#111925;border-radius:8px;padding:11px}.ok{color:var(--g)}.bad{color:var(--r)}label{display:block;margin:11px 0 5px;color:#ced9e8;font-size:13px}input,select{width:100%;padding:9px;border-radius:8px;border:1px solid var(--l);background:#101720;color:var(--t);font:inherit}.row{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}.output{border:1px solid var(--l);border-radius:11px;padding:14px;margin-top:11px;background:#151d28}.outhead{display:flex;justify-content:space-between;gap:10px;align-items:center}.outhead b{font-size:14px}.remove{color:#ffd5da;background:#442730;border-color:#8c4a56;padding:5px 9px}.range{display:flex;align-items:center;gap:10px}.range input{padding:0}.range output{min-width:32px;text-align:right}.pins{display:flex;gap:6px;flex-wrap:wrap}.pin{padding:5px 7px;border-radius:7px;background:#142031;color:#bcd0e9;font-size:12px}.pin.led{background:#3c2d18;color:#ffd490}.hidden{display:none}@media(max-width:600px){.row{grid-template-columns:1fr}}
</style></head><body><nav class="bar"><div class="logo">A</div><h1>ArtNet Devices</h1><span class="tag" id="mode">Cargando…</span></nav><main><div class="tabs"><button class="on" data-p="home">Inicio</button><button data-p="setup">Configuración</button><button data-p="ota">Firmware</button></div>
<section id="home" class="page on"><div class="grid"><article class="card"><h2>Dispositivo</h2><div class="state" id="board">—</div><p class="muted">Nombre Art‑Net: <b id="name">—</b><br>Universo global: <b id="universe">—</b><br>IP: <b id="ip">—</b></p></article><article class="card"><h2>Salidas DMX</h2><div id="homeOutputs" class="muted">Sin salidas configuradas.</div></article><article class="card wide"><h2>GPIO de esta placa</h2><p class="muted" id="pinInfo">—</p><div class="pins" id="pins"></div><p class="note">Sólo se muestran GPIO seguros de salida del perfil instalado. Los pines de flash, USB, UART, arranque y entrada exclusiva no aparecen como opciones.</p></article></div></section>
<section id="setup" class="page"><div class="grid"><form class="card" id="identity"><h2>Identidad y red</h2><label>Nombre del dispositivo</label><input id="deviceName" maxlength="63" required><label>Universo Art‑Net global</label><input id="artnetUniverse" type="number" min="0" max="32767" required><label>Red Wi‑Fi</label><div class="row"><select id="wifiNetworks"><option value="">Buscar redes…</option></select><button type="button" class="button" id="scan">Buscar redes</button></div><input id="wifiSsid" maxlength="32" placeholder="SSID"><label>Contraseña</label><input id="wifiPassword" maxlength="63" type="password" placeholder="Vacía = conservar"><button class="button primary">Guardar y reiniciar</button><p id="identityMsg" class="muted"></p></form><section class="card"><h2>Dirección IP</h2><label><input id="staticIpEnabled" type="checkbox" style="width:auto"> Usar IP estática</label><div id="ipFields"><label>IP</label><input id="staticIp" placeholder="192.168.1.50"><div class="row"><div><label>Puerta de enlace</label><input id="gateway" placeholder="192.168.1.1"></div><div><label>Máscara</label><input id="subnet" value="255.255.255.0"></div></div></div><p class="note">La red se guarda sin mostrar nunca la contraseña.</p></section><form class="card wide" id="outputs"><h2>Asignación de pines a canales DMX</h2><p class="muted">Cada salida usa un canal del universo global. <b>Digital</b> funciona como relé ON/OFF; <b>PWM</b> usa el valor DMX 0–255 para fundidos.</p><div id="outputList"></div><button class="button" type="button" id="addOutput">Añadir salida</button><button class="button primary">Guardar salidas y reiniciar</button><p id="outputMsg" class="muted"></p></form></div></section>
<section id="ota" class="page"><div class="grid"><form class="card" id="update"><h2>Actualización OTA</h2><p class="note">Selecciona únicamente la imagen OTA <code>firmware.bin</code> de la misma familia de chip. No desconectes la alimentación durante el proceso.</p><input id="firmware" type="file" accept=".bin" required><button class="button primary">Instalar actualización</button><p id="updateMsg" class="muted"></p></form></div></section></main><script>
const $=x=>document.getElementById(x);let state={},draft=[];document.querySelectorAll('.tabs button').forEach(b=>b.onclick=()=>{document.querySelectorAll('.tabs button,.page').forEach(x=>x.classList.remove('on'));b.classList.add('on');$(b.dataset.p).classList.add('on')});
const esc=s=>String(s).replace(/[&<>\"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
function optionPins(selected){return '<option value="-1">Seleccionar GPIO…</option>'+state.pins.map(p=>`<option value="${p.gpio}" ${p.reserved?'disabled':''} ${p.gpio==selected?'selected':''}>GPIO ${p.gpio}${p.reserved?' — LED de placa (reservado)':p.pwm?' — PWM':' — digital'}</option>`).join('')}
function renderOutputs(){let root=$('outputList');root.innerHTML=draft.map((o,i)=>`<div class="output"><div class="outhead"><b>Salida ${i+1}</b><button type="button" class="button remove" data-remove="${i}">Quitar</button></div><div class="row"><div><label>GPIO</label><select data-i="${i}" data-k="pin">${optionPins(o.pin)}</select></div><div><label>Canal DMX</label><input data-i="${i}" data-k="channel" type="number" min="1" max="512" value="${o.channel||1}"></div></div><div class="row"><div><label>Tipo</label><select data-i="${i}" data-k="mode"><option value="0" ${+o.mode===0?'selected':''}>Digital · ON/OFF</option><option value="1" ${+o.mode===1?'selected':''}>PWM · dimmer 0–255</option></select></div><div><label>Invertir polaridad</label><select data-i="${i}" data-k="inverted"><option value="false" ${!o.inverted?'selected':''}>No</option><option value="true" ${o.inverted?'selected':''}>Sí</option></select></div></div><div class="row"><div><label>Umbral digital</label><input data-i="${i}" data-k="threshold" type="number" min="0" max="255" value="${o.threshold??128}"></div><div><label>Prueba / intensidad</label><div class="range"><input data-test="${i}" type="range" min="0" max="255" value="${o.value||0}"><output id="v${i}">${o.value||0}</output></div></div></div></div>`).join('')||'<p class="note">Aún no hay ninguna salida. Añade un GPIO seguro.</p>';
 root.querySelectorAll('[data-remove]').forEach(b=>b.onclick=()=>{draft.splice(+b.dataset.remove,1);renderOutputs()});root.querySelectorAll('[data-k]').forEach(e=>e.onchange=()=>{let o=draft[+e.dataset.i],v=e.value;o[e.dataset.k]=e.dataset.k==='inverted'?v==='true':+v;if(e.dataset.k==='pin'){let p=state.pins.find(x=>x.gpio===+v);if(p&&!p.pwm)o.mode=0} });root.querySelectorAll('[data-test]').forEach(r=>r.oninput=async()=>{let i=+r.dataset.test,v=+r.value;draft[i].value=v;$('v'+i).value=v;await fetch('/api/test',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({slot:i,value:v})})})}
function fill(s){state=s;draft=(s.outputs||[]).map(o=>({...o}));$('mode').textContent=s.ap?'Modo configuración':'En red';$('board').textContent=s.board;$('name').textContent=s.name;$('universe').textContent=s.universe;$('ip').textContent=s.ip;$('deviceName').value=s.name;$('artnetUniverse').value=s.universe;$('wifiSsid').value=s.wifiSsid||'';$('staticIpEnabled').checked=!!s.staticIpEnabled;$('staticIp').value=s.staticIp||'';$('gateway').value=s.gateway||'';$('subnet').value=s.subnet||'255.255.255.0';$('ipFields').classList.toggle('hidden',!s.staticIpEnabled);$('pinInfo').textContent=`${s.safePinCount} GPIO seguros de salida · LED de placa: GPIO ${s.statusLedPin}${s.statusLedInverted?' (activo bajo)':''} · PWM indicado por pin.`;$('pins').innerHTML=s.pins.map(p=>`<span class="pin ${p.reserved?'led':''}">GPIO ${p.gpio} · ${p.reserved?'LED de placa':p.pwm?'PWM / digital':'digital'}</span>`).join('');$('homeOutputs').innerHTML=draft.length?draft.map(o=>`GPIO <b>${o.pin}</b> · DMX ${o.channel} · ${+o.mode?'PWM '+o.value:'digital '+(o.value>=o.threshold?'ON':'OFF')}`).join('<br>'):'Sin salidas configuradas.';renderOutputs()}
async function refresh(){try{fill(await (await fetch('/api/status')).json())}catch(e){$('mode').textContent='Sin conexión'}}
async function save(data,msg){let r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});$(msg).textContent=r.ok?'Guardado. Reiniciando…':'Configuración rechazada: revisa GPIO y canales.';$(msg).className=r.ok?'ok':'bad'}
$('identity').onsubmit=e=>{e.preventDefault();save({deviceName:$('deviceName').value,artnetUniverse:+$('artnetUniverse').value,wifiSsid:$('wifiSsid').value,wifiPassword:$('wifiPassword').value,staticIpEnabled:$('staticIpEnabled').checked,staticIp:$('staticIp').value,gateway:$('gateway').value,subnet:$('subnet').value},'identityMsg')};$('outputs').onsubmit=e=>{e.preventDefault();save({outputs:draft},'outputMsg')};$('addOutput').onclick=()=>{if(draft.length<24){draft.push({pin:-1,channel:1,mode:0,inverted:false,threshold:128,value:0});renderOutputs()}};$('staticIpEnabled').onchange=e=>$('ipFields').classList.toggle('hidden',!e.target.checked);$('scan').onclick=async()=>{let a=await (await fetch('/api/wifi-scan')).json();$('wifiNetworks').innerHTML='<option value="">Seleccionar red…</option>'+a.map(x=>'<option>'+esc(x)+'</option>').join('')};$('wifiNetworks').onchange=e=>{if(e.target.value)$('wifiSsid').value=e.target.value};$('update').onsubmit=async e=>{e.preventDefault();let f=$('firmware').files[0];if(!f)return;let d=new FormData();d.append('firmware',f);$('updateMsg').textContent='Subiendo…';let r=await fetch('/update',{method:'POST',body:d});$('updateMsg').textContent=r.ok?'Actualización correcta. Reiniciando…':'La actualización falló'};refresh();setInterval(refresh,2500);
</script></body></html>
)html";

bool isKnownPin(int pin) { for (uint8_t i=0;i<BOARD_PIN_COUNT;i++) if (BOARD_PINS[i].gpio == pin) return true; return false; }
bool pinSupportsPwm(int pin) { for (uint8_t i=0;i<BOARD_PIN_COUNT;i++) if (BOARD_PINS[i].gpio == pin) return BOARD_PINS[i].pwm; return false; }
bool isAllowedOutput(int pin) { return pin != STATUS_LED_PIN && isKnownPin(pin); }
uint8_t safePinCount() { uint8_t n=0; for(uint8_t i=0;i<BOARD_PIN_COUNT;i++) if(isAllowedOutput(BOARD_PINS[i].gpio)) n++; return n; }
void clearOutputs() { for (uint8_t i=0;i<MAX_OUTPUTS;i++) config.outputs[i] = OutputConfig(); }
void defaults() { clearOutputs(); config.name="ArtNet Devices"; config.universe=0; config.outputs[0].pin=DEFAULT_OUTPUT_PIN; config.outputs[0].channel=1; }

#if defined(ESP8266)
constexpr uint32_t CONFIG_MAGIC = 0x414E4432;
constexpr uint32_t LEGACY_MAGIC = 0x41524E55;
struct LegacyConfig { uint32_t magic; char ssid[33]; char password[64]; bool stat; char ip[16]; char gateway[16]; char subnet[16]; char name[64]; uint16_t universe; uint16_t channel; uint8_t pin; bool inverted; };
struct StoredConfig { uint32_t magic; char ssid[33]; char password[64]; bool stat; char ip[16]; char gateway[16]; char subnet[16]; char name[64]; uint16_t universe; OutputConfig outputs[MAX_OUTPUTS]; };
void loadConfig() { defaults(); EEPROM.begin(sizeof(StoredConfig)); StoredConfig stored={}; EEPROM.get(0,stored); if(stored.magic==CONFIG_MAGIC){config.wifiSsid=stored.ssid;config.wifiPassword=stored.password;config.staticIpEnabled=stored.stat;config.staticIp=stored.ip;config.gateway=stored.gateway;config.subnet=stored.subnet;config.name=stored.name;config.universe=stored.universe;memcpy(config.outputs,stored.outputs,sizeof(config.outputs));return;} LegacyConfig old={};EEPROM.get(0,old);if(old.magic==LEGACY_MAGIC){config.wifiSsid=old.ssid;config.wifiPassword=old.password;config.staticIpEnabled=old.stat;config.staticIp=old.ip;config.gateway=old.gateway;config.subnet=old.subnet;config.name=old.name;config.universe=old.universe;config.outputs[0].pin=old.pin;config.outputs[0].channel=old.channel;config.outputs[0].inverted=old.inverted;}}
void saveConfig(){StoredConfig s={};s.magic=CONFIG_MAGIC;strncpy(s.ssid,config.wifiSsid.c_str(),32);strncpy(s.password,config.wifiPassword.c_str(),63);s.stat=config.staticIpEnabled;strncpy(s.ip,config.staticIp.c_str(),15);strncpy(s.gateway,config.gateway.c_str(),15);strncpy(s.subnet,config.subnet.c_str(),15);strncpy(s.name,config.name.c_str(),63);s.universe=config.universe;memcpy(s.outputs,config.outputs,sizeof(s.outputs));EEPROM.put(0,s);EEPROM.commit();}
#else
void loadConfig(){defaults();prefs.begin("artnetrelay",true);config.wifiSsid=prefs.getString("ssid","");config.wifiPassword=prefs.getString("password","");config.staticIpEnabled=prefs.getBool("static",false);config.staticIp=prefs.getString("ip","");config.gateway=prefs.getString("gateway","");config.subnet=prefs.getString("subnet",config.subnet);config.name=prefs.getString("name",config.name);config.universe=prefs.getUShort("universe",0);if(prefs.getBytesLength("outputs")==sizeof(config.outputs))prefs.getBytes("outputs",config.outputs,sizeof(config.outputs));else{config.outputs[0].pin=prefs.getUChar("pin",DEFAULT_OUTPUT_PIN);config.outputs[0].channel=prefs.getUShort("channel",1);config.outputs[0].inverted=prefs.getBool("inverted",false);}prefs.end();}
void saveConfig(){prefs.begin("artnetrelay",false);prefs.putString("ssid",config.wifiSsid);prefs.putString("password",config.wifiPassword);prefs.putBool("static",config.staticIpEnabled);prefs.putString("ip",config.staticIp);prefs.putString("gateway",config.gateway);prefs.putString("subnet",config.subnet);prefs.putString("name",config.name);prefs.putUShort("universe",config.universe);prefs.putBytes("outputs",config.outputs,sizeof(config.outputs));prefs.end();}
#endif

String jsonEscape(const String& v){String r;for(size_t i=0;i<v.length();i++){char c=v[i];if(c=='\\'||c=='"')r+='\\';if(c!='\n'&&c!='\r')r+=c;}return r;}
String jsonValue(const String& body,const char* key){String n=String("\"")+key+"\"";int k=body.indexOf(n);if(k<0)return"";int c=body.indexOf(':',k+n.length());if(c<0)return"";int s=c+1;while(s<(int)body.length()&&(body[s]==' '||body[s]=='\"'))s++;int e=s;while(e<(int)body.length()&&body[e]!='\"'&&body[e]!=','&&body[e]!='}')e++;return body.substring(s,e);}
bool validIp(const String& s){IPAddress p;return p.fromString(s);}
bool validateOutputs(){bool used[50]={};for(uint8_t i=0;i<MAX_OUTPUTS;i++){OutputConfig&o=config.outputs[i];if(o.pin<0)continue;if(!isAllowedOutput(o.pin)||o.channel<1||o.channel>512||o.mode>OUTPUT_PWM||(o.mode==OUTPUT_PWM&&!pinSupportsPwm(o.pin))||used[(uint8_t)o.pin])return false;used[(uint8_t)o.pin]=true;}return true;}
void parseOutputs(const String& body){int key=body.indexOf("\"outputs\"");if(key<0)return;int pos=body.indexOf('[',key),end=body.indexOf(']',pos);if(pos<0||end<0)return;clearOutputs();uint8_t slot=0;while(slot<MAX_OUTPUTS){int a=body.indexOf('{',pos);if(a<0||a>end)break;int b=body.indexOf('}',a);if(b<0||b>end)break;String o=body.substring(a,b+1);OutputConfig&out=config.outputs[slot++];String v=jsonValue(o,"pin");out.pin=v.length()?v.toInt():-1;v=jsonValue(o,"channel");out.channel=constrain(v.toInt(),1,512);v=jsonValue(o,"mode");out.mode=constrain(v.toInt(),0,1);v=jsonValue(o,"inverted");out.inverted=v=="true";v=jsonValue(o,"threshold");out.threshold=constrain(v.toInt(),0,255);pos=b+1;}}
bool applyConfigJson(const String& body){String v=jsonValue(body,"deviceName");if(v.length())config.name=v.substring(0,63);v=jsonValue(body,"artnetUniverse");if(v.length())config.universe=constrain(v.toInt(),0,32767);v=jsonValue(body,"wifiSsid");if(v.length())config.wifiSsid=v.substring(0,32);v=jsonValue(body,"wifiPassword");if(v.length())config.wifiPassword=v.substring(0,63);v=jsonValue(body,"staticIpEnabled");if(v.length())config.staticIpEnabled=v=="true";v=jsonValue(body,"staticIp");if(v.length())config.staticIp=v;v=jsonValue(body,"gateway");if(v.length())config.gateway=v;v=jsonValue(body,"subnet");if(v.length())config.subnet=v;parseOutputs(body);if(config.staticIpEnabled&&(!validIp(config.staticIp)||!validIp(config.gateway)||!validIp(config.subnet)))return false;if(!validateOutputs())return false;saveConfig();return true;}

void setStatusLed(bool on){digitalWrite(STATUS_LED_PIN,(on^STATUS_LED_INVERTED)?HIGH:LOW);}
void applyOutput(uint8_t i,uint8_t value){if(i>=MAX_OUTPUTS)return;OutputConfig&o=config.outputs[i];if(o.pin<0||!isAllowedOutput(o.pin))return;outputValues[i]=value;if(o.mode==OUTPUT_PWM){uint8_t duty=o.inverted?255-value:value;
#if defined(ESP8266)
analogWrite(o.pin,duty);
#else
ledcWrite(o.pin,duty);
#endif
}else digitalWrite(o.pin,((value>=o.threshold)^o.inverted)?HIGH:LOW);bool active=false;for(uint8_t n=0;n<MAX_OUTPUTS;n++)if(config.outputs[n].pin>=0&&outputValues[n]>0)active=true;setStatusLed(active);}
void setupOutputs(){pinMode(STATUS_LED_PIN,OUTPUT);setStatusLed(false);for(uint8_t i=0;i<MAX_OUTPUTS;i++){OutputConfig&o=config.outputs[i];if(!isAllowedOutput(o.pin))continue;pinMode(o.pin,OUTPUT);if(o.mode==OUTPUT_PWM){
#if defined(ESP8266)
analogWriteRange(255);
#else
ledcAttach(o.pin,1000,8);
#endif
}applyOutput(i,0);}}

String pinsJson(){String r="[";for(uint8_t i=0;i<BOARD_PIN_COUNT;i++){if(i)r+=',';const PinDef&p=BOARD_PINS[i];r+="{\"gpio\":"+String(p.gpio)+",\"pwm\":"+(p.pwm?"true":"false")+",\"reserved\":"+(p.gpio==STATUS_LED_PIN?"true":"false")+"}";}return r+"]";}
String outputsJson(){String r="[";bool first=true;for(uint8_t i=0;i<MAX_OUTPUTS;i++){const OutputConfig&o=config.outputs[i];if(o.pin<0)continue;if(!first)r+=',';first=false;r+="{\"slot\":"+String(i)+",\"pin\":"+String(o.pin)+",\"channel\":"+String(o.channel)+",\"mode\":"+String(o.mode)+",\"inverted\":"+(o.inverted?"true":"false")+",\"threshold\":"+String(o.threshold)+",\"value\":"+String(outputValues[i])+"}";}return r+"]";}
String configJson(){String ip=accessPointMode?WiFi.softAPIP().toString():WiFi.localIP().toString();return "{\"name\":\""+jsonEscape(config.name)+"\",\"deviceName\":\""+jsonEscape(config.name)+"\",\"board\":\""+String(DEVICE_PROFILE)+"\",\"universe\":"+String(config.universe)+",\"artnetUniverse\":"+String(config.universe)+",\"wifiSsid\":\""+jsonEscape(config.wifiSsid)+"\",\"staticIpEnabled\":"+(config.staticIpEnabled?"true":"false")+",\"staticIp\":\""+jsonEscape(config.staticIp)+"\",\"gateway\":\""+jsonEscape(config.gateway)+"\",\"subnet\":\""+jsonEscape(config.subnet)+"\",\"ip\":\""+ip+"\",\"ap\":"+(accessPointMode?"true":"false")+",\"statusLedPin\":"+String(STATUS_LED_PIN)+",\"statusLedInverted\":"+(STATUS_LED_INVERTED?"true":"false")+",\"safePinCount\":"+String(safePinCount())+",\"pins\":"+pinsJson()+",\"outputs\":"+outputsJson()+"}";}
String cachedWifiScanJson;
bool wifiScanCached = false;
String wifiScanJson(){
  // Repeated scan/delete cycles can reset the C3/C6 Wi-Fi driver while a USB
  // client is reading the result. Keep one copied, bounded scan per boot; a
  // hidden network can always be entered in the SSID field.
  if(wifiScanCached)return cachedWifiScanJson;
  int n=WiFi.scanNetworks(false,true);
  String r; r.reserve(640); r="[";
  uint8_t added=0;
  for(int i=0;i<n&&added<20;i++){String ssid=WiFi.SSID(i);if(!ssid.length())continue;if(added++)r+=',';r+='"';r+=jsonEscape(ssid.substring(0,32));r+='"';}
  r+=']';cachedWifiScanJson=r;wifiScanCached=true;return cachedWifiScanJson;
}

String safeHostname(){String h=config.name;h.toLowerCase();for(size_t i=0;i<h.length();i++)if(!isAlphaNumeric(h[i]))h.setCharAt(i,'-');while(h.indexOf("--")>=0)h.replace("--","-");if(!h.length())h="artnet-devices";return h.substring(0,63);}
void beginAp(){accessPointMode=true;WiFi.mode(WIFI_AP_STA);WiFi.softAP("ArtNet-Devices-Setup");captiveDns.start(53,"*",WiFi.softAPIP());}
void startNetwork(){WiFi.mode(WIFI_STA);
#if defined(ESP8266)
WiFi.hostname(safeHostname());
#else
WiFi.setHostname(safeHostname().c_str());
#endif
if(!config.wifiSsid.length()){beginAp();MDNS.begin(safeHostname().c_str());MDNS.addService("http","tcp",80);return;}if(config.staticIpEnabled){IPAddress a,b,c;if(a.fromString(config.staticIp)&&b.fromString(config.gateway)&&c.fromString(config.subnet))WiFi.config(a,b,c);}WiFi.begin(config.wifiSsid.c_str(),config.wifiPassword.c_str());networkConnecting=true;networkStartedAt=millis();}
void serviceNetwork(){if(!networkConnecting)return;if(WiFi.status()==WL_CONNECTED){networkConnecting=false;MDNS.begin(safeHostname().c_str());MDNS.addService("http","tcp",80);return;}if(millis()-networkStartedAt>=15000){networkConnecting=false;beginAp();MDNS.begin(safeHostname().c_str());MDNS.addService("http","tcp",80);}}

void sendPollReply(IPAddress dst){uint8_t r[239]={};memcpy(r,"Art-Net\0",8);r[8]=0;r[9]=0x21;IPAddress ip=accessPointMode?WiFi.softAPIP():WiFi.localIP();for(uint8_t i=0;i<4;i++)r[10+i]=ip[i];r[14]=ARTNET_PORT>>8;r[15]=ARTNET_PORT&0xff;r[16]=0;r[17]=1;strncpy((char*)r+26,config.name.c_str(),17);String longName=config.name+" - ArtNet Devices";strncpy((char*)r+44,longName.c_str(),63);r[172]=0;r[173]=1;r[174]=0x80;r[190]=config.universe;r[194]=config.universe>>8;artnet.beginPacket(dst,ARTNET_PORT);artnet.write(r,sizeof(r));artnet.endPacket();}
void receiveArtnet(){int size=artnet.parsePacket();if(size<10)return;uint8_t d[530];int n=artnet.read(d,min(size,(int)sizeof(d)));if(n<10||memcmp(d,"Art-Net\0",8))return;uint16_t op=d[8]|d[9]<<8;if(op==0x2000){sendPollReply(artnet.remoteIP());return;}if(op!=0x5000||n<18)return;uint16_t uni=d[14]|d[15]<<8,len=d[16]<<8|d[17];if(uni!=config.universe||18+len>n)return;for(uint8_t i=0;i<MAX_OUTPUTS;i++){OutputConfig&o=config.outputs[i];if(o.pin>=0&&o.channel<=len)applyOutput(i,d[17+o.channel]);}}

void setupWebServer(){
#if !defined(ESP8266)
server.enableCORS(true);
#endif
server.on("/",HTTP_GET,[]{server.send_P(200,"text/html; charset=utf-8",INDEX_HTML);});server.on("/generate_204",HTTP_GET,[]{server.sendHeader("Location","/",true);server.send(302,"text/plain","");});server.on("/hotspot-detect.html",HTTP_GET,[]{server.sendHeader("Location","/",true);server.send(302,"text/plain","");});server.on("/connecttest.txt",HTTP_GET,[]{server.sendHeader("Location","/",true);server.send(302,"text/plain","");});server.on("/ncsi.txt",HTTP_GET,[]{server.sendHeader("Location","/",true);server.send(302,"text/plain","");});server.on("/api/status",HTTP_GET,[]{server.send(200,"application/json",configJson());});server.on("/api/wifi-scan",HTTP_GET,[]{server.send(200,"application/json",wifiScanJson());});server.on("/api/test",HTTP_POST,[]{String b=server.arg("plain");int slot=jsonValue(b,"slot").toInt(),value=constrain(jsonValue(b,"value").toInt(),0,255);if(slot<0||slot>=MAX_OUTPUTS||config.outputs[slot].pin<0){server.send(400,"application/json","{\"ok\":false}");return;}applyOutput(slot,value);server.send(200,"application/json","{\"ok\":true}");});server.on("/api/config",HTTP_POST,[]{bool ok=applyConfigJson(server.arg("plain"));server.send(ok?200:400,"application/json",ok?"{\"ok\":true}":"{\"ok\":false,\"error\":\"GPIO o configuración inválida\"}");if(ok){delay(700);ESP.restart();}});server.on("/update",HTTP_POST,[]{server.sendHeader("Connection","close");server.send(Update.hasError()?500:200,"text/plain",Update.hasError()?"FAILED":"OK");delay(700);if(!Update.hasError())ESP.restart();},[]{HTTPUpload&u=server.upload();if(u.status==UPLOAD_FILE_START)Update.begin(UPDATE_SIZE_UNKNOWN);else if(u.status==UPLOAD_FILE_WRITE)Update.write(u.buf,u.currentSize);else if(u.status==UPLOAD_FILE_END)Update.end(true);});server.onNotFound([]{if(accessPointMode){server.sendHeader("Location",String("http://")+WiFi.softAPIP().toString()+"/",true);server.send(302,"text/plain","Opening ArtNet Devices setup...");}else server.send(404,"text/plain","Not found");});server.begin();}

String serialLine;
void serialCommand(const String& c){if(c=="ARCFG GET"){Serial.println(String("ARCFG ")+configJson());return;}if(c=="ARCFG SCAN"){Serial.println(String("ARCFG ")+wifiScanJson());return;}if(c=="ARCFG PING"){Serial.println("ARCFG PONG");return;}if(c.startsWith("ARCFG SET ")){bool ok=applyConfigJson(c.substring(10));Serial.println(ok?"ARCFG OK":"ARCFG ERROR GPIO o configuración inválida");Serial.flush();if(ok){delay(900);ESP.restart();}}}
void handleTextSerial(uint8_t c){if(c=='\r')return;if(c=='\n'){if(serialLine.length())serialCommand(serialLine);serialLine="";}else if(serialLine.length()<4096)serialLine+=(char)c;else serialLine="";}

// ESP Web Tools speaks Improv after flashing.  Returning the AP or Wi-Fi URL
// is what enables its built-in "Visit device" button without a custom popup.
constexpr uint8_t IMPROV_VERSION=1, IMPROV_STATE=0x01, IMPROV_ERROR=0x02, IMPROV_RPC=0x03, IMPROV_RESULT=0x04;
constexpr uint8_t IMPROV_READY=0x02, IMPROV_PROVISIONING=0x03, IMPROV_PROVISIONED=0x04;
uint8_t improv[266]; uint16_t improvLength=0, improvExpected=0;
bool improvReachable(){return accessPointMode||WiFi.status()==WL_CONNECTED;}
String improvUrl(){return String("http://")+(WiFi.status()==WL_CONNECTED?WiFi.localIP():WiFi.softAPIP()).toString();}
void sendImprov(uint8_t type,const uint8_t* data,uint8_t length){uint8_t p[266]={'I','M','P','R','O','V',IMPROV_VERSION,type,length};if(length)memcpy(p+9,data,length);uint8_t sum=0;for(uint16_t i=0;i<9+length;i++)sum+=p[i];p[9+length]=sum;Serial.write(p,10+length);Serial.write('\n');}
void sendImprovState(){uint8_t s=improvReachable()?IMPROV_PROVISIONED:IMPROV_READY;sendImprov(IMPROV_STATE,&s,1);}
void sendImprovResult(uint8_t command,const String* values,uint8_t count){uint8_t d[255]={command,0};uint8_t used=0;for(uint8_t i=0;i<count;i++){uint8_t n=min((size_t)values[i].length(),(size_t)(252-used));if(used+n+1>252)break;d[2+used++]=n;for(uint8_t c=0;c<n;c++)d[2+used++]=values[i][c];}d[1]=used;sendImprov(IMPROV_RESULT,d,used+2);}
bool connectImprovWifi(const String& ssid,const String& password){
  config.wifiSsid=ssid.substring(0,32);config.wifiPassword=password.substring(0,63);saveConfig();
  captiveDns.stop();accessPointMode=false;WiFi.mode(WIFI_STA);
  #if defined(ESP8266)
  WiFi.hostname(safeHostname());
  #else
  WiFi.setHostname(safeHostname().c_str());
  #endif
  if(config.staticIpEnabled){IPAddress a,b,c;if(a.fromString(config.staticIp)&&b.fromString(config.gateway)&&c.fromString(config.subnet))WiFi.config(a,b,c);}
  WiFi.begin(config.wifiSsid.c_str(),config.wifiPassword.c_str());uint32_t started=millis();
  while(WiFi.status()!=WL_CONNECTED&&millis()-started<15000)delay(250);
  if(WiFi.status()!=WL_CONNECTED){beginAp();MDNS.begin(safeHostname().c_str());MDNS.addService("http","tcp",80);return false;}
  MDNS.begin(safeHostname().c_str());MDNS.addService("http","tcp",80);return true;
}
void processImprov(){if(improvLength<10||improv[6]!=IMPROV_VERSION||improv[7]!=IMPROV_RPC)return;uint8_t dataLength=improv[8],sum=0;for(uint16_t i=0;i<improvLength-1;i++)sum+=improv[i];if(sum!=improv[improvLength-1]||dataLength<2){uint8_t e=1;sendImprov(IMPROV_ERROR,&e,1);return;}const uint8_t* d=improv+9;if(d[1]!=dataLength-2){uint8_t e=1;sendImprov(IMPROV_ERROR,&e,1);return;}uint8_t command=d[0];if(command==0x01){uint8_t used=0;if(d[1]<2){uint8_t e=1;sendImprov(IMPROV_ERROR,&e,1);return;}uint8_t ssidLength=d[2];if(3+ssidLength>=dataLength){uint8_t e=1;sendImprov(IMPROV_ERROR,&e,1);return;}String ssid;for(uint8_t i=0;i<ssidLength;i++)ssid+=(char)d[3+i];used=3+ssidLength;uint8_t passwordLength=d[used++];if(used+passwordLength>dataLength){uint8_t e=1;sendImprov(IMPROV_ERROR,&e,1);return;}String password;for(uint8_t i=0;i<passwordLength;i++)password+=(char)d[used+i];uint8_t state=IMPROV_PROVISIONING;sendImprov(IMPROV_STATE,&state,1);if(!connectImprovWifi(ssid,password)){uint8_t e=3;sendImprov(IMPROV_ERROR,&e,1);sendImprovState();return;}sendImprovState();String v[]={improvUrl()};sendImprovResult(command,v,1);return;}if(command==0x02){sendImprovState();if(improvReachable()){String v[]={improvUrl()};sendImprovResult(command,v,1);}return;}if(command==0x03){String v[]={"ArtNet Devices","2.0.0",DEVICE_PROFILE,config.name};sendImprovResult(command,v,4);return;}if(command==0x07){String v[]={improvReachable()?"3":"2",improvUrl()};sendImprovResult(command,v,improvReachable()?2:1);return;}uint8_t e=2;sendImprov(IMPROV_ERROR,&e,1);}
void handleSerial(){static const char header[]="IMPROV";while(Serial.available()){uint8_t c=Serial.read();if(!improvLength){if(c=='I'){improv[0]=c;improvLength=1;}else handleTextSerial(c);continue;}improv[improvLength++]=c;if(improvLength<=6&&c!=(uint8_t)header[improvLength-1]){for(uint16_t i=0;i<improvLength;i++)handleTextSerial(improv[i]);improvLength=improvExpected=0;continue;}if(improvLength==9){improvExpected=10+improv[8];if(improvExpected>sizeof(improv)){improvLength=improvExpected=0;}}if(improvExpected&&improvLength==improvExpected){processImprov();improvLength=improvExpected=0;}}}

void setup(){Serial.begin(115200);Serial.println("ARCFG READY");loadConfig();if(!validateOutputs()){defaults();saveConfig();}setupOutputs();startNetwork();artnet.begin(ARTNET_PORT);setupWebServer();Serial.printf("ArtNet Devices ready: %s\n",DEVICE_PROFILE);}
void loop(){handleSerial();serviceNetwork();if(accessPointMode)captiveDns.processNextRequest();server.handleClient();receiveArtnet();}
