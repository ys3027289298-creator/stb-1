#!/bin/sh
# build + run the Softimage PIC writer demo; all artifacts land in output/
set -e
cd "$(dirname "$0")/.."
OUT=output
mkdir -p "$OUT"

stage() {
   echo
   echo "==================== $1 ===================="
   sleep "$2"
}

stage "stage 1/4: compile pic_write_test (stb_image_write PIC writer + PIC-capable stb_image)" 6
cc -std=c99 -Wall -Wextra -Itests -I. tests/pic_write_test.c -lm -o "$OUT/pic_write_test" 2>&1 | tee "$OUT/pic_build.log"
echo "build OK"
sleep 4

stage "stage 2/4: four cases (small, large, RGBA, bad params); write then read back, print diffs" 6
"$OUT/pic_write_test" | tee "$OUT/pic_run.log"
sleep 4

stage "stage 3/4: inspect 104-byte header, packet chain and RLE/raw compression bytes" 6
echo "--- small_rle.pic first 112 bytes (magic, 80-byte comment, PICT, dims, packets) ---"
od -An -tx1 -N112 "$OUT/small_rle.pic" | tee "$OUT/pic_headers.log"
sleep 4
echo "--- packet chain: RLE type=2 (small_rle) vs uncompressed type=0 (small_raw) ---"
od -An -tx1 -j104 -N4 "$OUT/small_rle.pic" | tee -a "$OUT/pic_headers.log"
od -An -tx1 -j104 -N4 "$OUT/small_raw.pic" | tee -a "$OUT/pic_headers.log"
ls -la "$OUT"/*.pic | tee -a "$OUT/pic_headers.log"
sleep 4

stage "stage 4/4: compile repo example image_write_test as regression" 6
cc -std=c99 -DIWT_TEST -Itests -I. tests/image_write_test.c -lm -o "$OUT/image_write_test" 2>>"$OUT/pic_build.log"
"$OUT/image_write_test"
ls -1 "$OUT" | tee "$OUT/pic_filelist.log"
echo
echo "all done; artifacts in $OUT/"
sleep 4

# non-interactive replay recording (no keyboard):
#   asciinema rec --overwrite output/demo.cast -c "sh tests/run_pic_demo.sh"
# asciinema 3.x here restarts per-event timestamps after idle gaps, so re-base
# the cast onto one cumulative monotonic time axis:
#   python3 - "$OUT/demo.cast" <<'PY'
#   import json,sys; p=sys.argv[1]; L=open(p).read().splitlines()
#   h=json.loads(L[0]); E=[json.loads(x) for x in L[1:]]; b=0.0; O=[json.dumps(h)]
#   for e in E: b+=max(0.0,e[0]); O.append(json.dumps([round(b,3),e[1],e[2]]))
#   open(p,"w").write("\n".join(O)+"\n")
#   PY
