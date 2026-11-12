#!/usr/bin/env bash
# GSUB single/ligature substitution demo, driven by the repo's own
# tests/test_truetype.c (TT_GSUB_TEST mode). No keyboard input required.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=output
CC=${CC:-gcc}
SAMPLES=("ffi" "fi" "affi" "ffiafffi" "fff")

banner() { printf '\n================ %s ================\n' "$1"; sleep 6; }

banner "1/4 build tiny test fonts"
python3 "$OUT/make_fonts.py"
ls -la "$OUT"/*.ttf

banner "2/4 compile the bundled truetype example"
$CC -std=c99 -Wall -Wextra -DTT_TEST -DTT_GSUB_TEST -I. \
    tests/test_c_compilation.c tests/test_truetype.c -lm -o "$OUT/tt_gsub_test"
echo "built $OUT/tt_gsub_test"

banner "3/4 font WITH gsub: glyph sequences and ligature counts"
for t in "${SAMPLES[@]}"; do
  echo "-- sample text: $t"
  "$OUT/tt_gsub_test" "$OUT/gsub_font.ttf" "$t"
  sleep 4
done

banner "4/4 plain font (no GSUB) and range-coverage font"
echo "-- plain font, sample: affi"
"$OUT/tt_gsub_test" "$OUT/plain_font.ttf" affi
sleep 6
echo "-- range coverage font, samples: abcd / abx"
"$OUT/tt_gsub_test" "$OUT/range_font.ttf" abcd
sleep 4
"$OUT/tt_gsub_test" "$OUT/range_font.ttf" abx

printf '\n================ done ================\n'
