#!/usr/bin/env python3
# Reads back the PNGs written by write_cases and reports iCCP chunk status.
import os, struct, sys, zlib

def find_iccp(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    pos = 8
    while pos + 12 <= len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        tag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if tag == b"iCCP":
            return body
        pos += 12 + length
        if tag == b"IEND":
            break
    return None

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    outdir = sys.argv[1] if len(sys.argv) > 1 else "output"
    with open(os.path.join(here, "fake.icc"), "rb") as f:
        original = f.read()
    cases = [("with_icc", original), ("empty_icc", b""),
             ("long_name", None), ("no_icc", None)]
    all_ok = True
    for name, expected in cases:
        body = find_iccp(os.path.join(outdir, "case_%s.png" % name))
        present = body is not None
        line = "case=%-10s iccp_present=%s" % (name, "yes" if present else "no")
        if present:
            nul = body.index(b"\x00")
            pname, method = body[:nul].decode("latin-1"), body[nul + 1]
            raw = zlib.decompress(body[nul + 2:])
            match = (expected is not None) and raw == expected
            line += (" name=%r method=%d decompressed=%d original=%d match=%s"
                     % (pname, method, len(raw), len(expected or b""),
                        "yes" if match else "no"))
            all_ok &= match
        else:
            line += " (expected: %s)" % ("no" if expected is None else "yes")
            all_ok &= expected is None
        print(line)
    print("RESULT: %s" % ("PASS" if all_ok else "FAIL"))
    return 0 if all_ok else 1

if __name__ == "__main__":
    sys.exit(main())
