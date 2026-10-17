#!/usr/bin/env python3
"""Controlar el robot ZYC0076 con el mando Funlab FF01 (clon Switch Pro).

El mando es Bluetooth Classic HID: se empareja al PC (modo PC: B + HOME) y
Windows lo ve como gamepad. pygame lo lee y manda las ordenes al UNO por USB.

    python gamepad.py --detect     # ver ejes/botones del mando
    python gamepad.py              # manejar el robot (Ctrl+C sale)
    python gamepad.py --port COM4

Correr con el Python de Windows (C:\\Python314\\python.exe gamepad.py ...).
"""

import argparse
import time

from robot import Robot

# Indices de ejes/botones. Si --detect muestra otros, ajustar aqui.
AX_LX, AX_LY, AX_RX = 0, 1, 2
BTN = {"a": 0, "b": 1, "x": 2, "y": 3, "lb": 4, "rb": 5, "back": 6, "start": 7}
# boton mantenido -> accion (el resto son puntuales)
BUTTON_ACTIONS = {"a": "stop", "b": "model1", "x": "model2", "y": "model3",
                  "lb": "servo-left", "rb": "servo-right"}
HOLD = {"lb", "rb"}          # servo: se repite mientras se mantiene
AX_LT, AX_RT = 4, 5          # gatillos; en reposo valen -1
TRIG_ACTIONS = {AX_LT: "model4", AX_RT: "servo-center"}
DEAD = 0.35                  # zona muerta del stick (calibrado: centro exacto)
SMOOTH = 0.5                 # suavizado de los sticks (menor = mas suave)


def _pad():
    import pygame
    pygame.init()
    pygame.joystick.init()
    if pygame.joystick.get_count() == 0:
        raise SystemExit("no hay mando; empareja el FF01 (mantén B + HOME)")
    js = pygame.joystick.Joystick(0)
    js.init()
    return pygame, js


def detect():
    pygame, js = _pad()
    print(f"mando 0: {js.get_name()}  ejes={js.get_numaxes()} "
          f"botones={js.get_numbuttons()} hats={js.get_numhats()}")
    print("mueve los ejes / pulsa botones (15 s)...")
    last = {}
    t0 = time.time()
    while time.time() - t0 < 15:
        pygame.event.pump()
        for i in range(js.get_numaxes()):
            v = round(js.get_axis(i), 2)
            if abs(v - last.get(("a", i), 0)) > 0.3:
                last[("a", i)] = v
                print(f"  eje {i} = {v}")
        for i in range(js.get_numbuttons()):
            v = js.get_button(i)
            if v != last.get(("b", i), 0):
                last[("b", i)] = v
                print(f"  boton {i} = {v}")
        for i in range(js.get_numhats()):
            v = js.get_hat(i)
            if v != last.get(("h", i), (0, 0)):
                last[("h", i)] = v
                print(f"  hat {i} = {v}")
        time.sleep(0.03)


def control(port):
    pygame, js = _pad()
    robot = Robot(port)
    print(f"mando: {js.get_name()}   (Ctrl+C para salir)")
    print("  stick izq = avanzar/atras + girar | stick der = lateral\n"
          "  A stop | B/X/Y = modo1/2/3 | LT = modo4 | LB/RB servo | RT = centrar servo")
    last_move, last_btn, servo_t = None, {}, 0.0
    sx = sy = sr = 0.0                       # sticks suavizados (filtro)
    last_send = 0.0
    try:
        while True:
            pygame.event.pump()
            # suavizado exponencial: quita el ruido del mando (evita tirones)
            sx += (js.get_axis(AX_LX) - sx) * SMOOTH
            sy += (js.get_axis(AX_LY) - sy) * SMOOTH
            arx = js.get_axis(AX_RX) if js.get_numaxes() > AX_RX else 0
            sr += (arx - sr) * SMOOTH
            # histeresis: seguir cuesta menos que arrancar (no rebota en el borde)
            thr = DEAD * 0.5 if last_move else DEAD
            if sy < -thr:
                move = "forward"
            elif sy > thr:
                move = "back"
            elif sx < -thr:
                move = "ccw"
            elif sx > thr:
                move = "cw"
            elif sr < -thr:
                move = "left"
            elif sr > thr:
                move = "right"
            else:
                move = None
            now = time.time()
            if move != last_move and now - last_send > 0.04:
                robot.send(move or "stop")
                print(f"  -> {move or 'stop'}")
                last_move, last_send = move, now

            now = time.time()
            for name, idx in BTN.items():
                if idx >= js.get_numbuttons():
                    continue
                pressed = bool(js.get_button(idx))
                act = BUTTON_ACTIONS.get(name)
                if act and pressed:
                    if name in HOLD:
                        if now - servo_t > 0.06:
                            servo_t = now
                            robot.send(act)
                    elif not last_btn.get(name):
                        robot.send(act)
                        print(f"  {name} -> {act}")
                last_btn[name] = pressed
            for aidx, act in TRIG_ACTIONS.items():
                if aidx >= js.get_numaxes():
                    continue
                pressed = js.get_axis(aidx) > 0.5
                if pressed and not last_btn.get(aidx):
                    robot.send(act)
                    print(f"  gatillo {aidx} -> {act}")
                last_btn[aidx] = pressed
            time.sleep(0.02)
    except KeyboardInterrupt:
        pass
    finally:
        robot.send("stop")
        robot.close()
        print("\nfreno y salida")


def calibrate():
    pygame, js = _pad()
    n = js.get_numaxes()
    print(f"mando: {js.get_name()}  ({n} ejes)")
    print("0) Despierta el mando: mueve un stick hasta que responda (max 8 s)...")
    t0 = time.time()
    while time.time() - t0 < 8:
        pygame.event.pump()
        if any(abs(js.get_axis(i)) > 0.5 for i in range(n)) or \
           any(js.get_button(i) for i in range(js.get_numbuttons())):
            print("   mando despierto.")
            break
        time.sleep(0.05)
    else:
        print("   OJO: no respondio; puede estar dormido o apagado.")
    print("1) Suelta los sticks y los gatillos. Midiendo reposo (3 s)...")
    t0 = time.time()
    acc = [0.0] * n
    cnt = 0
    while time.time() - t0 < 3:
        pygame.event.pump()
        for i in range(n):
            acc[i] += js.get_axis(i)
        cnt += 1
        time.sleep(0.02)
    center = [a / cnt for a in acc]
    lo, hi = list(center), list(center)
    print("2) Ahora mueve los DOS sticks en circulos completos y aprieta los")
    print("   gatillos a fondo (15 s)...")
    t0 = time.time()
    nxt = 0.0
    while time.time() - t0 < 15:
        pygame.event.pump()
        for i in range(n):
            v = js.get_axis(i)
            lo[i], hi[i] = min(lo[i], v), max(hi[i], v)
        el = time.time() - t0
        if el >= nxt:
            print(f"   t={el:4.1f}s  {[round(js.get_axis(i), 2) for i in range(n)]}")
            nxt += 2
        time.sleep(0.01)
    print("   eje   centro    min      max")
    for i in range(n):
        print(f"   {i:>2}   {center[i]:+.3f}   {lo[i]:+.2f}   {hi[i]:+.2f}")
    off = max(abs(center[0]), abs(center[1]), abs(center[2]))
    dead = min(0.6, round(off + 0.15, 2))
    print(f"\n   Centro de los sticks: desvio max {off:.3f} "
          f"({'OK, centrado' if off < 0.05 else 'DESCENTRADO'})")
    print(f"   Recorrido sticks: {'completo (±1)' if min(lo[0], lo[1], lo[2]) < -0.9 and max(hi[0], hi[1], hi[2]) > 0.9 else 'revisar'}")
    print(f"   -> usa DEAD = {max(0.45, dead)} en gamepad.py")


def main():
    ap = argparse.ArgumentParser(description="Mando FF01 -> robot ZYC0076")
    ap.add_argument("--detect", action="store_true", help="mostrar ejes/botones y salir")
    ap.add_argument("--calibrate", action="store_true", help="medir centro/recorrido de ejes")
    ap.add_argument("--port", default="COM4")
    a = ap.parse_args()
    if a.detect:
        detect()
    elif a.calibrate:
        calibrate()
    else:
        control(a.port)


if __name__ == "__main__":
    main()
