const fs = require('node:fs');
const path = require('node:path');

const sourceDir = path.resolve(__dirname, '../../web-installer/firmware');
const destinationDir = path.resolve(__dirname, '../firmware');
const targets = ['esp32c6-supermini', 'esp32c3-supermini', 'esp32dev', 'wemos-d1-mini'];
fs.mkdirSync(destinationDir, { recursive: true });
for (const target of targets) {
  const source = path.join(sourceDir, `artnet-relay-${target}.bin`);
  const destination = path.join(destinationDir, `artnet-relay-${target}.bin`);
  if (!fs.existsSync(source)) throw new Error(`No se encuentra el firmware: ${source}. Ejecuta la compilación de ${target}.`);
  fs.copyFileSync(source, destination);
}
console.log(`Firmwares listos para la app: ${destinationDir}`);
