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

// Arduino-ESP32 exposes this constant; Arduino-ESP8266 3.x accepts the same
// sentinel value but does not define its name.
#ifndef UPDATE_SIZE_UNKNOWN
  #define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF
#endif

// ESP32 Art-Net Relay
// Default relay pin is GPIO 2. It can be changed from the web interface.
// Use a GPIO that is free on your particular ESP32 Pro Micro board.
constexpr uint16_t ARTNET_PORT = 6454;
constexpr uint8_t DEFAULT_RELAY_PIN = 2;
constexpr uint16_t DEFAULT_UNIVERSE = 0;
constexpr uint16_t DEFAULT_CHANNEL = 1; // DMX channels are 1-based.

// Every target uses its board's built-in indicator as an ordinary GPIO. The
// pin and polarity are supplied by its PlatformIO environment when needed.
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

struct Config {
  String wifiSsid;
  String wifiPassword;
  bool staticIpEnabled = false;
  String staticIp;
  String gateway;
  String subnet = "255.255.255.0";
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
DNSServer captiveDns;
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
constexpr uint32_t CONFIG_MAGIC = 0x41524E55; // "ARNU" (USB-config capable layout)
struct StoredConfig {
  uint32_t magic;
  char wifiSsid[33];
  char wifiPassword[64];
  bool staticIpEnabled;
  char staticIp[16];
  char gateway[16];
  char subnet[16];
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
    config.staticIpEnabled = stored.staticIpEnabled;
    config.staticIp = stored.staticIp;
    config.gateway = stored.gateway;
    config.subnet = stored.subnet;
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
  stored.staticIpEnabled = config.staticIpEnabled;
  strncpy(stored.staticIp, config.staticIp.c_str(), sizeof(stored.staticIp) - 1);
  strncpy(stored.gateway, config.gateway.c_str(), sizeof(stored.gateway) - 1);
  strncpy(stored.subnet, config.subnet.c_str(), sizeof(stored.subnet) - 1);
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
  config.staticIpEnabled = prefs.getBool("static", false);
  config.staticIp = preferenceString("ip");
  config.gateway = preferenceString("gateway");
  config.subnet = preferenceString("subnet", config.subnet);
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
  prefs.putBool("static", config.staticIpEnabled);
  prefs.putString("ip", config.staticIp);
  prefs.putString("gateway", config.gateway);
  prefs.putString("subnet", config.subnet);
  prefs.putString("name", config.name);
  prefs.putUShort("universe", config.universe);
  prefs.putUShort("channel", config.channel);
  prefs.putUChar("pin", config.relayPin);
  prefs.putBool("inverted", config.relayInverted);
  prefs.end();
}
#endif

void setStatusLed(bool on) {
  digitalWrite(STATUS_LED_PIN, (on ^ STATUS_LED_INVERTED) ? HIGH : LOW);
}

void setupStatusLed() {
  pinMode(STATUS_LED_PIN, OUTPUT);
  setStatusLed(false);
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

String jsonEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value[i];
    if (c == '\\' || c == '"') escaped += '\\';
    if (c == '\n' || c == '\r') continue;
    escaped += c;
  }
  return escaped;
}

bool isValidIp(const String& value) {
  IPAddress address;
  return address.fromString(value);
}

bool applyConfigJson(const String& body) {
  String name = jsonValue(body, "deviceName");
  String universe = jsonValue(body, "artnetUniverse");
  String channel = jsonValue(body, "artnetChannel");
  String pin = jsonValue(body, "relayPin");
  String inverted = jsonValue(body, "relayInverted");
  String ssid = jsonValue(body, "wifiSsid");
  String password = jsonValue(body, "wifiPassword");
  String staticEnabled = jsonValue(body, "staticIpEnabled");
  String staticIp = jsonValue(body, "staticIp");
  String gateway = jsonValue(body, "gateway");
  String subnet = jsonValue(body, "subnet");
  if (name.length()) config.name = name.substring(0, 63);
  if (universe.length()) config.universe = constrain(universe.toInt(), 0, 32767);
  if (channel.length()) config.channel = constrain(channel.toInt(), 1, 512);
  if (pin.length()) config.relayPin = constrain(pin.toInt(), 0, 39);
  if (inverted.length()) config.relayInverted = inverted == "true";
  if (ssid.length()) config.wifiSsid = ssid.substring(0, 32);
  // An empty password means "keep the saved password", which lets the USB
  // configurator read settings without ever exposing credentials.
  if (password.length()) config.wifiPassword = password.substring(0, 63);
  if (staticEnabled.length()) config.staticIpEnabled = staticEnabled == "true";
  if (staticIp.length()) config.staticIp = staticIp;
  if (gateway.length()) config.gateway = gateway;
  if (subnet.length()) config.subnet = subnet;
  if (config.staticIpEnabled && (!isValidIp(config.staticIp) || !isValidIp(config.gateway) || !isValidIp(config.subnet))) return false;
  saveConfig();
  return true;
}

String configJson() {
  String ip = accessPointMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  String ssid = accessPointMode ? "ArtNet-Relay-Setup" : WiFi.SSID();
  uint32_t age = lastDmxMillis ? millis() - lastDmxMillis : 0;
  return "{\"name\":\"" + jsonEscape(config.name) + "\",\"deviceName\":\"" + jsonEscape(config.name) +
         "\",\"universe\":" + config.universe + ",\"artnetUniverse\":" + config.universe +
         ",\"channel\":" + config.channel + ",\"artnetChannel\":" + config.channel +
         ",\"relayPin\":" + config.relayPin + ",\"relayInverted\":" + (config.relayInverted ? "true" : "false") +
         ",\"wifiSsid\":\"" + jsonEscape(config.wifiSsid) + "\",\"staticIpEnabled\":" + (config.staticIpEnabled ? "true" : "false") +
         ",\"staticIp\":\"" + jsonEscape(config.staticIp) + "\",\"gateway\":\"" + jsonEscape(config.gateway) +
         "\",\"subnet\":\"" + jsonEscape(config.subnet) + "\",\"relay\":" + (relayState ? "true" : "false") +
         ",\"value\":" + lastDmxValue + ",\"lastSeen\":" + age + ",\"ip\":\"" + ip +
         "\",\"ssid\":\"" + jsonEscape(ssid) + "\",\"ap\":" + (accessPointMode ? "true" : "false") + "}";
}

String wifiScanJson() {
  int count = WiFi.scanNetworks();
  String result = "[";
  for (int i = 0; i < count; ++i) {
    if (i) result += ',';
    result += '"';
    result += jsonEscape(WiFi.SSID(i));
    result += '"';
  }
  WiFi.scanDelete();
  return result + "]";
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
    if (config.staticIpEnabled) {
      IPAddress ip, gateway, subnet;
      if (ip.fromString(config.staticIp) && gateway.fromString(config.gateway) && subnet.fromString(config.subnet)) {
        WiFi.config(ip, gateway, subnet);
      }
    }
    WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());
    uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) delay(250);
  }
  if (WiFi.status() != WL_CONNECTED) {
    accessPointMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ArtNet-Relay-Setup");
    // Route every DNS lookup to this local web server. This lets operating
    // systems recognise the setup AP as a captive portal.
    captiveDns.start(53, "*", WiFi.softAPIP());
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
    server.send(200, "application/json", configJson());
  });
  server.on("/api/config", HTTP_POST, [] {
    String body = server.arg("plain");
    bool ok = applyConfigJson(body);
    server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"IP inválida\"}");
    if (!ok) return;
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
  // Captive-portal checks use different URLs on each operating system. In
  // setup mode every unknown route returns to the local configurator.
  server.onNotFound([] {
    if (accessPointMode) {
      server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
      server.send(302, "text/plain", "Opening Art-Net Relay setup...");
    } else {
      server.send(404, "text/plain", "Not found");
    }
  });
  server.begin();
}

// USB configuration protocol used by the Chrome installer.  Normal boot logs
// are intentionally left untouched; machine-readable replies always start
// with "ARCFG " so the browser can safely ignore everything else.
String serialCommand;

// Minimal Improv Serial service. ESP Web Tools uses this open protocol after
// flashing to configure Wi-Fi and, when a URL is available, offer "Visit
// device" inside its own completion dialog.
constexpr uint8_t IMPROV_VERSION = 1;
constexpr uint8_t IMPROV_CURRENT_STATE = 0x01;
constexpr uint8_t IMPROV_ERROR_STATE = 0x02;
constexpr uint8_t IMPROV_RPC = 0x03;
constexpr uint8_t IMPROV_RPC_RESULT = 0x04;
constexpr uint8_t IMPROV_READY = 0x02;
constexpr uint8_t IMPROV_PROVISIONING = 0x03;
constexpr uint8_t IMPROV_PROVISIONED = 0x04;
uint8_t improvPacket[266];
uint16_t improvPacketLength = 0;
uint16_t improvExpectedLength = 0;

String webUrl() {
  IPAddress ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP() : WiFi.softAPIP();
  return String("http://") + ip.toString();
}

bool improvReachable() {
  return WiFi.status() == WL_CONNECTED || accessPointMode;
}

bool openWifiNetwork(int index) {
#if defined(ESP8266)
  return WiFi.encryptionType(index) == ENC_TYPE_NONE;
#else
  return WiFi.encryptionType(index) == WIFI_AUTH_OPEN;
#endif
}

void sendImprovPacket(uint8_t type, const uint8_t* data, uint8_t dataLength) {
  uint8_t packet[266] = {'I', 'M', 'P', 'R', 'O', 'V', IMPROV_VERSION, type, dataLength};
  if (dataLength) memcpy(packet + 9, data, dataLength);
  uint8_t checksum = 0;
  for (uint16_t i = 0; i < 9 + dataLength; ++i) checksum += packet[i];
  packet[9 + dataLength] = checksum;
  Serial.write(packet, 10 + dataLength);
  Serial.write('\n');
}

void sendImprovState() {
  uint8_t state = improvReachable() ? IMPROV_PROVISIONED : IMPROV_READY;
  sendImprovPacket(IMPROV_CURRENT_STATE, &state, 1);
}

void sendImprovError(uint8_t error) {
  sendImprovPacket(IMPROV_ERROR_STATE, &error, 1);
}

void sendImprovResult(uint8_t command, const String* values, uint8_t valueCount) {
  uint8_t data[255] = {command, 0};
  uint8_t length = 0;
  for (uint8_t i = 0; i < valueCount; ++i) {
    uint8_t valueLength = min((size_t)values[i].length(), (size_t)(252 - length));
    if (length + valueLength + 1 > 252) break;
    data[2 + length++] = valueLength;
    for (uint8_t c = 0; c < valueLength; ++c) data[2 + length++] = values[i][c];
  }
  data[1] = length;
  sendImprovPacket(IMPROV_RPC_RESULT, data, length + 2);
}

String improvChipFamily() {
#if defined(ESP8266)
  return "ESP8266";
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
  return "ESP32-C6";
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  return "ESP32-C3";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  return "ESP32-S3";
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
  return "ESP32-S2";
#else
  return "ESP32";
#endif
}

bool connectImprovWifi(const String& ssid, const String& password) {
  config.wifiSsid = ssid.substring(0, 32);
  config.wifiPassword = password.substring(0, 63);
  saveConfig();
  WiFi.mode(WIFI_STA);
#if defined(ESP8266)
  WiFi.hostname(safeHostname());
#else
  WiFi.setHostname(safeHostname().c_str());
#endif
  WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());
  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) delay(250);
  if (WiFi.status() != WL_CONNECTED) {
    accessPointMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ArtNet-Relay-Setup");
    captiveDns.start(53, "*", WiFi.softAPIP());
    return false;
  }
  accessPointMode = false;
  captiveDns.stop();
  MDNS.begin(safeHostname().c_str());
  MDNS.addService("http", "tcp", 80);
  return true;
}

void processImprovPacket() {
  if (improvPacketLength < 10 || improvPacket[6] != IMPROV_VERSION || improvPacket[7] != IMPROV_RPC) return;
  uint8_t dataLength = improvPacket[8];
  if (improvPacketLength != 10 + dataLength) return;
  uint8_t checksum = 0;
  for (uint16_t i = 0; i < improvPacketLength - 1; ++i) checksum += improvPacket[i];
  if (checksum != improvPacket[improvPacketLength - 1] || dataLength < 2) {
    sendImprovError(0x01);
    return;
  }
  const uint8_t* data = improvPacket + 9;
  uint8_t command = data[0];
  if (data[1] != dataLength - 2) {
    sendImprovError(0x01);
    return;
  }
  sendImprovError(0x00);
  if (command == 0x01) { // Wi-Fi credentials
    if (dataLength < 4 || data[2] + 4 > dataLength) { sendImprovError(0x01); return; }
    uint8_t ssidLength = data[2];
    uint8_t passwordOffset = 3 + ssidLength;
    uint8_t passwordLength = data[passwordOffset];
    if (passwordOffset + 1 + passwordLength != dataLength) { sendImprovError(0x01); return; }
    String ssid, password;
    for (uint8_t i = 0; i < ssidLength; ++i) ssid += static_cast<char>(data[3 + i]);
    for (uint8_t i = 0; i < passwordLength; ++i) password += static_cast<char>(data[passwordOffset + 1 + i]);
    uint8_t state = IMPROV_PROVISIONING;
    sendImprovPacket(IMPROV_CURRENT_STATE, &state, 1);
    if (!connectImprovWifi(ssid, password)) { sendImprovError(0x03); sendImprovState(); return; }
    sendImprovState();
    String values[] = {webUrl()};
    sendImprovResult(command, values, 1);
    return;
  }
  if (command == 0x02) { // Current state
    sendImprovState();
    if (improvReachable()) { String values[] = {webUrl()}; sendImprovResult(command, values, 1); }
    return;
  }
  if (command == 0x03) { // Device information
    String values[] = {"Art-Net Relay", "1.1.0", improvChipFamily(), config.name};
    sendImprovResult(command, values, 4);
    return;
  }
  if (command == 0x04) { // Wi-Fi network scan
    int count = WiFi.scanNetworks();
    for (int i = 0; i < count; ++i) {
      String values[] = {WiFi.SSID(i), String(WiFi.RSSI(i)), openWifiNetwork(i) ? "NO" : "YES"};
      sendImprovResult(command, values, 3);
    }
    WiFi.scanDelete();
    sendImprovResult(command, nullptr, 0);
    return;
  }
  if (command == 0x07) { // Network state and URL for "Visit device"
    String values[] = {improvReachable() ? "3" : "2", webUrl()};
    sendImprovResult(command, values, improvReachable() ? 2 : 1);
    return;
  }
  sendImprovError(0x02);
}

void processSerialCommand(const String& command) {
  if (command == "ARCFG GET") {
    Serial.println(String("ARCFG ") + configJson());
    return;
  }
  if (command.startsWith("ARCFG SET ")) {
    bool ok = applyConfigJson(command.substring(10));
    Serial.println(ok ? "ARCFG OK" : "ARCFG ERROR IP inválida");
    // Native USB CDC can disappear as soon as ESP.restart() is called. Flush
    // the acknowledgement first so Chrome has a deterministic completion.
    Serial.flush();
    if (ok) {
      delay(1000);
      ESP.restart();
    }
    return;
  }
  if (command == "ARCFG SCAN") {
    Serial.println(String("ARCFG ") + wifiScanJson());
    return;
  }
  if (command == "ARCFG PING") Serial.println("ARCFG PONG");
}

void handleTextSerialByte(uint8_t c) {
  if (c == '\r') return;
  if (c == '\n') {
    if (serialCommand.length()) processSerialCommand(serialCommand);
    serialCommand = "";
  } else if (serialCommand.length() < 768) {
    serialCommand += static_cast<char>(c);
  } else {
    serialCommand = "";
  }
}

void handleSerialConfig() {
  static const char header[] = "IMPROV";
  while (Serial.available()) {
    uint8_t c = static_cast<uint8_t>(Serial.read());
    if (improvPacketLength == 0) {
      if (c == 'I') { improvPacket[0] = c; improvPacketLength = 1; }
      else handleTextSerialByte(c);
      continue;
    }
    improvPacket[improvPacketLength++] = c;
    if (improvPacketLength <= 6 && c != static_cast<uint8_t>(header[improvPacketLength - 1])) {
      for (uint16_t i = 0; i < improvPacketLength; ++i) handleTextSerialByte(improvPacket[i]);
      improvPacketLength = 0;
      improvExpectedLength = 0;
      continue;
    }
    if (improvPacketLength == 9) {
      improvExpectedLength = 10 + improvPacket[8];
      if (improvExpectedLength > sizeof(improvPacket)) { improvPacketLength = 0; improvExpectedLength = 0; }
    }
    if (improvExpectedLength && improvPacketLength == improvExpectedLength) {
      processImprovPacket();
      improvPacketLength = 0;
      improvExpectedLength = 0;
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Art-Net Relay: starting");
  loadConfig();
  pinMode(config.relayPin, OUTPUT);
  setupStatusLed();
  setRelay(false);
  startNetwork();
  Serial.printf("Art-Net Relay: network ready (%s)\n", accessPointMode ? "AP" : WiFi.localIP().toString().c_str());
  artnet.begin(ARTNET_PORT);
  setupWebServer();
  Serial.println("Art-Net Relay: ready");
}

void loop() {
  if (accessPointMode) captiveDns.processNextRequest();
  server.handleClient();
  receiveArtnet();
  handleSerialConfig();
}
