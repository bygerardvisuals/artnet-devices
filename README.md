# ArtNet Devices

ArtNet Devices transforma placas ESP compatibles en nodos Art-Net configurables por web: un universo Art-Net global y múltiples GPIO asignables a canales DMX.

## Salidas

Cada salida se define independientemente con:

- GPIO seguro, seleccionado según el perfil de placa instalado.
- Canal DMX 1–512 del universo global.
- Modo **Digital**: OFF por debajo del umbral y ON por encima.
- Modo **PWM**: valor DMX 0–255 para regular intensidad y fundidos.
- Inversión de polaridad, necesaria por ejemplo para relés activos en bajo.

La interfaz indica el LED integrado y lo reserva para visualizar actividad, por lo que no deja configurarlo accidentalmente como una salida. También excluye pines de flash, USB, UART, arranque o de entrada exclusiva. No todos los GPIO físicos de un chip están expuestos por todas las placas: el número mostrado es el de GPIO seguros del perfil de firmware instalado.

## Pixel LED

Las placas con temporización compatible ofrecen una salida Pixel LED direccionable: una tira RGB usa un GPIO reservado, de uno a tres universos Art-Net consecutivos y no puede compartir el pin con una salida digital o PWM. El ESP32-C2 se anuncia explícitamente como **GPIO/PWM solamente**: el núcleo Arduino actual no aporta un backend estable para Pixel LED en esa familia, por lo que el instalador no publica una imagen rota ni deja activar esa opción.

## Placas incluidas

- ESP32-C6 SuperMini
- ESP32-C3 SuperMini
- ESP32-C2 DevKitM-1
- ESP32 clásico / DevKit-WROOM
- ESP32-S2 Saola-1
- ESP32-S3 DevKitC-1
- ESP8266: LOLIN Wemos D1 mini y NodeMCU

Las familias sin Wi-Fi no se incluyen porque no pueden recibir Art-Net por red.

## Instalación y configuración

Abre el instalador publicado en Chrome o Edge, conecta la placa por USB y pulsa instalar. El instalador comprueba la familia de chip y, al terminar el arranque del firmware, ofrece **Visitar dispositivo**.

Si no hay una red guardada, la placa crea el punto de acceso `ArtNet-Devices-Setup`; el portal cautivo y [http://192.168.4.1](http://192.168.4.1) abren la configuración. El mismo configurador puede usarse por USB desde `web-installer/usb-configure.html`, sin desconectar el ordenador de su Wi-Fi.

Desde el panel se configuran red, IP estática opcional, nombre Art-Net, universo, salidas y, cuando la placa lo permite, Pixel LED. El nombre aparece en las respuestas ArtPoll, para que MadMapper y otros controladores lo identifiquen. La pestaña Firmware acepta actualizaciones OTA usando la imagen OTA de la misma familia de chip.

## Desarrollo

```sh
pio run -e esp32c6-supermini
```

La compilación genera dos imágenes en `web-installer/firmware/`:

- `artnet-devices-<placa>.bin`: instalación inicial por USB.
- `artnet-devices-<placa>-ota.bin`: actualización OTA desde el panel.

El flujo de GitHub Actions compila todas las familias y publica el instalador en GitHub Pages. La aplicación de escritorio en `desktop-app/` ofrece las mismas operaciones en macOS, Windows y Linux.
