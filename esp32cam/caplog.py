import serial, time, sys

out = sys.argv[1]
dur = float(sys.argv[2])
s = serial.Serial('COM5', 115200, timeout=0.5)
s.dtr = False
s.rts = False
open(out, 'wb').close()
f = open(out, 'ab')
t = time.time()
while time.time() - t < dur:
    d = s.read(512)
    if d:
        f.write(d)
        f.flush()
f.close()
s.close()
print('capturados', __import__('os').path.getsize(out), 'bytes')
