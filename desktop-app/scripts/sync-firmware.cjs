const fs = require('node:fs');
const path = require('node:path');

const source = path.resolve(__dirname, '../../web-installer/firmware/merged-firmware.bin');
const destination = path.resolve(__dirname, '../firmware/merged-firmware.bin');
if (!fs.existsSync(source)) throw new Error(`No se encuentra el firmware: ${source}. Ejecuta pio run primero.`);
fs.mkdirSync(path.dirname(destination), { recursive: true });
fs.copyFileSync(source, destination);
console.log(`Firmware listo para la app: ${destination}`);
