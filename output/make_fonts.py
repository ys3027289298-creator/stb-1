# Build three tiny TTFs for GSUB tests (all under 2 KB each):
#   plain_font.ttf - no GSUB table (behavior must stay unchanged)
#   gsub_font.ttf  - SingleSubst (f->r) + LigatureSubst (fi, longest ffi)
#   range_font.ttf - Coverage format 2 (ranges) + multi-glyph SingleSubst
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.feaLib.builder import addOpenTypeFeaturesFromString

def box_glyph():
    pen = TTGlyphPen(None)
    pen.moveTo((0, 0)); pen.lineTo((300, 0)); pen.lineTo((300, 600)); pen.lineTo((0, 600))
    pen.closePath(); return pen.glyph()

def build(fname, name, order, cmap_cps, features):
    fb = FontBuilder(1000, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap({ord(c): c for c in cmap_cps})
    fb.setupGlyf({g: (TTGlyphPen(None).glyph() if g == '.notdef' else box_glyph()) for g in order})
    fb.setupHorizontalMetrics({g: (500, 50) for g in order})
    fb.setupHorizontalHeader(ascent=800, descent=-200)
    fb.setupNameTable({'familyName': name, 'styleName': 'Regular'})
    fb.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800, usWinDescent=200)
    fb.setupPost(); fb.setupMaxp()
    if features:
        addOpenTypeFeaturesFromString(fb.font, 'languagesystem DFLT dflt;\n' + features)
    fb.save('output/' + fname)

build('plain_font.ttf', 'PlainTiny', ['.notdef','a','b','c','f','i'], 'abcfi', '')
build('gsub_font.ttf', 'GsubTiny', ['.notdef','a','b','c','f','i','r','fi','ffi'], 'abcfi',
      'feature ss01 { sub f by r; } ss01;\n'
      'feature liga { sub f i by fi; sub f f i by ffi; } liga;\n')
build('range_font.ttf', 'RangeTiny', ['.notdef','a','b','c','d','x','y','z','lig'], 'abcdxyz',
      'feature ss01 { sub [a b c d] by [x y z x]; } ss01;\n'
      'feature liga { sub a b c d by lig; } liga;\n')
print('wrote plain_font.ttf gsub_font.ttf range_font.ttf in output/')
