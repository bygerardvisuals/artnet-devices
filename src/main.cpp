#include <Arduino.h>
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

// ESP32 Art-Net Relay
// Default relay pin is GPIO 2. It can be changed from the web interface.
// Use a GPIO that is free on your particular ESP32 Pro Micro board.
constexpr uint16_t ARTNET_PORT = 6454;
constexpr uint8_t DEFAULT_RELAY_PIN = 2;
constexpr uint16_t DEFAULT_UNIVERSE = 0;
constexpr uint16_t DEFAULT_CHANNEL = 1; // DMX channels are 1-based.

// ESP32-C6 SuperMini has a WS2812 RGB status LED on GPIO 8.  It cannot be
// driven with digitalWrite(); it needs the ESP32 RGB LED peripheral.  Other
// boards use their ordinary built-in LED, with the pin and polarity supplied
// by their PlatformIO environment when necessary.
#if defined(CONFIG_IDF_TARGET_ESP32C6)
constexpr uint8_t STATUS_RGB_PIN = 8;
#else
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
#endif

struct Config {
  String wifiSsid;
  String wifiPassword;
  String name = "ArtNet Relay";
  uint16_t universe = DEFAULT_UNIVERSE;
  uint16_t channel = DEFAULT_CHANNEL;
  uint8_t relayPin = DEFAULT_RELAY_PIN;
  bool relayInverted = false;
};

Config config;
#if defined(ESP8266)
ESP8266WebServer server(80);
#else
Preferences prefs;
WebServer server(80);
#endif
WiFiUDP artnet;
bool accessPointMode = false;
bool relayState = false;
uint8_t lastDmxValue = 0;
uint32_t lastDmxMillis = 0;

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html><html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Art-Net Relay</title><style>
:root{--orange:#ff9f1c;--bg:#11151c;--card:#1d2430;--line:#344052;--text:#eef3fa;--muted:#a7b1c2;--green:#37d67a;--red:#ff5d5d}*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at top,#273451,#11151c 45%);color:var(--text);font:15px system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;min-height:100vh}.bar{height:62px;background:#171e29dd;backdrop-filter:blur(12px);border-bottom:1px solid var(--line);display:flex;align-items:center;padding:0 max(18px,calc((100vw - 940px)/2));gap:12px}.mark{width:29px;height:29px;border-radius:9px;background:var(--orange);display:grid;place-items:center;color:#18202b;font-weight:900}.bar h1{font-size:18px;margin:0;font-weight:650}.pill{margin-left:auto;border-radius:99px;background:#263246;padding:5px 10px;font-size:12px;color:var(--muted)}main{max-width:940px;margin:28px auto;padding:0 18px}.tabs{display:flex;gap:8px;margin-bottom:18px}.tabs button{border:1px solid var(--line);background:#19212d;color:var(--muted);padding:9px 14px;border-radius:9px;cursor:pointer}.tabs button.active{background:var(--orange);color:#1b2029;border-color:var(--orange);font-weight:700}.page{display:none}.page.active{display:block}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(285px,1fr));gap:17px}.card{background:linear-gradient(145deg,#202937,#1a202b);border:1px solid var(--line);border-radius:14px;padding:21px;box-shadow:0 12px 30px #0002}.card h2{font-size:17px;margin:0 0 16px}.status{display:flex;align-items:center;gap:14px}.dot{width:15px;height:15px;border-radius:50%;background:var(--red);box-shadow:0 0 18px var(--red)}.dot.on{background:var(--green);box-shadow:0 0 18px var(--green)}.state{font-size:27px;font-weight:750}.meta{margin-top:16px;color:var(--muted);line-height:1.7;font-size:13px}label{display:block;margin:12px 0 6px;color:#c8d2e0;font-size:13px}input,select{background:#111720;border:1px solid var(--line);border-radius:8px;width:100%;padding:10px;color:var(--text);font:inherit}input:focus{outline:2px solid #ff9f1c88;border-color:var(--orange)}.row{display:grid;grid-template-columns:1fr 1fr;gap:12px}button.primary{margin-top:19px;background:var(--orange);border:0;border-radius:8px;padding:11px 15px;color:#17202a;font-weight:750;cursor:pointer}.note{color:var(--muted);font-size:13px;line-height:1.55}.msg{margin-top:12px;color:var(--green);min-height:18px}.danger{background:#44262d!important;color:#ffdfe3!important;border:1px solid #934653!important}.file{padding:7px}.hidden{display:none}@media(max-width:520px){.row{grid-template-columns:1fr}main{margin-top:18px}}
</style></head><body><nav class="bar"><div class="mark">A</div><h1>Art‑Net Relay</h1><span class="pill" id="mode">Cargando…</span></nav><main>
<div class="tabs"><button class="active" data-page="home">Inicio</button><button data-page="setup">Configuración</button><button data-page="update">Firmware</button></div>
<section class="page active" id="home"><div class="grid"><div class="card"><h2>Salida de relé</h2><div class="status"><span class="dot" id="dot"></span><span class="state" id="state">—</span></div><div class="meta">Último valor DMX: <b id="value">—</b><br>Universo <b id="universe">—</b> · Canal <b id="channel">—</b><br>Última trama: <b id="last">—</b></div></div><div class="card"><h2>Red y nodo</h2><div class="meta">Nombre Art‑Net: <b id="name">—</b><br>IP: <b id="ip">—</b><br>SSID: <b id="ssid">—</b><br>Los controladores Art‑Net verán este equipo bajo el nombre configurado.</div></div></div></section>
<section class="page" id="setup"><div class="grid"><form class="card" id="wifiForm"><h2>Wi‑Fi</h2><label>Red Wi‑Fi</label><input id="wifiSsid" maxlength="32" required placeholder="Nombre de la red"><label>Contraseña</label><input id="wifiPassword" maxlength="63" type="password" placeholder="Déjala vacía para conservarla"><button class="primary">Guardar y reiniciar</button><div class="msg" id="wifiMsg"></div></form><form class="card" id="artnetForm"><h2>Art‑Net y relé</h2><label>Nombre del dispositivo</label><input id="deviceName" maxlength="63" required><div class="row"><div><label>Universo Art‑Net</label><input id="artnetUniverse" type="number" min="0" max="32767" required></div><div><label>Canal DMX</label><input id="artnetChannel" type="number" min="1" max="512" required></div></div><div class="row"><div><label>GPIO del relé</label><input id="relayPin" type="number" min="0" max="39" required></div><div><label>Polaridad</label><select id="relayInverted"><option value="false">Activo en HIGH</option><option value="true">Activo en LOW</option></select></div></div><p class="note">0–127 apaga el relé; 128–255 lo enciende. Para una señal estrictamente blanco/negro, envía 255 o 0.</p><button class="primary">Aplicar configuración</button><div class="msg" id="artnetMsg"></div></form></div></section>
<section class="page" id="update"><div class="grid"><form class="card" id="updateForm"><h2>Actualizar firmware</h2><p class="note">Selecciona un archivo <code>.bin</code> compilado para esta placa. No desconectes la alimentación durante el proceso.</p><input class="file" id="firmware" type="file" accept=".bin" required><button class="primary">Instalar actualización</button><div class="msg" id="updateMsg"></div></form><div class="card"><h2>Primera instalación</h2><p class="note">Conecta el ESP32 por USB y ejecuta <code>pio run -t upload</code> desde el proyecto. Después, si no hay una red Wi‑Fi guardada, conéctate a <b>ArtNet-Relay-Setup</b> y abre <b>192.168.4.1</b>.</p></div></div></section>
</main><script>
const $=id=>document.getElementById(id);let current={};
document.querySelectorAll('.tabs button').forEach(b=>b.onclick=()=>{document.querySelectorAll('.tabs button,.page').forEach(x=>x.classList.remove('active'));b.classList.add('active');$(b.dataset.page).classList.add('active')});
function fill(c){current=c;$('mode').textContent=c.ap?'Modo configuración':'Conectado';$('dot').className='dot '+(c.relay?'on':'');$('state').textContent=c.relay?'ENCENDIDO':'APAGADO';$('value').textContent=c.value;$('universe').textContent=c.universe;$('channel').textContent=c.channel;$('last').textContent=c.lastSeen?Math.round(c.lastSeen/1000)+' s':'sin datos';$('name').textContent=c.name;$('ip').textContent=c.ip;$('ssid').textContent=c.ssid||'—';$('wifiSsid').value=c.ssid||'';$('deviceName').value=c.name;$('artnetUniverse').value=c.universe;$('artnetChannel').value=c.channel;$('relayPin').value=c.relayPin;$('relayInverted').value=String(c.relayInverted)}
async function refresh(){try{fill(await (await fetch('/api/status')).json())}catch(e){$('mode').textContent='Sin conexión'}}refresh();setInterval(refresh,1500);
async function save(form,msg){let data=Object.fromEntries(new FormData(form));let r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});$(msg).textContent=r.ok?'Guardado. Reiniciando…':'No se pudo guardar';}
$('wifiForm').onsubmit=e=>{e.preventDefault();save(e.target,'wifiMsg')};$('artnetForm').onsubmit=e=>{e.preventDefault();save(e.target,'artnetMsg')};
$('updateForm').onsubmit=async e=>{e.preventDefault();let f=$('firmware').files[0];if(!f)return;let d=new FormData();d.append('firmware',f);$('updateMsg').textContent='Subiendo…';let r=await fetch('/update',{method:'POST',body:d});$('updateMsg').textContent=r.ok?'Actualización correcta. Reiniciando…':'La actualización falló'};
</script></body></html>)rawliteral";

String preferenceString(const char* key, const String& fallback = "") {
#if defined(ESP8266)
  (void)key;
  return fallback;
#else
  return prefs.getString(key, fallback);
#endif
}

#if defined(ESP8266)
constexpr uint32_t CONFIG_MAGIC = 0x41524E54; // "ARNT"
struct StoredConfig {
  uint32_t magic;
  char wifiSsid[33];
  char wifiPassword[64];
  char name[64];
  uint16_t universe;
  uint16_t channel;
  uint8_t relayPin;
  bool relayInverted;
};

void loadConfig() {
  EEPROM.begin(sizeof(StoredConfig));
  StoredConfig stored = {};
  EEPROM.get(0, stored);
  if (stored.magic == CONFIG_MAGIC) {
    config.wifiSsid = stored.wifiSsid;
    config.wifiPassword = stored.wifiPassword;
    config.name = stored.name;
    config.universe = stored.universe;
    config.channel = stored.channel;
    config.relayPin = stored.relayPin;
    config.relayInverted = stored.relayInverted;
  }
  if (config.name.length() == 0) config.name = "ArtNet Relay";
  if (config.channel < 1 || config.channel > 512) config.channel = DEFAULT_CHANNEL;
}

void saveConfig() {
  StoredConfig stored = {};
  stored.magic = CONFIG_MAGIC;
  strncpy(stored.wifiSsid, config.wifiSsid.c_str(), sizeof(stored.wifiSsid) - 1);
  strncpy(stored.wifiPassword, config.wifiPassword.c_str(), sizeof(stored.wifiPassword) - 1);
  strncpy(stored.name, config.name.c_str(), sizeof(stored.name) - 1);
  stored.universe = config.universe;
  stored.channel = config.channel;
  stored.relayPin = config.relayPin;
  stored.relayInverted = config.relayInverted;
  EEPROM.put(0, stored);
  EEPROM.commit();
}
#else
void loadConfig() {
  prefs.begin("artnetrelay", true);
  config.wifiSsid = preferenceString("ssid");
  config.wifiPassword = preferenceString("password");
  config.name = preferenceString("name", config.name);
  config.universe = prefs.getUShort("universe", DEFAULT_UNIVERSE);
  config.channel = prefs.getUShort("channel", DEFAULT_CHANNEL);
  config.relayPin = prefs.getUChar("pin", DEFAULT_RELAY_PIN);
  config.relayInverted = prefs.getBool("inverted", false);
  prefs.end();
  if (config.channel < 1 || config.channel > 512) config.channel = DEFAULT_CHANNEL;
}

void saveConfig() {
  prefs.begin("artnetrelay", false);
  prefs.putString("ssid", config.wifiSsid);
  prefs.putString("password", config.wifiPassword);
  prefs.putString("name", config.name);
  prefs.putUShort("universe", config.universe);
  prefs.putUShort("channel", config.channel);
  prefs.putUChar("pin", config.relayPin);
  prefs.putBool("inverted", config.relayInverted);
  prefs.end();
}
#endif

void setStatusLed(bool on) {
#if defined(CONFIG_IDF_TARGET_ESP32C6)
  // White when Art-Net enables the relay, fully off otherwise.
  rgbLedWrite(STATUS_RGB_PIN, on ? 255 : 0, on ? 255 : 0, on ? 255 : 0);
#else
  digitalWrite(STATUS_LED_PIN, (on ^ STATUS_LED_INVERTED) ? HIGH : LOW);
#endif
}

void setupStatusLed() {
#if defined(CONFIG_IDF_TARGET_ESP32C6)
  // rgbLedWrite configures the RMT output as needed, and starts with the LED off.
  rgbLedWrite(STATUS_RGB_PIN, 0, 0, 0);
#else
  pinMode(STATUS_LED_PIN, OUTPUT);
  setStatusLed(false);
#endif
}

void setRelay(bool on) {
  relayState = on;
  digitalWrite(config.relayPin, (on ^ config.relayInverted) ? HIGH : LOW);
  setStatusLed(on);
}

String jsonValue(const String& body, const char* key) {
  String needle = String("\"") + key + "\"";
  int keyPos = body.indexOf(needle);
  if (keyPos < 0) return "";
  int colon = body.indexOf(':', keyPos + needle.length());
  if (colon < 0) return "";
  int start = colon + 1;
  while (start < (int)body.length() && (body[start] == ' ' || body[start] == '\"')) start++;
  int end = start;
  while (end < (int)body.length() && body[end] != '\"' && body[end] != ',' && body[end] != '}') end++;
  return body.substring(start, end);
}

String safeHostname() {
  String host = config.name;
  host.toLowerCase();
  for (size_t i = 0; i < host.length(); ++i) {
    if (!isAlphaNumeric(host[i])) host.setCharAt(i, '-');
  }
  while (host.indexOf("--") >= 0) host.replace("--", "-");
  host.trim();
  if (host.length() == 0) host = "artnet-relay";
  return host.substring(0, 63);
}

void startNetwork() {
  WiFi.mode(WIFI_STA);
#if defined(ESP8266)
  WiFi.hostname(safeHostname());
#else
  WiFi.setHostname(safeHostname().c_str());
#endif
  if (config.wifiSsid.length()) {
    WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());
    uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) delay(250);
  }
  if (WiFi.status() != WL_CONNECTED) {
    accessPointMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ArtNet-Relay-Setup");
  }
  MDNS.begin(safeHostname().c_str());
  MDNS.addService("http", "tcp", 80);
}

void sendPollReply(IPAddress destination) {
  // ArtPollReply packet (Art-Net 4). The node name is visible in MadMapper and consoles.
  uint8_t reply[239] = {0};
  memcpy(reply, "Art-Net\0", 8);
  reply[8] = 0x00; reply[9] = 0x21; // OpPollReply (little endian)
  IPAddress ip = accessPointMode ? WiFi.softAPIP() : WiFi.localIP();
  for (uint8_t i = 0; i < 4; ++i) reply[10 + i] = ip[i];
  reply[14] = ARTNET_PORT >> 8; reply[15] = ARTNET_PORT & 0xff;
  reply[16] = 0; reply[17] = 1; // version
  strncpy(reinterpret_cast<char*>(reply + 26), config.name.c_str(), 17); // ShortName
  String longName = config.name + " - Art-Net Relay";
  strncpy(reinterpret_cast<char*>(reply + 44), longName.c_str(), 63);
  reply[172] = 0; reply[173] = 1; // NumPorts
  reply[174] = 0x80; // Port 1 is output capable
  reply[190] = config.universe & 0xff; // SwOut[0] (low byte)
  reply[194] = (config.universe >> 8) & 0x7f;
  artnet.beginPacket(destination, ARTNET_PORT);
  artnet.write(reply, sizeof(reply));
  artnet.endPacket();
}

void receiveArtnet() {
  int packetSize = artnet.parsePacket();
  if (packetSize < 10) return;
  uint8_t data[530];
  int count = artnet.read(data, min(packetSize, (int)sizeof(data)));
  if (count < 10 || memcmp(data, "Art-Net\0", 8) != 0) return;
  uint16_t opcode = data[8] | (data[9] << 8);
  if (opcode == 0x2000) { // ArtPoll
    sendPollReply(artnet.remoteIP());
    return;
  }
  if (opcode != 0x5000 || count < 18) return; // ArtDMX
  uint16_t universe = data[14] | (data[15] << 8);
  uint16_t length = (data[16] << 8) | data[17];
  if (universe != config.universe || config.channel > length || 18 + length > count) return;
  lastDmxValue = data[17 + config.channel];
  lastDmxMillis = millis();
  setRelay(lastDmxValue >= 128);
}

void setupWebServer() {
  // Allows the local Chrome configurator to call this device while it is on its setup AP.
#if !defined(ESP8266)
  server.enableCORS(true);
#endif
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); });
  server.on("/api/status", HTTP_GET, [] {
    String ip = accessPointMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    String ssid = accessPointMode ? "ArtNet-Relay-Setup" : WiFi.SSID();
    uint32_t age = lastDmxMillis ? millis() - lastDmxMillis : 0;
    String result = "{\"name\":\"" + config.name + "\",\"universe\":" + config.universe + ",\"channel\":" + config.channel + ",\"relayPin\":" + config.relayPin + ",\"relayInverted\":" + (config.relayInverted ? "true" : "false") + ",\"relay\":" + (relayState ? "true" : "false") + ",\"value\":" + lastDmxValue + ",\"lastSeen\":" + age + ",\"ip\":\"" + ip + "\",\"ssid\":\"" + ssid + "\",\"ap\":" + (accessPointMode ? "true" : "false") + "}";
    server.send(200, "application/json", result);
  });
  server.on("/api/config", HTTP_POST, [] {
    String body = server.arg("plain");
    String name = jsonValue(body, "deviceName");
    String universe = jsonValue(body, "artnetUniverse");
    String channel = jsonValue(body, "artnetChannel");
    String pin = jsonValue(body, "relayPin");
    String inverted = jsonValue(body, "relayInverted");
    String ssid = jsonValue(body, "wifiSsid");
    String password = jsonValue(body, "wifiPassword");
    if (name.length()) config.name = name;
    if (universe.length()) config.universe = constrain(universe.toInt(), 0, 32767);
    if (channel.length()) config.channel = constrain(channel.toInt(), 1, 512);
    if (pin.length()) config.relayPin = constrain(pin.toInt(), 0, 39);
    if (inverted.length()) config.relayInverted = inverted == "true";
    if (ssid.length()) config.wifiSsid = ssid;
    if (password.length()) config.wifiPassword = password;
    saveConfig();
    server.send(200, "application/json", "{\"ok\":true}");
    delay(700);
    ESP.restart();
  });
  server.on("/update", HTTP_POST, [] {
    server.sendHeader("Connection", "close");
    server.send(Update.hasError() ? 500 : 200, "text/plain", Update.hasError() ? "FAILED" : "OK");
    delay(700);
    if (!Update.hasError()) ESP.restart();
  }, [] {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
    else if (upload.status == UPLOAD_FILE_WRITE) Update.write(upload.buf, upload.currentSize);
    else if (upload.status == UPLOAD_FILE_END) Update.end(true);
  });
  server.begin();
}

void setup() {
  Serial.begin(115200);
  loadConfig();
  pinMode(config.relayPin, OUTPUT);
  setupStatusLed();
  setRelay(false);
  startNetwork();
  artnet.begin(ARTNET_PORT);
  setupWebServer();
}

void loop() {
  server.handleClient();
  receiveArtnet();
}
