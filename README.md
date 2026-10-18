# Robot omnidireccional TSCINBUNY ZYC0076

Robot de 4 ruedas mecanum controlado por un **Arduino UNO R3** (ATmega328P) con
**ESP32-CAM** para WiFi/video. Este documento resume todo lo que hicimos para
dejarlo funcionando y como manejarlo.

## Hardware

| Pieza | Detalle |
|-------|---------|
| Placa de control | TSCINBUNY UNO R3 (compatible Arduino UNO, ATmega328P) |
| Enlace USB | Chip **FTDI FT232** -> aparece como **COM4** en Windows |
| Expansion board | Driver de 4 motores, regulador, switch ON/OFF, puerto del ESP32-CAM, receptor IR |
| ESP32-CAM | WiFi + camara OV2640; web server en **:80**, stream MJPEG en **:81** |
| Ultrasonido | **HC-SR04** (el "sensor acustico de proximidad": mide distancia por sonido) |
| Servo | SG90 sobre el ultrasonido (pan). `MotorLeft/Right/Center` mueven **el servo, no una rueda** |
| Motores | 4 motores TT con ruedas mecanum de 60 mm (movimiento omnidireccional) |
| Driver | 74HCT595 (shift register) que fija la direccion de los 4 motores |
| Siguelineas | 3 canales (A0/A1/A2) |
| Alimentacion | **2 pilas 18650** (no incluidas). Sin ellas los motores y el ESP32 no encienden |

Pinout relevante del sketch: servo=9, PWM1=5, PWM2=6, SHCP=2, EN=7, DATA=8,
STCP=4, Trig=12, Echo=13, linea=A0/A1/A2.

## Como lo dejamos funcionando

1. **Deteccion del puerto**: el UNO expone un FTDI que Windows enumera como `COM4`.
   WSL **no** ve ese puerto (solo `/dev/ttyS*` virtuales), asi que hay que
   trabajar desde Windows (PowerShell + Python de Windows).
2. **Diagnostico**: al inicio no respondia ni se movia. Se probó esptool (era un
   UNO, no un ESP), escucha serial pasiva y reset. Todo mudo.
3. **Causa raiz**: faltaban las **pilas 18650**. El USB solo alimenta al ATmega;
   sin pilas los motores y el ESP32-CAM quedan sin energia.
4. **Solucion**: se colocaron las pilas 18650 y se encendio el switch de la
   expansion board. El robot ya responde y se mueve.
5. Nota: **un solo cable USB basta** (al UNO). El ESP32-CAM se alimenta de la
   bateria, no del USB.

## Protocolo serial (115200 baud)

Cada orden es un paquete de **3 bytes**: `0xA5, <orden>, 0x5A`.

| Orden | Byte | Orden | Byte |
|-------|------|-------|------|
| Stop | 0 | Counterclockwise (gira izq) | 172 |
| Forward | 92 | Clockwise (gira der) | 83 |
| Backward | 163 | Model1 (manual) | 25 |
| Turn_Left (lado izq) | 149 | Model2 (evasion obstaculos) | 26 |
| Turn_Right (lado der) | 106 | Model3 (seguir) | 27 |
| Top_Left | 20 | Model4 (siguelineas) | 28 |
| Bottom_Left | 129 | MotorLeft (servo izq) | 230 |
| Top_Right | 72 | MotorRight (servo der) | 231 |
| Bottom_Right | 34 | MotorCenter (servo centro) | 232 |

El UNO mantiene el movimiento hasta recibir otra orden. El firmware imprime el
byte de orden recibido por serial (util para verificar desde el PC).

**Modos**: `model1` manual (control por las ordenes de arriba), `model2` evasion
automatica con el HC-SR04, `model3` seguir a un objeto, `model4` siguelineas.

## Utilidad `robot.py`

Dos vias de control: **USB serial** (por defecto) y **WiFi HTTP**.

```bash
# desde Windows (WSL no ve COM4)
python robot.py --list                 # ver puertos COM
python robot.py --selftest             # validar sin hardware
python robot.py forward                # avanza (hasta que mandes stop)
python robot.py forward 1.5            # avanza 1.5 s y frena solo
python robot.py ccw 0.8                # gira en sitio 0.8 s
python robot.py servo-left             # mueve el servo del ultrasonido
python robot.py model2                 # activa evasion de obstaculos

# modo teclado (teleop en vivo, sale con ESC)
python robot.py --keyboard

# por WiFi (si el ESP32-CAM esta en tu red)
python robot.py --ip 192.168.1.50 forward 1.0
```

**Modo teclado** (tecla -> accion):

| Tecla | Accion | Tecla | Accion |
|-------|--------|-------|--------|
| `w` | avanzar | `z` / `x` | servo izquierda / derecha |
| `s` | retroceder | `c` | servo al centro |
| `a` / `d` | lateral izq / der | `1` `2` `3` `4` | modelo manual / obstaculos / seguir / linea |
| `q` / `e` | girar izq / der | `espacio` | frenar |
| `ESC` | salir (frena solo) | | |

Ordenes disponibles: `stop forward back left right upleft downleft upright
downright ccw cw model1..4 servo-left servo-right servo-center` (+ por HTTP
`ledon ledoff`).

## Endpoints HTTP del ESP32-CAM (puerto 80)

`/go /back /stop /left /right /leftup /leftdown /rightup /rightdown
/counterclockwise /clockwise /model1 /model2 /model3 /model4 /motorleft
/motorright /motorcenter /ledon /ledoff /` y el stream en `:81/stream`.

El firmware del ESP32-CAM (carpeta `esp32cam/`) se une a la red WiFi en modo
`STA`. Red usada: `INFINITUM181E` (la clave no se incluye en este repo publico).

## Problemas conocidos / notas

- **WSL no expone COM4**: usar el Python de Windows
  (`C:\Python314\python.exe`), que ya trae `pyserial`.
- **No se mueve**: revisar pilas 18650 colocadas y switch en ON.
- **App del telefono no conecta**: se reporta que funciona instalando el core de
  ESP32 **v1.0.6** (la v3.2.0 da problemas). Firmware alternativo en
  `github.com/FroGPipA/ESP32-Car`.
- **Un solo cable USB** es suficiente (va al UNO).

## Mando Bluetooth (Funlab FF01) sin PC

El ESP32-CAM hace de **host Bluetooth** (Bluepad32), no el PC, asi que el robot
se maneja solo con el mando:

```
mando FF01 --Bluetooth--> ESP32-CAM --UART--> UNO R3 --> motores
```

Emparejar la primera vez:

1. Robot encendido (pilas + switch ON).
2. En Windows, **olvidar/quitar** el mando de Bluetooth (el mando recuerda un
   unico host; si esta emparejado al PC no se conecta al robot).
3. En el mando, mantener **B + HOME** ~3 s hasta que parpadeen los LED 1 y 4.
4. El ESP32-CAM lo empareja solo.

Mapeo: stick izquierdo = avanzar/retroceder y girar, stick derecho = lateral
(mecanum), A = frenar, B = model1 (manual), X = model2, Y = model3,
LT = model4, LB/RB = servo izquierda/derecha, RT = servo al centro.
Los botones de modelo desactivan los sticks hasta pulsar B (manual).

## Firmware del ESP32-CAM (carpeta `esp32cam/`)

Firmware del ESP32-CAM = puente Bluetooth + servidor OTA. Como compilarlo y
flashearlo por cable la primera vez esta en `esp32cam/README.md`.

**Actualizar por WiFi (OTA)** — sin cable y sin botones:

```bash
curl -F firmware=@fw/firmware.bin http://<ip-del-esp32>/update
```

El ESP32 responde `200`, se reinicia y arranca la version nueva. La IP la da el
DHCP; el `hostname` es `zyc0076`. El modo descarga manual (BOOT + RESET) solo se
necesita para el **primer** flasheo por USB, no para las actualizaciones.
