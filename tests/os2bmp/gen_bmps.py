#!/usr/bin/env python3
# Generates four OS/2 BMP variants used by the os2bmp decoding demo.
import struct, os

OUT = os.path.join(os.path.dirname(__file__), "output")
os.makedirs(OUT, exist_ok=True)

def fh(offset):
    return b"BM" + struct.pack("<IHHI", 0, 0, 0, offset)

def core(w, h, bpp):  # OS/2 1.x, 12-byte BITMAPCOREHEADER
    return struct.pack("<IHHHH", 12, w, h, 1, bpp)

def core2(w, h, bpp, comp):  # OS/2 2.x, 64-byte BITMAPCOREHEADER2
    fmt = "<IIIHH" + "I" * 8 + "H" * 4 + "I" * 2
    return struct.pack(fmt, 64, w, h, 1, bpp, comp,
                       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)

def write(name, blob):
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(blob)
    print("wrote", name, len(blob), "bytes")

# 1) 1-bit, OS/2 1.x header, 4x2 checkerboard, 2 palette entries (3 bytes each)
pal1 = bytes([0, 0, 0, 255, 255, 255])
rows1 = [bytes([0b10100000]), bytes([0b01010000])]  # rows padded to 4 bytes
blob = fh(14 + 12 + 6) + core(4, 2, 1) + pal1 + rows1[0] + b"\0\0\0" + rows1[1] + b"\0\0\0"
write("os2_1bit.bmp", blob)

# 2) 8-bit RLE8, OS/2 2.x header, 6x2, 4 palette entries
pal8 = bytes([255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0])
rle = bytes([3, 1, 2, 2, 1, 3, 0, 0,       # bottom row: idx 1x3, 2x2, 3x1
             4, 0, 2, 2, 1, 0, 1])          # top row: idx 0x4, absolute 2,1 (word aligned)
blob = fh(14 + 64 + 12) + core2(6, 2, 8, 1) + pal8 + rle
write("os2_8bit_rle.bmp", blob)

# 3) 24-bit uncompressed, OS/2 2.x header, 3x2, rows padded to 4 bytes
def bgr(px):
   r, g, b = px
   return bytes([b, g, r])
# image top-to-bottom rows; stb flips the bottom-up disk order, so write reversed
image_rows = [[(255, 0, 0), (0, 255, 0), (0, 0, 255)],
              [(10, 20, 30), (40, 50, 60), (70, 80, 90)]]
row_bytes = b"".join(bgr(px) for px in image_rows[0])
pad = (-len(row_bytes)) & 3  # rows are padded to a multiple of 4 bytes
disk = b"".join(b"".join(bgr(px) for px in row) + b"\0" * pad for row in image_rows[::-1])
blob = fh(14 + 64) + core2(3, 2, 24, 0) + disk
write("os2_24bit.bmp", blob)

# 4) bad header: valid OS/2 2.x header shell but unsupported 16 bpp
blob = fh(14 + 64) + core2(4, 2, 16, 0)
write("os2_badheader.bmp", blob)
