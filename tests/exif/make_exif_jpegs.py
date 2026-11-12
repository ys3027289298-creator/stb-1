#!/usr/bin/env python3
# Inject EXIF APP1 Orientation (tag 0x0112) into base.jpg; also a corrupt one.
import os, struct
OUT = os.path.dirname(__file__)
def app1(orientation, corrupt=False):
    t = b"II" + struct.pack("<H", 0x2A) + struct.pack("<I", 8) + struct.pack("<H", 1)
    t += struct.pack("<HHI", 0x0112, 3, 1) + struct.pack("<HH", orientation, 0) + struct.pack("<I", 0)
    if corrupt:
        t = b"ZZ" + b"\x00" * (len(t) - 2)
    p = b"Exif\x00\x00" + t
    return b"\xFF\xE1" + struct.pack(">H", len(p) + 2) + p
raw = open(os.path.join(OUT, "base.jpg"), "rb").read()
for o in (1, 3, 6, 8):
    open(os.path.join(OUT, "orient_%d.jpg" % o), "wb").write(raw[:2] + app1(o) + raw[2:])
open(os.path.join(OUT, "corrupt_exif.jpg"), "wb").write(raw[:2] + app1(1, True) + raw[2:])
print("injected EXIF orientations 1/3/6/8 plus a corrupt segment into", OUT)
