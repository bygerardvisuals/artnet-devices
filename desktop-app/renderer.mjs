import { ESPLoader, Transport } from './node_modules/esptool-js/bundle.js';

const $ = (id) => document.getElementById(id);
let base = '';
const terminal = { clean: () => { $('terminal').textContent = ''; }, write: (text) => { $('terminal').textContent += text; }, writeLine: (text) => { $('terminal').textContent += `${text}\n`; } };

document.querySelectorAll('[data-tab]').forEach((button) => button.onclick = () => {
  document.querySelectorAll('[data-tab],.tab').forEach((item) => item.classList.remove('active'));
  button.classList.add('active'); $(button.dataset.tab).classList.add('active');
});

async function refreshPorts() {
  const ports = await window.relayDesktop.listPorts();
  const select = $('ports');
  select.replaceChildren();
  if (!ports.length) {
    const option = document.createElement('option'); option.value = ''; option.textContent = 'No se han detectado puertos'; select.append(option);
  } else {
    ports.forEach((port) => {
      const option = document.createElement('option'); option.value = port.portId;
      option.textContent = `${port.displayName || port.portName} ${port.vendorId ? `(${port.vendorId}:${port.productId || '?'})` : ''}`;
      select.append(option);
    });
  }
  $('flashStatus').textContent = ports.length ? 'Selecciona el ESP32 y pulsa «Instalar firmware».' : 'No se han detectado puertos. Conecta el ESP32 y revisa el cable USB.';
}
$('refresh').onclick = refreshPorts;

$('flash').onclick = async () => {
  const portId = $('ports').value;
  if (!portId) return refreshPorts();
  $('flash').disabled = true; $('progress').value = 0; terminal.clean();
  try {
    await window.relayDesktop.selectPort(portId);
    $('flashStatus').textContent = 'Conectando con el ESP32…';
    const port = await navigator.serial.requestPort();
    const transport = new Transport(port);
    const loader = new ESPLoader({ transport, baudrate: 115200, terminal });
    const chip = await loader.main();
    if (!String(chip).includes('ESP32-C6')) throw new Error(`Este firmware es para ESP32‑C6; se detectó ${chip}.`);
    const firmware = new Uint8Array(await window.relayDesktop.firmware());
    $('flashStatus').textContent = 'Grabando firmware…';
    await loader.writeFlash({ fileArray: [{ data: firmware, address: 0 }], flashSize: '4MB', flashMode: 'dio', flashFreq: '40m', eraseAll: true, compress: true, reportProgress: (_index, written, total) => { $('progress').value = Math.round(written / total * 100); } });
    await loader.after('hard_reset');
    await transport.disconnect();
    $('flashStatus').textContent = 'Instalación terminada. Abre la pestaña «Configurar dispositivo».';
  } catch (error) { $('flashStatus').textContent = `Error: ${error.message || error}`; }
  finally { $('flash').disabled = false; }
};

function host(value) { return value.trim().replace(/\/$/, ''); }
$('connect').onclick = async () => {
  base = host($('host').value); $('connection').textContent = 'Conectando…';
  try {
    const response = await fetch(`${base}/api/status`); if (!response.ok) throw new Error();
    const config = await response.json();
    $('deviceTitle').textContent = `Configuración · ${config.name}`; $('deviceName').value = config.name; $('wifiSsid').value = config.ssid === 'ArtNet-Relay-Setup' ? '' : config.ssid; $('artnetUniverse').value = config.universe; $('artnetChannel').value = config.channel; $('relayPin').value = config.relayPin; $('relayInverted').value = String(config.relayInverted); $('settings').classList.remove('hidden'); $('connection').textContent = `Conectado a ${config.ip}`;
  } catch { $('settings').classList.add('hidden'); $('connection').textContent = 'No se ha encontrado el ESP32. Comprueba el Wi‑Fi y la dirección.'; }
};
$('settings').onsubmit = async (event) => {
  event.preventDefault(); $('saveStatus').textContent = 'Guardando…';
  try { const response = await fetch(`${base}/api/config`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(Object.fromEntries(new FormData(event.target))) }); $('saveStatus').textContent = response.ok ? 'Guardado. El ESP32 se está reiniciando.' : 'No se pudo guardar.'; } catch { $('saveStatus').textContent = 'Se perdió conexión; el ESP32 puede estar reiniciándose.'; }
};
refreshPorts();
