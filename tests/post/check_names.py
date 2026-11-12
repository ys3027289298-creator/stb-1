import sys
from fontTools.ttLib import TTFont

font = TTFont(sys.argv[1])
expected = font.getGlyphOrder()[:20] if "post" in font else [""] * 20
got = [line.split(" ", 1)[1].rstrip("\n") for line in open(sys.argv[2])]
ok = True
for i in range(20):
    match = got[i] == expected[i]
    ok = ok and match
    print("%2d  stb=%-18s fonttools=%-18s %s" % (i, got[i], expected[i], "ok" if match else "MISMATCH"))
print("PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
