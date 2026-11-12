#!/usr/bin/env bash
# End-to-end OS/2 BMP demo. Each section prints a title, sections pause 2s,
# and every artifact lands in output/. No keyboard input is required.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p output
LOG=output/run.log
: > "$LOG"

title() { printf '\n===== %s =====\n' "$1" | tee -a "$LOG"; }
step()  { echo "+ $*"; "$@" 2>&1 | tee -a "$LOG"; }

title "1/12 environment"; step cc --version; sleep 4
title "2/12 generate OS/2 BMP fixtures (1bit, 8bit RLE8, 24bit, bad header)"
step python3 gen_bmps.py; sleep 2
title "3/12 fixture listing"; step ls -l output; sleep 4
title "4/12 build decoder (stb_image)"
step cc -O2 -Wall -I../.. decode_demo.c -o output/decode_demo -lm; sleep 2
title "5/12 decode 1-bit palette (OS/2 1.x, 12-byte header)"
step ./output/decode_demo output/os2_1bit.bmp; sleep 2
title "6/12 decode 8-bit RLE8 palette (OS/2 2.x, 64-byte header)"
step ./output/decode_demo output/os2_8bit_rle.bmp; sleep 2
title "7/12 decode 24-bit direct (OS/2 2.x, 64-byte header)"
step ./output/decode_demo output/os2_24bit.bmp; sleep 2
title "8/12 decode bad header (expected failure with reason)"
step ./output/decode_demo output/os2_badheader.bmp || true; sleep 2
title "9/12 Windows BMP regression: write 40-byte header reference then decode"
python3 - <<'EOF'
import struct
px=[(1,2,3),(4,5,6),(7,8,9),(10,11,12)]
def bgr(p): r,g,b=p; return bytes([b,g,r])
data=b"".join(b"".join(bgr(p) for p in row)+b"\0\0" for row in (px[2:4],px[0:2]))
off=14+40
open("output/win_ref.bmp","wb").write(b"BM"+struct.pack("<IHHI",off+len(data),0,0,off)
  +struct.pack("<IiiHHIIiiII",40,2,2,1,24,0,len(data),0,0,0,0)+data)
EOF
step ./output/decode_demo output/win_ref.bmp; sleep 2
title "10/12 stbi_info2 smoke across fixtures"
cat > output/info_check.c <<'EOF'
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <stdio.h>
int main(int c,char**v){for(int i=1;i<c;i++){stbi_info_res r=stbi_info2(v[i]);
 printf("%s -> type=%d %dx%d comp=%d\n",v[i],r.type,r.x,r.y,r.comp);}return 0;}
EOF
step cc -O2 -I../.. output/info_check.c -o output/info_check -lm
step ./output/info_check output/os2_1bit.bmp output/os2_8bit_rle.bmp \
    output/os2_24bit.bmp output/os2_badheader.bmp output/win_ref.bmp || true
sleep 2
title "11/12 combined decode run"
step ./output/decode_demo output/os2_1bit.bmp output/os2_8bit_rle.bmp \
    output/os2_24bit.bmp output/os2_badheader.bmp || true; sleep 2
title "12/12 summary (artifacts in output/)"; step ls -l output; sleep 4
echo "demo finished; full log: $LOG"
