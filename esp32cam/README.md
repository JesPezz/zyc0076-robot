# Firmware ESP32-CAM — puente mando Bluetooth + OTA WiFi

El ESP32-CAM actúa como **host Bluetooth** (Bluepad32) del mando Funlab FF01 y
reenvía las órdenes al Arduino UNO por UART0 con el protocolo del kit
(`0xA5, <orden>, 0x5A`). Además se conecta a WiFi y expone **OTA por HTTP**.

```
FF01 --Bluetooth--> ESP32-CAM --UART--> UNO R3 --> motores
```

## Archivos propios de este proyecto

| Archivo | Qué es |
|---|---|
| `main/sketch.cpp` | Mapeo mando→órdenes, WiFi STA, OTA HTTP (`POST /update`) |
| `main/main.c` | `app_main` que arranca Bluepad32 + Arduino |
| `platformio.ini` | PlatformIO (`framework = espidf`, board `esp32cam`) |
| `sdkconfig.defaults` | BT dual-mode, tabla two-OTA, `-Os`, ajustes de IRAM |
| `partitions.csv` | Tabla de particiones two-OTA (2 slots de app) |

## Compilar

Este repo **no incluye** las fuentes de terceros (arduino-esp32, btstack,
bluepad32) por tamaño. Se parte de la plantilla
[`ricardoquesada/esp-idf-arduino-bluepad32-template`](https://github.com/ricardoquesada/esp-idf-arduino-bluepad32-template):

1. Clonar la plantilla y copiar encima `main/`, `platformio.ini`,
   `sdkconfig.defaults`, `partitions.csv`, `CMakeLists.txt`.
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

## Actualizar por WiFi (sin cable ni botones)

```
curl -F firmware=@fw/firmware.bin http://<ip-del-esp32>/update
```

El dispositivo responde `200`, se reinicia y arranca la versión nueva.
La IP la asigna el DHCP; el hostname es `zyc0076` (contraseña ArduinoOTA
`zyc0076`).
