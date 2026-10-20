# Firmware ESP32-CAM — mando Bluetooth, cámara, control web y OTA por release

El ESP32-CAM actúa como **host Bluetooth** (Bluepad32) del mando Funlab FF01,
reenvía las órdenes al Arduino UNO por UART0 con el protocolo del kit
(`0xA5, <orden>, 0x5A`) y sirve la **página de control** (`:80`) y el **stream
MJPEG** (`:81`). Al arrancar se conecta a WiFi y busca un **release nuevo en
GitHub** para actualizarse solo.

```
FF01 --Bluetooth--> ESP32-CAM --UART--> UNO R3 --> motores
```

## Archivos propios de este proyecto

| Archivo | Qué es |
|---|---|
| `main/sketch.cpp` | Mando→órdenes, cámara MJPEG, control web, OTA por release |
| `main/web.h` | Página HTML de control (`PAGE_HTML`) |
| `main/main.c` | `app_main` que arranca Bluepad32 + Arduino |
| `platformio.ini` | PlatformIO (`framework = espidf`, board `esp32cam`) |
| `sdkconfig.defaults` | BT dual-mode, tabla two-OTA, PSRAM, `-Os`, ajustes de IRAM |

## Compilar

Este repo **no incluye** las fuentes de terceros (arduino-esp32, btstack,
bluepad32) por tamaño. Se parte de la plantilla
[`ricardoquesada/esp-idf-arduino-bluepad32-template`](https://github.com/ricardoquesada/esp-idf-arduino-bluepad32-template):

1. Clonar la plantilla y copiar encima `main/`, `platformio.ini`,
   `sdkconfig.defaults`, `CMakeLists.txt`.
2. `components/arduino/` = arduino-esp32 en el sha `94afccf35fb1e401facddbcf9e13bcf7c76a31d8`.
3. `external/btstack/` = btstack en el sha `8a8b616e04b07612a273978131efad32a083b1ed`.
4. Compilar:
   ```
   pio run -e esp32cam
   ```

Requisitos: **PlatformIO Core 6.1.19** (la 6.2 rompe el build con
`SCons.Tool.FortranCommon`) y Python 3.10–3.13. En un PC de 16 núcleos tarda
~10 min; en uno lento, ~50 min. Se compila con `-Os` porque con `-Og` se
desborda la IRAM (`iram0_0_seg overflowed`).

## Flashear por cable (solo la primera vez)

La base del ESP32-CAM **no tiene auto-reset**, así que hay que entrar a mano en
modo descarga: mantener **BOOT/IO0**, pulsar **RESET**, soltar BOOT.

```
C:\Python314\python.exe -m esptool --chip esp32 -p COM5 -b 460800 ^
  --before no-reset --connect-attempts 3000 ^
  write-flash -z 0x10000 fw\firmware.bin
```

## Control por WiFi

Con el ESP32 en la red: la **página de control** está en `http://<ip>/` (mover,
girar, STOP, servo) y el **stream MJPEG** en `http://<ip>:81/stream`. La orden
suelta va a `/c?o=<orden>` y el UNO la mantiene hasta que llega otra.

## Actualizar por WiFi (OTA desde GitHub)

El firmware lleva su versión en `FW_VERSION` (`main/sketch.cpp`). **Al arrancar**
consulta el último release de `JesPezz/zyc0076-robot`; si su tag es mayor,
descarga el asset `firmware.bin`, se flashea y reinicia. Mueve el servo
(barrido izquierda-derecha-centro) y da tres destellos con el LED de iluminación
para avisar del **inicio** y del **fin** de la actualización. Si ya está al día,
no hace nada.

Para publicar una versión nueva:

1. Subir `FW_VERSION` en `main/sketch.cpp` (p. ej. `"1.0"` → `"1.1"`).
2. Compilar (`pio run -e esp32cam`) y copiar `.pio/build/esp32cam/firmware.bin`
   a `fw/firmware.bin`.
3. Publicar el release con el binario adjunto, tag `vX.Y`:
   ```
   gh release create v1.1 esp32cam/fw/firmware.bin --title "v1.1" --notes "..." --prerelease
   ```
   Los dispositivos toman el release de `releases/latest`; promocionar con
   `gh release edit v1.1 --prerelease=false` cuando toque.

> El tag y `FW_VERSION` deben coincidir; si el binario publicado tiene una
> `FW_VERSION` vieja, el ESP32 volverá a ofrecerse la actualización en bucle.

La IP la asigna el DHCP; el hostname es `zyc0076` (contraseña ArduinoOTA
`zyc0076`).
