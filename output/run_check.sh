#!/bin/bash
# Staged check for Apple 'kern' version 0 support in stb_truetype.
# Prints stage titles, pauses ~2s between stages, writes everything to output/.
set -e
cd "$(dirname "$0")/.."
OUT=output; N=0; FAIL=0; START=$(date +%s)
stage() { N=$((N + 1)); echo; echo "=== Stage $N: $1 ==="; sleep 2; }

stage "Check toolchain (gcc, python3, fonttools)"
gcc --version | head -1
python3 -c "import fontTools; print('fonttools', fontTools.version)"

stage "Generate font A (Apple kern v0) + fonttools reference"
python3 "$OUT/make_fonts.py" A
stage "Generate font B (Apple kern v0) + fonttools reference"
python3 "$OUT/make_fonts.py" B
stage "Generate control font C (no kern table) + reference"
python3 "$OUT/make_fonts.py" C

stage "Inspect Apple kern table layout (font A)"
python3 -c "import struct; from fontTools.ttLib import TTFont; f=TTFont('$OUT/fontA.ttf'); e=f.reader.tables['kern']; f.reader.file.seek(e.offset); raw=f.reader.file.read(16); v,n=struct.unpack('>LL',raw[:8]); l,c,t=struct.unpack('>LHH',raw[8:16]); print('version=%d nTables=%d | subtable: length=%d coverage=0x%04x tupleIndex=%d' % (v,n,l,c,t))"
stage "Verify control font C has no kern table"
python3 -c "from fontTools.ttLib import TTFont; f=TTFont('$OUT/fontC.ttf'); assert 'kern' not in f.reader.tables; print('font C: no kern table, as expected')"

stage "Compile existing repo example tests/test_truetype.c (regression)"
gcc -I. -Itests -c tests/test_truetype.c -o "$OUT/test_truetype.o" -Wall
echo "compiled OK"
stage "Compile tests/apple_kern_example.c"
gcc -I. tests/apple_kern_example.c -o "$OUT/apple_kern_example" -Wall -Wextra -lm
echo "built $OUT/apple_kern_example"

stage "Sample kerning pairs from font A"
"$OUT/apple_kern_example" "$OUT/fontA.ttf" "$OUT/fontA_pairs.txt" | tee "$OUT/stb_A.txt"
stage "Sample kerning pairs from font B"
"$OUT/apple_kern_example" "$OUT/fontB.ttf" "$OUT/fontB_pairs.txt" | tee "$OUT/stb_B.txt"
stage "Sample kerning pairs from control font C (expect zeros)"
"$OUT/apple_kern_example" "$OUT/fontC.ttf" "$OUT/fontA_pairs.txt" | tee "$OUT/stb_C.txt"
stage "Determinism check: re-sample font A"
"$OUT/apple_kern_example" "$OUT/fontA.ttf" "$OUT/fontA_pairs.txt" > "$OUT/stb_A2.txt"
diff "$OUT/stb_A.txt" "$OUT/stb_A2.txt" && echo "deterministic: MATCH" || FAIL=1

stage "Compare font A vs fonttools reference"
diff "$OUT/ref_A.txt" "$OUT/stb_A.txt" && echo "font A: MATCH" || FAIL=1
stage "Compare font B vs fonttools reference"
diff "$OUT/ref_B.txt" "$OUT/stb_B.txt" && echo "font B: MATCH" || FAIL=1
stage "Compare control font C vs reference"
diff "$OUT/ref_C.txt" "$OUT/stb_C.txt" && echo "font C: MATCH (all zero)" || FAIL=1

stage "Summary"
echo "all artifacts in $OUT/"
[ "$FAIL" -eq 0 ] && echo "RESULT: PASS" || echo "RESULT: FAIL"
echo "total elapsed: $(( $(date +%s) - START ))s"
exit "$FAIL"
