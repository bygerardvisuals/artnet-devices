# Art-Net Relay Desktop

Aplicación Electron para instalar y configurar el ESP32 en macOS, Windows y Linux.

## Ejecutar en desarrollo

Desde `desktop-app`:

```sh
npm install
npm start
```

## Crear instaladores

Los instaladores deben generarse en el sistema de destino (o con un entorno CI de ese sistema):

```sh
npm run dist:mac    # .dmg y .zip
npm run dist:win    # instalador .exe NSIS
npm run dist:linux  # AppImage y .deb
```

Antes de empaquetar se incorpora automáticamente `web-installer/firmware/merged-firmware.bin` de ESP32‑C6 a la aplicación. La pestaña de configuración conecta por Wi‑Fi con el panel del ESP32, que incluye su actualización OTA.

El flujo GitHub Actions **Build desktop installers** crea los paquetes de macOS, Windows y Linux como artefactos descargables en cada publicación del proyecto.
