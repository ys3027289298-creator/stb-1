#!/usr/bin/env python3
# Builds the two fonts used by cff2_test.c: a plain TTF and a variable CFF2 OTF
# sharing the same default-instance geometry.
import os
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.pens.t2CharStringPen import T2CharStringPen
from fontTools.designspaceLib import DesignSpaceDocument, AxisDescriptor, SourceDescriptor
from fontTools.varLib import build

HERE = os.path.dirname(os.path.abspath(__file__)); ORDER = ['.notdef', 'A', 'B', 'C']

def rect(pen, pts):
    pen.moveTo(pts[0]); [pen.lineTo(p) for p in pts[1:]]; pen.closePath()

def draw(name, pen, wght=400):
    dx, dy = (40, 60) if wght == 900 else (0, 0)
    if name == 'A':
        rect(pen, ((100, 0), (100, 700+dy), (170+dx, 700+dy), (170+dx, 0)))
    elif name == 'B':
        pt = (430 + dx, 700 + dy)
        if isinstance(pen, TTGlyphPen):
            pen.moveTo((250, 0)); pen.qCurveTo((250, 400), pt); pen.lineTo((500, 700+dy)); pen.qCurveTo((500, 350), (320, 0)); pen.closePath()
        else:
            pen.moveTo((250, 0)); pen.curveTo((250, 400), pt, pt); pen.lineTo((500, 700+dy)); pen.curveTo((500, 350), (320, 350), (320, 0)); pen.closePath()
    else:
        rect(pen, ((560, 0), (620, 0), (620, 50), (560, 50)) if name == 'C' else ((0, 0), (600, 0), (600, 80), (0, 80)))

def base_tables(fb):
    fb.setupGlyphOrder(ORDER); fb.setupCharacterMap({ord('A'): 'A', ord('B'): 'B', ord('C'): 'C'})
    fb.setupHorizontalMetrics({n: (700, 0) for n in ORDER})
    fb.setupHorizontalHeader(ascent=800, descent=-200); fb.setupNameTable({'familyName': 'Cff2Proto', 'styleName': 'Regular'})
    fb.setupOS2(); fb.setupPost(); fb.setupMaxp()

def make_ttf():
    fb = FontBuilder(1000, isTTF=True); base_tables(fb); glyphs = {}
    for n in ORDER:
        pen = TTGlyphPen(None); draw(n, pen); glyphs[n] = pen.glyph()
    fb.setupGlyf(glyphs); fb.font.save(os.path.join(HERE, 'proto.ttf'))

def make_master(path, wght):
    fb = FontBuilder(1000, isTTF=False); base_tables(fb); cs = {}
    for n in ORDER:
        pen = T2CharStringPen(700, None); draw(n, pen, wght); cs[n] = pen.getCharString()
    fb.setupCFF('Cff2Proto', {'FullName': 'Cff2Proto'}, cs, {}); fb.font.save(path)
def make_cff2():
    reg, bold = os.path.join(HERE, 'regular.otf'), os.path.join(HERE, 'bold.otf')
    make_master(reg, 400); make_master(bold, 900); doc = DesignSpaceDocument()
    ax = AxisDescriptor(); ax.name, ax.tag = 'Weight', 'wght'; ax.minimum, ax.default, ax.maximum = 400, 400, 900; doc.addAxis(ax)
    for w, path in ((400, reg), (900, bold)):
        s = SourceDescriptor(); s.path, s.location = path, {'Weight': w}; s.copyLib = s.copyInfo = True; doc.addSource(s)
    ds = os.path.join(HERE, 'proto.designspace'); doc.write(ds); font, _, _ = build(ds)
    for item in font['CFF2'].cff.topDictIndex[0].CharStrings.values():
        item.program = [0, 'vsindex'] + item.program  # exercise both CFF2 variation ops
    font.save(os.path.join(HERE, 'proto_cff2.otf'))

if __name__ == '__main__':
    make_ttf(); make_cff2()
    [os.remove(os.path.join(HERE, j)) for j in ('regular.otf', 'bold.otf', 'proto.designspace')]
    print('wrote proto.ttf and proto_cff2.otf')
