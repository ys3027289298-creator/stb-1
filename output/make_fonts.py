#!/usr/bin/env python3
# Builds a test font from font?_pairs.txt and exports reference kerning
# decoded from its Apple 'kern' version 0 table (raw bytes via fonttools).
# usage: make_fonts.py A|B|C   (C = control font without a 'kern' table)
import os, struct, sys
from fontTools.fontBuilder import FontBuilder
from fontTools.ttLib import TTFont
from fontTools.ttLib.tables.DefaultTable import DefaultTable
from fontTools.pens.ttGlyphPen import TTGlyphPen

HERE = os.path.dirname(os.path.abspath(__file__))
TAG = sys.argv[1]
SRC = {"A": "fontA_pairs.txt", "B": "fontB_pairs.txt", "C": "fontA_pairs.txt"}[TAG]

pairs = []
for line in open(os.path.join(HERE, SRC)):
    line = line.split("#")[0].split()
    if line:
        pairs.append((line[0], line[1], int(line[2])))

chars = sorted({c for l, r, v in pairs for c in (l, r)})
order = [".notdef"] + chars
fb = FontBuilder(1000)
fb.setupGlyphOrder(order)
fb.setupCharacterMap({ord(c): c for c in chars})
fb.setupGlyf({g: TTGlyphPen(None).glyph() for g in order})
fb.setupHorizontalMetrics({g: (600, 0) for g in order})
fb.setupHorizontalHeader(); fb.setupOS2(); fb.setupPost(); fb.setupMaxp()
fb.setupNameTable({"familyName": "AppleKern" + TAG, "styleName": "Regular"})

if TAG != "C":  # Apple 'kern' v0: u32 version, u32 nTables, u32 subtable length
    gid = {g: i for i, g in enumerate(order)}
    rec = sorted((gid[l], gid[r], v) for l, r, v in pairs if v)
    body = struct.pack(">4H", len(rec), 0, 0, 0)
    body += b"".join(struct.pack(">2Hh", *r) for r in rec)
    kern = DefaultTable("kern")
    kern.data = struct.pack(">LL", 0, 1) + struct.pack(">LHH", 8 + len(body), 0, 0) + body
    fb.font["kern"] = kern

path = os.path.join(HERE, "font%s.ttf" % TAG)
fb.save(path)

font = TTFont(path)
cmap = font.getBestCmap()
ref = {}
if "kern" in font:
    ent = font.reader.tables["kern"]
    font.reader.file.seek(ent.offset)
    raw = font.reader.file.read(ent.length)
    assert struct.unpack(">L", raw[:4])[0] == 0, "not an Apple kern v0 table"
    for i in range(struct.unpack(">H", raw[16:18])[0]):
        g1, g2, v = struct.unpack(">2Hh", raw[24 + 6 * i:30 + 6 * i])
        ref[(g1, g2)] = v
with open(os.path.join(HERE, "ref_%s.txt" % TAG), "w") as f:
    for l, r, v in pairs:
        ids = (font.getGlyphID(cmap[ord(l)]), font.getGlyphID(cmap[ord(r)]))
        f.write("%s %s %d\n" % (l, r, ref.get(ids, 0)))
print("built %s + ref_%s.txt (%d pairs)" % (path, TAG, len(pairs)))
