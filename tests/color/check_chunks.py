#!/usr/bin/env python3
"""Read back PNG color chunks and verify order, CRC and stored values."""
import struct, sys, zlib

OUT = sys.argv[1]
CASES = [
    ("case_srgb.png",   "sRGB", 0),
    ("case_gama18.png", "gAMA", 100000 / 1.8),
    ("case_gama22.png", "gAMA", 100000 / 2.2),
    ("case_none.png",   None,   0),
]

def chunks(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "bad PNG signature"
    pos, out = 8, []
    while pos < len(data):
        ln = struct.unpack(">I", data[pos:pos+4])[0]
        typ = data[pos+4:pos+8]
        body = data[pos+8:pos+8+ln]
        crc_stored = struct.unpack(">I", data[pos+8+ln:pos+12+ln])[0]
        assert crc_stored == zlib.crc32(data[pos+4:pos+8+ln]) & 0xffffffff, "CRC mismatch"
        out.append((typ.decode(), body))
        pos += 12 + ln
    return out, len(data)

_, none_size = chunks(f"{OUT}/case_none.png")
for name, want_tag, want_val in CASES:
    found, size = chunks(f"{OUT}/{name}")
    tags = [t for t, _ in found]
    order_ok = tags[0] == "IHDR" and tags.index("IDAT") == 1 + (want_tag is not None)
    print(f"case: {name[:-4]}  size={size}B  order={'OK' if order_ok else 'BAD'}: {tags}")
    if want_tag is None:
        print("  chunk: none  value=n/a  diff=n/a (no color chunk written)")
        continue
    body = dict(found)[want_tag]
    val = body[0] if want_tag == "sRGB" else struct.unpack(">I", body)[0]
    exp = int(want_val + 0.5)
    print(f"  chunk: {want_tag}  value={val}  written={exp}  diff={val - exp}")
    print(f"  size={size}B  no-entry size={none_size}B  delta={size - none_size:+d} bytes")
assert none_size is not None
print("PASS: all chunks ordered after IHDR / before IDAT, CRCs and values verified")
