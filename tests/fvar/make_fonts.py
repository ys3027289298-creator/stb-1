#!/usr/bin/env python3
# Generates four minimal sfnt files for the fvar tests (no external deps).
import os, struct
OUT = os.path.join(os.path.dirname(__file__), "fonts")
os.makedirs(OUT, exist_ok=True)
u16 = lambda v: struct.pack(">H", v)
u32 = lambda v: struct.pack(">I", v)
tag = lambda s: s.encode("ascii")
fixed = lambda v: struct.pack(">i", int(round(v * 65536.0)))

def cmap():
    table = bytes((1 if c == 65 else 0) for c in range(256))  # format 0: only 'A' -> glyph 1
    sub = u16(0) + u16(262) + u16(0) + table
    return u16(0) + u16(1) + u16(3) + u16(1) + u32(12) + sub

def head():
    return u32(0x00010000) + u32(0)*3 + u32(0x5F0F3CF5) + u16(0) + u16(1000) + b"\0"*16 \
        + struct.pack(">hhhh", 0, 0, 1000, 1000) + u16(0) + u16(8) + struct.pack(">hhh", -1, 0, 0)

def hhea():
    return fixed(1.0) + struct.pack(">hhh", 800, -200, 0) + u16(1000) \
        + struct.pack(">hhh", 1, 0, 0) + u16(2)

def fvar(axes, instances):
    ac, ic, asz, isz = len(axes), len(instances), 20, 6 + 4*len(axes)
    h = u16(1) + u16(0) + u16(16) + u16(0) + u16(ac) + u16(asz) + u16(ic) + u16(isz)
    body = b"".join(tag(t) + fixed(lo) + fixed(df) + fixed(hi) + u16(0) + u16(256+i)
                    for i, (t, lo, df, hi) in enumerate(axes))
    body += b"".join(u16(256+ac+j) + u16(0) + u16(0) + b"".join(fixed(v) for v in vals)
                     for j, vals in enumerate(instances))
    return h + body

def sfnt(tables):
    out = u32(0x00010000) + u16(len(tables)) + u16(16) + u16(0) + u16(0)
    body, off, directory = b"", 12 + 16*len(tables), b""
    for name, data in sorted(tables.items()):
        directory += tag(name) + u32(0) + u32(off) + u32(len(data))
        body += data + (b"\0" if len(data) % 2 else b"")
        off += len(data) + (len(data) % 2)
    return out + directory + body

def base_tables():
    glyf = struct.pack(">hhhhh", 1, 0, 0, 500, 700) + b"\0"*10
    return {"cmap": cmap(), "head": head(), "hhea": hhea(),
            "hmtx": (u16(600) + struct.pack(">h", 0)) * 2, "maxp": u32(0x00010000) + u16(2),
            "name": u16(0) + u16(0) + u16(6), "glyf": glyf, "loca": u16(0)*3}

t = base_tables()
t["fvar"] = fvar([("wght", 100, 400, 900)], [[400], [700]])
open(os.path.join(OUT, "var_wght.ttf"), "wb").write(sfnt(t))
t = base_tables()
t["fvar"] = fvar([("wght", 100, 400, 900), ("wdth", 75, 100, 125)], [[400, 100], [700, 75]])
open(os.path.join(OUT, "var_two_axes.ttf"), "wb").write(sfnt(t))
t = base_tables()
open(os.path.join(OUT, "regular.ttf"), "wb").write(sfnt(t))
t = base_tables()
t["fvar"] = fvar([("wght", 100, 400, 900)], [[400]])[:18]  # truncated mid first axis record
open(os.path.join(OUT, "corrupt_fvar.ttf"), "wb").write(sfnt(t))
print("wrote", len(os.listdir(OUT)), "fonts to", OUT)
