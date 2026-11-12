#!/usr/bin/env bash
# End-to-end check for PNG iCCP support. No keyboard input required.
# Prints stage headers, pauses between stages, finishes in 30-90s.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$ROOT/output"
BUILD="$HERE/build"

stage() {
   echo ""
   echo "==================== STAGE: $1 ===================="
   sleep 3
}

stage "1/9 prepare output dirs"
mkdir -p "$OUT" "$BUILD"
rm -f "$OUT"/case_*.png
echo "output dir: $OUT"

stage "2/9 generate fake ICC data (<=4KB)"
python3 "$HERE/gen_fake_icc.py"
ls -l "$HERE/fake.icc"

stage "3/9 build iCCP case writer"
cc -O1 -Wall -I"$ROOT" "$HERE/write_cases.c" -o "$BUILD/write_cases"
echo "built $BUILD/write_cases"

stage "4/9 repo's own image_write_test (regression)"
cc -O1 -I"$ROOT" -DIWT_TEST "$ROOT/tests/image_write_test.c" -lm -o "$BUILD/image_write_test"
( cd "$BUILD" && ./image_write_test )
echo "repo image_write_test OK"

stage "5/9 C++ compile check (repo test_cpp_compilation)"
g++ -fsyntax-only -I"$ROOT" "$ROOT/tests/test_cpp_compilation.cpp"
echo "C++ syntax OK"

stage "6/9 write 4 PNG cases"
"$BUILD/write_cases" "$OUT" "$HERE/fake.icc"

stage "7/9 list written PNGs"
ls -l "$OUT"

stage "8/9 read back and verify iCCP chunks"
python3 "$HERE/check_iccp.py" "$OUT"

stage "9/9 summary"
echo "PNGs are in: $OUT"
ls "$OUT"
sleep 6
echo ""
echo "ALL STAGES DONE"
