#!/usr/bin/env python3
"""Control del robot omnidireccional TSCINBUNY ZYC0076.

Placa: Arduino UNO R3 (ATmega328P) + ESP32-CAM.
Dos formas de control:

  USB (serial)  -> el UNO escucha en COM4 a 115200 baudios. Clase Robot.
  WiFi (HTTP)   -> el ESP32-CAM sirve un servidor web. Clase RobotHttp.

Ejemplos:
    python robot.py --list              # lista los puertos COM disponibles
    python robot.py --selftest          # valida la logica sin hardware
    python robot.py forward 1.5         # avanza 1.5 s y frena
    python robot.py ccw                 # gira en sitio, sin parar
    python robot.py servo-left
    python robot.py model2              # modo 2 = evasion de obstaculos
    python robot.py --ip 192.168.1.50 forward   # control por WiFi

En WSL el puerto COM4 no existe: hay que correr esto con el Python de
Windows (C:\\Python314\\python.exe robot.py ...).
"""

import argparse
import sys
import time

BAUD = 115200
PACKET_HEAD = 0xA5
PACKET_TAIL = 0x5A

# Ordenes del sketch Arduino-UNO.ino (un byte cada una).
ORDERS = {
    "stop": 0,
    "forward": 92,
    "back": 163,
    "left": 149,        # desplazamiento lateral izquierdo
    "right": 106,       # desplazamiento lateral derecho
    "upleft": 20,
    "downleft": 129,
    "upright": 72,
    "downright": 34,
    "ccw": 172,         # girar en sentido antihorario
    "cw": 83,           # girar en sentido horario
    "model1": 25,       # manual
    "model2": 26,       # evasion de obstaculos (HC-SR04)
    "model3": 27,       # seguir
    "model4": 28,       # siguelineas
    "servo-left": 230,  # el servo es el pan del ultrasonido, NO una rueda
    "servo-right": 231,
    "servo-center": 232,
}

# Endpoint HTTP del ESP32-CAM para cada orden.
HTTP_PATHS = {
    "stop": "/stop",
    "forward": "/go",
    "back": "/back",
    "left": "/left",
    "right": "/right",
    "upleft": "/leftup",
    "downleft": "/leftdown",
    "upright": "/rightup",
    "downright": "/rightdown",
    "ccw": "/counterclockwise",
    "cw": "/clockwise",
    "model1": "/model1",
    "model2": "/model2",
    "model3": "/model3",
    "model4": "/model4",
    "servo-left": "/motorleft",
    "servo-right": "/motorright",
    "servo-center": "/motorcenter",
    "ledon": "/ledon",
    "ledoff": "/ledoff",
}

# Mapa del modo teclado: tecla -> accion.
KEYS = {
    "w": "forward",
    "s": "back",
    "a": "left",
    "d": "right",
    "q": "ccw",
    "e": "cw",
    "z": "servo-left",
    "x": "servo-right",
    "c": "servo-center",
    "1": "model1",
    "2": "model2",
    "3": "model3",
    "4": "model4",
    " ": "stop",
}


def packet(order):
    return bytes([PACKET_HEAD, order & 0xFF, PACKET_TAIL])


def _resolve(name):
    if name not in ORDERS:
        raise SystemExit(f"orden desconocida: {name}\nvalidas: {', '.join(ORDERS)}")
    return ORDERS[name]


class Robot:
    """Control por USB serial. El UNO mantiene el movimiento hasta la proxima orden."""

    def __init__(self, port="COM4", baud=BAUD):
        import serial  # pyserial
        self.ser = serial.Serial()
        self.ser.port = port
        self.ser.baudrate = baud
        self.ser.timeout = 0.15
        self.ser.dtr = False
        self.ser.rts = False
        self.ser.open()
        self.ser.dtr = False
        self.ser.rts = False
        # Al abrir el puerto el UNO se reinicia (DTR): esperar al boot antes de
        # enviar, si no se pierde el primer paquete.
        time.sleep(2.5)
        self.ser.reset_input_buffer()

    def send(self, name):
        self.ser.write(packet(_resolve(name)))
        self.ser.flush()

    def read_echo(self, wait=0.3):
        """El firmware imprime el byte de orden recibido; util para verificar."""
        out = b""
        end = time.time() + wait
        while time.time() < end:
            data = self.ser.read(64)
            if data:
                out += data
                end = time.time() + 0.05
            else:
                time.sleep(0.02)
        return out

    def move(self, name, seconds=None):
        self.send(name)
        time.sleep(0.15)
        echo = self.read_echo()
        if seconds is not None:
            time.sleep(seconds)
            self.send("stop")
            time.sleep(0.1)

    def close(self):
        self.ser.close()


class RobotHttp:
    """Control por WiFi usando el servidor web del ESP32-CAM (puerto 80)."""

    def __init__(self, ip, timeout=3):
        self.base = f"http://{ip}"
        self.timeout = timeout

    def send(self, name):
        import urllib.request
        path = HTTP_PATHS.get(name)
        if path is None:
            raise SystemExit(f"orden sin endpoint HTTP: {name}")
        with urllib.request.urlopen(self.base + path, timeout=self.timeout) as r:
            return r.status

    def move(self, name, seconds=None):
        self.send(name)
        if seconds is not None:
            time.sleep(seconds)
            self.send("stop")


def _get_key():
    """Lee una tecla sin necesidad de Enter. Devuelve None si no hay (solo Unix)."""
    try:
        import msvcrt  # Windows
        return msvcrt.getch().decode(errors="ignore").lower()
    except ImportError:
        import select
        if not select.select([sys.stdin], [], [], 0.05)[0]:
            return None
        return sys.stdin.read(1).lower()


def teleop(robot):
    print("Modo teclado  (w/a/s/d mover, q/e girar, z/x/c servo, 1-4 modo, espacio frenar, esc salir)")
    terminal = None
    try:
        import msvcrt  # Windows: no hace falta configurar la terminal
        def read_key():
            return msvcrt.getch().decode(errors="ignore").lower()
    except ImportError:
        import termios
        import tty
        fd = sys.stdin.fileno()
        old = termios.tcgetattr(fd)
        tty.setcbreak(fd)
        terminal = (termios, fd, old)
        def read_key():
            import select
            if not select.select([sys.stdin], [], [], 0.1)[0]:
                return ""
            return sys.stdin.read(1).lower()
    try:
        while True:
            key = read_key()
            if key in ("\x1b", "\x03"):  # ESC o Ctrl+C
                break
            action = KEYS.get(key)
            if action:
                robot.send(action)
                print(f"  -> {action}")
    except KeyboardInterrupt:
        pass
    finally:
        if terminal:
            t, fd, old = terminal
            t.tcsetattr(fd, t.TCSADRAIN, old)
        robot.send("stop")
        print("\nfreno y salida")


def list_ports():
    from serial.tools import list_ports as lp
    for p in lp.comports():
        print(f"{p.device}\t{p.description}")


def selftest():
    assert packet(0) == b"\xa5\x00\x5a"
    assert packet(92) == b"\xa5\x5c\x5a"
    assert packet(300) == b"\xa5\x2c\x5a"  # se recorta a un byte
    assert ORDERS["forward"] == 0x5C and ORDERS["back"] == 0xA3
    assert set(HTTP_PATHS) - {"ledon", "ledoff"} <= set(ORDERS)
    print("selftest OK")


def main(argv=None):
    ap = argparse.ArgumentParser(description="Control del robot ZYC0076")
    ap.add_argument("action", nargs="?", choices=sorted(ORDERS) + ["ledon", "ledoff"])
    ap.add_argument("seconds", nargs="?", type=float,
                    help="si se indica, frena con STOP tras ese tiempo")
    ap.add_argument("--port", default="COM4", help="puerto serial (por defecto COM4)")
    ap.add_argument("--ip", help="IP del ESP32-CAM para control por WiFi")
    ap.add_argument("--keyboard", "-k", action="store_true",
                    help="modo teclado (teleop) hasta pulsar ESC")
    ap.add_argument("--list", action="store_true", help="lista puertos COM y sale")
    ap.add_argument("--selftest", action="store_true", help="valida la logica y sale")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()
    if args.list:
        return list_ports()

    if args.ip:
        robot = RobotHttp(args.ip)
    else:
        robot = Robot(args.port)

    if args.keyboard:
        teleop(robot)
    elif args.action:
        robot.move(args.action, args.seconds)
    else:
        ap.error("falta una accion o usa --keyboard")

    if isinstance(robot, Robot):
        robot.close()


if __name__ == "__main__":
    main()
