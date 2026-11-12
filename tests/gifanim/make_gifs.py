#!/usr/bin/env python3
# Generates four small test GIFs using only the stdlib.
import struct, os

def lzw(data, mc=2):
    clear, end = 1 << mc, (1 << mc) + 1
    tab = {bytes([i]): i for i in range(clear)}
    nxt, csz, bits, nb, out = clear + 2, mc + 1, 0, 0, []
    def bump():
        nonlocal csz
        if nxt == (1 << csz) + 1 and csz < 12: csz += 1
    def emit(c):
        nonlocal bits, nb
        bits |= c << nb; nb += csz
        while nb >= 8: out.append(bits & 255); bits >>= 8; nb -= 8
    emit(clear); w = b""
    for ch in data:
        wc = w + bytes([ch])
        if wc in tab: w = wc
        else:
            emit(tab[w]); tab[wc] = nxt
            nxt += 1; bump()
            w = bytes([ch])
    if w: emit(tab[w])
    emit(end)
    if bits: out.append(bits)
    return bytes(out)

def blocks(p):
    return b"".join(bytes([min(255, len(p) - i)]) + p[i:i+255] for i in range(0, len(p), 255)) + b"\x00"

def gif(path, w, h, pal, frames):
    q = b"GIF89a" + struct.pack("<HHBBB", w, h, 0xF0 | (len(pal).bit_length() - 2), 0, 0)
    q += b"".join(struct.pack("BBB", *c) for c in pal)
    for delay, disp, tr, l, t, fw, fh, idx in frames:
        q += b"\x21\xF9\x04" + bytes([(disp << 2) | (1 if tr >= 0 else 0)]) + struct.pack("<H", delay) + bytes([tr & 255, 0])
        q += b"\x2C" + struct.pack("<HHHH", l, t, fw, fh) + b"\x00" + bytes([2]) + blocks(lzw(idx))
    open(path, "wb").write(q + b"\x3B")

W, H, D = 5, 5, os.path.dirname(__file__)
PAL = [(255, 0, 0), (0, 200, 0), (0, 0, 255), (255, 255, 255)]
C = W // 2
grid = lambda dot: [dot if (r == C and c == C) else 3 for r in range(H) for c in range(W)]
P = lambda n: os.path.join(D, n)
gif(P("two_frame.gif"), W, H, PAL, [(10, 1, -1, 0, 0, W, H, grid(0)), (25, 1, -1, 0, 0, W, H, grid(1))])
gif(P("transparent.gif"), W, H, PAL, [(15, 1, 3, 0, 0, W, H, grid(0)), (30, 1, 3, C, C, 1, 1, [1])])
gif(P("dispose2.gif"), W, H, PAL,
    # frame 0: full frame red center on white; frame 1: green center, dispose=2;
    # frame 2: touches only top-left, so center must show the restored state
    [(20, 1, -1, 0, 0, W, H, grid(0)), (40, 2, -1, C, C, 1, 1, [1]),
     (60, 1, -1, 0, 0, 1, 1, [2])])
gif(P("single.gif"), W, H, PAL, [(50, 0, -1, 0, 0, W, H, grid(2))])
print("wrote 4 gifs into", D)
