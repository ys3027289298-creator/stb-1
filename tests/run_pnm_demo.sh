#!/bin/sh
# build + run the PNM writer demo; all artifacts land in ../output
set -e
cd "$(dirname "$0")/.."
OUT=output
mkdir -p "$OUT"

stage() {
  echo
  echo "==================== $1 ===================="
  sleep "$2"
}

stage "stage 1/4: compile pnm_write_test (stb_image_write + stb_image)" 4
cc -std=c99 -Wall -Wextra -Itests -I. tests/pnm_write_test.c -lm -o "$OUT/pnm_write_test" 2>&1 | tee "$OUT/build.log"
echo "build OK"

stage "stage 2/4: run 4 cases (8-bit PGM, 16-bit PGM, PPM, bad params)" 10
"$OUT/pnm_write_test" | tee "$OUT/pnm_run.log"

stage "stage 3/4: verify headers, maxval and big-endian 16-bit bytes" 10
for f in g8.pgm g16.pgm rgb.ppm bw.pbm; do
  echo "--- $f header ---"
  head -c 40 "$OUT/$f" | tr -c '[:print:]\n' '.' | sed -n '1,4p'
  echo
done | tee "$OUT/headers.log"
# PNM emits bottom row first; on-disk px0 is source i=8 -> 32000 = 0x7D00, big-endian: 7d 00
off=$(head -4 "$OUT/g16.pgm" | wc -c)
echo "g16 first 4 data bytes (expect 7d 00 8c a0, big-endian):" | tee -a "$OUT/headers.log"
tail -c +$((off + 1)) "$OUT/g16.pgm" | od -An -tx1 -N4 | tee -a "$OUT/headers.log"

stage "stage 4/4: compile existing image_write_test example as regression" 10
cc -std=c99 -DIWT_TEST -Itests -I. tests/image_write_test.c -lm -o "$OUT/image_write_test" 2>&1 | tee -a "$OUT/build.log"
"$OUT/image_write_test"
echo "regression example wrote:" && ls -1 "$OUT"/wr6x5_regular.* | tee -a "$OUT/filelist.log"
ls -1 "$OUT" | tee "$OUT/filelist.log"
echo
echo "all done; artifacts in $OUT/"
