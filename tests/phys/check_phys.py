import struct, sys, zlib, os

CASES = [
    ("phys_96dpi.png",  (3780, 3780)),
    ("phys_300dpi.png", (11811, 11811)),
    ("phys_honly.png",  (3780, 3780)),
    ("phys_none.png",   None),
]

def read_phys(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "bad PNG signature"
    pos, phys = 8, None
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos+4])
        ctype = data[pos+4:pos+8]
        body, crc = data[pos+8:pos+8+length], data[pos+8+length:pos+12+length]
        assert zlib.crc32(ctype + body) == struct.unpack(">I", crc)[0], "CRC mismatch"
        if ctype == b"pHYs":
            phys = struct.unpack(">IIB", body)
        pos += 12 + length
    return phys

try:
    from PIL import Image
except ImportError:
    Image = None

print(f"{'case':16} {'written':>14} {'readback':>14} {'PIL dpi':>16}  bytes")
for name, written in CASES:
    path = os.path.join(os.path.dirname(__file__), "output", name)
    got = read_phys(path)
    readback = "no pHYs" if got is None else f"({got[0]},{got[1]},u={got[2]})"
    expect = "no pHYs" if written is None else str(written)
    ok = (got is None) if written is None else got[:2] == written and got[2] == 1
    pil = "-"
    if Image is not None:
        dpi = Image.open(path).info.get("dpi")
        pil = "-" if dpi is None else f"({dpi[0]:.1f},{dpi[1]:.1f})"
    print(f"{name:16} {expect:>14} {readback:>14} {pil:>16}  {os.path.getsize(path)}  {'OK' if ok else 'FAIL'}")
    assert ok, name
print("all cases OK")
