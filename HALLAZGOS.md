# Hallazgos y pendientes — TSCINBUNY ZYC0076

Resumen técnico de todo lo verificado en el robot, más lo que queda por hacer
(Fase 3: OTA del UNO por WiFi).

## Hardware detectado

| Pieza | Detalle verificado |
|---|---|
| Controlador | Arduino UNO R3 (ATmega328P), USB-serial FTDI `0403:6001` |
| Escudo | puente-H de 4 motores (74HCT595), puerto ultrasónico, servo, interruptor |
| Cámara | ESP32-CAM AI-Thinker con sensor **GC2145** (`PID=0x2145`), **no** OV2640 |
| Distancia | HC-SR04 (sólo lo imprime `model2`/`model3`) |
| Movimiento | 4× TT + ruedas mecanum 60 mm; servo SG90 para el sensor |
| Alimentación | 2× 18650 + regulador; el USB sólo alimenta la lógica |
| Puertos | `COM4` = FTDI del UNO · `COM5` = CH340 de la base del CAM |
| Identidad | MAC WiFi `f4:2d:c9:a6:4d:6c` · MAC BT `f4:2d:c9:a6:4d:6e` · IP por DHCP |

**Ojo:** la base del ESP32-CAM **no tiene auto-reset**, así que flashear por
cable exige secuencia manual (mantener BOOT/IO0 → pulsar RESET → soltar BOOT)
y hay que usar `--before no-reset` para no reventarla.

## Protocolo serie (UNO)

- Paquete de 3 bytes `0xA5 <orden> 0x5A` a **115200** baudios.
- Al abrir el puerto el DTR resetea el UNO: **el primer paquete se pierde**
  (esperar ~3 s antes de escribir).
- `model1` re-aplica la última orden en cada bucle → hay que mandar `STOP`.
- `MotorLeft/Right/Center` (230/231/232) mueven el **servo**, no ruedas: no
  existe orden individual por rueda.
- El ESP32 y el UNO comparten la línea serie: mientras el ESP32 esté conectado,
  el UNO **no se puede flashear** (hay que desconectarlo del escudo).

## Causas raíz de los fallos

| Síntoma | Causa real |
|---|---|
| "Zumba pero no se mueve" | pilas 18650 agotadas (no software ni puente-H) |
| El UNO parpadea con el LED fábrica | no tenía el sketch del kit |
| Primer orden sin efecto | reset por DTR al abrir el puerto |
| `Python version must be between 3.10 and 3.13` | pioarduino rechaza Python 3.14 |
| `SCons.Tool.FortranCommon` | PlatformIO 6.2 borra `tool-scons` en medio del build → **fijar `platformio==6.1.19`** |
| `iram0_0_seg overflowed` | build de Debug (`-Og`) → `-Os` + `WIFI_IRAM_OPT=n` + `FREERTOS_PLACE_FUNCTIONS_INTO_FLASH=y` |
| `dram0_0_seg overflowed` | `BLUEPAD32_MAX_DEVICES=4` y `BR_EDR_MAX_ACL_CONN=5` → bajar a 1 y 1 |
| `undefined reference to sdp_init` | `BTDM_CTRL_MODE_BTDM=n` excluye `btstack/src/classic` |
| `#error WIFI_RX_BA_WIN ...` | `WIFI_RX_BA_WIN` debe ser ≤ 2 × `WIFI_STATIC_RX_BUFFER_NUM` |
| App en bucle, sin WiFi | cámara sin soporte JPEG **+** `camServer` antes de `tcpip_init` (assert en lwip) |
| OTA `No response from device` | ArduinoOTA exige back-connect TCP 3232 (bloqueado por firewall) → **HTTP `POST /update` sí funciona** |

## Cámara GC2145

El driver `esp32-camera` **no admite JPEG** en la GC2145. La solución usada:

- `PIXFORMAT_RGB565`, `FRAMESIZE_QVGA`, `fb_count=2`, framebuffers en PSRAM.
- Convertir cada frame con `frame2jpg(fb, 12, ...)` antes de enviarlo.
- PSRAM activa (`CONFIG_SPIRAM=y`, 40 MHz, `USE_MALLOC`).

## Estado actual

1. **Sin PC:** FF01 → Bluetooth (Bluepad32, BR/EDR) → UART → UNO → motores. ✔
2. **WiFi:** se conecta a `INFINITUM181E` / `_EXT` / `TP-Link_BF0C`. ✔
3. **OTA del ESP32:** `curl.exe -F firmware=@fw\firmware.bin http://<ip>/update`. ✔
4. **Cámara:** MJPEG ~3 fps QVGA en `http://<ip>:81/stream`. ✔
5. **UI de control:** `http://<ip>/` (joystick carro, joystick cámara, STOP) con
   whitelist de órdenes. ✔
6. **APK Android** (WebView + IP editable): compilada, firmada y publicada. ✔
7. Tamaño: RAM 34%, Flash 35%, binario ~1,48 MB (límite de slot OTA: 1.572.864 B).

Herramientas: `robot.py` (USB), `gamepad.py` (FF01 por PC),
`esp32cam/serial_monitor.py` (monitor de órdenes), `esp32cam/caplog.py` (crudo).

## Pendiente — Fase 3: OTA del UNO por WiFi

**Objetivo:** actualizar el sketch del Arduino UNO sin cable ni PC.

**Cómo funcionaría:** el ESP32-CAM actúa como programador AVR (STK500v1 como
hace `avrdude -c arduino`), subiendo el `.hex` por HTTP y quemándolo en el UNO.

**Lo que falta (en este orden):**

1. **Confirmar la línea RESET.** Antes de programar, el UNO debe pulsarse en
   RESET. Hay que verificar si un GPIO del ESP32 está cableado al `RESET` del
   UNO en el escudo. Si sólo están `TX/RX/VCC/GND`, hará falta un cable
   (GPIO12→RESET) o descartar la Fase 3 y programar por USB.
2. **Dejar de enviar órdenes durante la programación** — el UART0 del ESP32 es
   el mismo que manda `0xA5/orden/0x5A`: suspender el keepalive y los eventos
   del mando mientras se programa.
3. **Servir el `.hex` por HTTP** (p.ej. `POST /updateuno`) y ejecutar el
   diálogo STK500v1 (`0x30 0x20` sync, `0x55`/`0x56` dirección, `0x64`/`0x74`
   escritura word/byte, `0x51` verificación) sobre ese UART.
4. **Flujo de releases:** `arduino-cli compile` → `build/*.hex` → subir junto a
   `firmware.bin`.

**Riesgo:** un corte a mitad deja el UNO sin firmware → recuperación por USB con
`arduino-cli upload` (nunca por OTA).

**Decisión pendiente:** medir/confirmar la línea RESET antes de implementar.
