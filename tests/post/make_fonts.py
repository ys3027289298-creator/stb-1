from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
NAMES = [".notdef", ".null", "space"] + [chr(c) for c in range(65, 81)] + ["f_f", "f_f_i", "logo.mark"]
def build(path, keep_post):
    fb = FontBuilder(1000)
    fb.setupGlyphOrder(NAMES)
    fb.setupCharacterMap({32: "space", **{ord(c): c for c in NAMES if len(c) == 1 and c.isalpha()}})
    glyphs = {}
    for name in NAMES:
        pen = TTGlyphPen(None); pen.moveTo((0, 0)); pen.lineTo((400, 0)); pen.lineTo((400, 600)); pen.closePath()
        glyphs[name] = pen.glyph()
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics({n: (500, 0) for n in NAMES})
    fb.setupHorizontalHeader(ascent=800, descent=-200); fb.setupOS2(); fb.setupMaxp()
    fb.setupNameTable({"familyName": "PostTest", "styleName": "Regular"})
    fb.setupPost(formatType=2.0)
    if not keep_post: del fb.font["post"]
    fb.save(path)
build("regular.ttf", True)
build("no_post.ttf", False)
