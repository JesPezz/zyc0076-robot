import sys
import serial

N = {
    0: "STOP", 92: "FWD", 163: "BACK", 149: "LEFT", 106: "RIGHT",
    20: "UPLEFT", 129: "DOWNLEFT", 72: "UPRIGHT", 34: "DOWNRIGHT",
    172: "CCW", 83: "CW", 25: "M1", 26: "M2", 27: "M3", 28: "M4",
    230: "SVL", 231: "SVR", 232: "SVC",
}

port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
s = serial.Serial(port, 115200, timeout=0.5)
s.dtr = False
s.rts = False
print("MONITOR %s @115200  (Ctrl+C para parar)" % port, flush=True)

buf = bytearray()
while True:
    d = s.read(64)
    if d:
        buf.extend(d)

    while True:
        i = buf.find(b"\xa5")
        if i < 0:
            buf.clear()
            break
        if len(buf) < i + 3:
            del buf[:i]
            break
        if buf[i + 2] == 0x5A:
            o = buf[i + 1]
            print("  orden %3d  %s" % (o, N.get(o, "?")), flush=True)
            del buf[: i + 3]
        else:
            del buf[i : i + 1]
