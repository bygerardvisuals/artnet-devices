# Art-Net Relay para ESP32

Firmware para un ESP32‑C6 SuperMini que convierte un canal de Art-Net en una salida de relé:

- DMX `0–127`: relé apagado.
- DMX `128–255`: relé encendido.
- El universo, canal, GPIO, polaridad, Wi-Fi y nombre Art-Net se guardan en la memoria no volátil del ESP32.
- Responde a `ArtPoll`, de modo que MadMapper y otras aplicaciones compatibles pueden mostrar el nombre asignado al dispositivo.

## Conexión del relé

El valor inicial del GPIO de salida es **GPIO 2**, pero puede cambiarse desde la interfaz. Conecta `IN` del módulo de relé al GPIO elegido, `VCC` a la alimentación que admita el módulo y une siempre los `GND`.

Muchos módulos de relé son de lógica invertida: si el relé se activa al revés, selecciona **Activo en LOW** en la página de configuración. No conectes carga de red eléctrica sin una caja, protecciones y experiencia adecuada; el ESP32 sólo debe manejar la entrada de control del módulo de relé.

## Instalar desde Chrome (recomendado)

La carpeta `web-installer` es una página web de instalación, con apariencia coherente con la interfaz del dispositivo. Usa Web Serial y funciona en **Google Chrome o Microsoft Edge**, por USB.

1. Compila una vez el proyecto: `pio run`. El proceso genera automáticamente `web-installer/firmware/merged-firmware.bin`, con el bootloader, particiones y firmware juntos.
2. Sirve la página localmente:

   ```sh
   python3 -m http.server 8000 --directory web-installer
   ```

3. Abre [http://localhost:8000](http://localhost:8000) en Chrome, conecta el ESP32‑C6 por USB y pulsa **Instalar en el ESP32**.

Tras la instalación abre `http://localhost:8000/configure.html` en Chrome. Conecta el ordenador a la red Wi-Fi `ArtNet-Relay-Setup` y abre el panel del propio ESP32 en `http://192.168.4.1`. Desde ahí se guarda el Wi-Fi, nombre, universo, canal, GPIO y polaridad, y también se hacen actualizaciones OTA. Es la misma experiencia de configuración web que WLED, sin instalar una app adicional.

Para publicar el instalador en Internet, sube toda la carpeta `web-installer` a un alojamiento estático HTTPS. No funcionará si se abre `index.html` directamente con `file://`, porque Chrome exige un contexto seguro para acceder al puerto USB.

## Publicar en GitHub Pages

El flujo `.github/workflows/deploy-pages.yml` publica automáticamente `web-installer` al hacer *push* a la rama `main`. Tras crear el repositorio y activar **Settings → Pages → GitHub Actions**, el instalador se abrirá en una URL HTTPS de GitHub Pages. Desde esa web se instala el firmware por USB; después se abre el panel del ESP32 en `http://192.168.4.1` para configurar y hacer las actualizaciones OTA.

## Compilar y grabar por USB desde PlatformIO

1. Instala [PlatformIO](https://platformio.org/install) (la extensión de VS Code también sirve). La primera compilación descargará pioarduino, que aporta soporte Arduino 3.x para ESP32‑C6.
2. Conecta el ESP32‑C6 SuperMini por USB.
3. Desde esta carpeta, ejecuta:

   ```sh
   pio run -t upload
   ```

4. Si el puerto no se detecta automáticamente, añade `upload_port = /dev/cu.usbmodem-...` a la sección `[env:esp32c6-supermini]` de `platformio.ini`.

El entorno viene configurado como `esp32-c6-supermini`, para un ESP32‑C6 SuperMini de 4 MB. El binario de un ESP32‑C6 no es compatible con ESP32 clásico, C3, S2 ni S3.

## Primera configuración

Al no tener una red guardada crea el punto de acceso **ArtNet-Relay-Setup**. Conéctate a él y abre [http://192.168.4.1](http://192.168.4.1). Desde la interfaz tipo WLED puedes:

1. Introducir la red Wi‑Fi y contraseña.
2. Asignar el nombre que verá el software Art-Net.
3. Elegir universo Art-Net (empieza en 0) y canal DMX (1–512).
4. Elegir GPIO y polaridad del relé.
5. Subir nuevas compilaciones `.bin` sin cable USB desde la pestaña **Firmware**.

El nodo también puede abrirse por `http://<nombre>.local` cuando la red admite mDNS.

## Compilar el archivo OTA

Para generar el `.bin` que se puede subir desde la interfaz OTA:

```sh
pio run
```

Sube **sólo** `.pio/build/esp32c6-supermini/firmware.bin` desde la pestaña **Firmware** del ESP32. También se genera el archivo completo para el instalador USB de Chrome en `web-installer/firmware/merged-firmware.bin`; éste es exclusivamente para una instalación inicial por USB, no para OTA.
