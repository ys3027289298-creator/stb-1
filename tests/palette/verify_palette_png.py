import os
from PIL import Image


def chunks(path):
    out = {}
    with open(path, "rb") as f:
        data = f.read()
    pos = 8
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        tag = data[pos + 4:pos + 8].decode("latin1")
        out[tag] = data[pos + 8:pos + 8 + length]
        pos += 12 + length
    return out, data


def check(path, expected_pal, samples):
    c, raw = chunks(path)
    bit_depth, color_type = raw[24], raw[25]
    im = Image.open(path)
    rgba = im.convert("RGBA")
    ok = bit_depth == 8 and color_type == 3 and len(c["PLTE"]) // 3 == expected_pal
    trns = "tRNS" in c
    for (sx, sy), want in samples:
        px = rgba.getpixel((sx, sy))
        ok = ok and px == want
    print(f"{os.path.basename(path):12s} depth={bit_depth} ctype={color_type} "
          f"palette={len(c['PLTE']) // 3} trns={trns} samples={samples} -> {'PASS' if ok else 'FAIL'} {[rgba.getpixel(p[0]) for p in samples]}")
    return ok


def rgb16(i):
    return ((i % 16) * 16, 255 - (i % 16) * 16, 128, 255)


all_ok = True
all_ok &= check("output/pal16.png", 16, [((0, 0), rgb16(0)), ((1, 0), rgb16(1)), ((3, 1), rgb16(19))])
all_ok &= check("output/pal256.png", 256, [((0, 0), (0, 255, 0, 255)),
                                           ((15, 7), (127, 128, (127 * 7) & 255, 255))])
all_ok &= check("output/paltrns.png", 8, [((0, 0), (0, 100, 200, 255)),
                                          ((3, 0), (90, 100, 170, 0)),
                                          ((6, 0), (180, 100, 140, 128))])
bad_exists = os.path.exists("output/palbad.png")
print(f"palbad.png   exists={bad_exists} -> {'PASS' if not bad_exists else 'FAIL'} (out-of-range index must fail without writing)")
all_ok &= not bad_exists
print("OVERALL:", "PASS" if all_ok else "FAIL")
raise SystemExit(0 if all_ok else 1)
