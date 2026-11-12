#!/usr/bin/env bash
# Build EXIF materials, read each case switch off/on, verify pixels+geometry.
set -u
cd "$(dirname "$0")/../.."
mkdir -p output
LOG=output/exif_report.txt
: > "$LOG"
section() { echo | tee -a "$LOG"; echo "==== $* ====" | tee -a "$LOG"; sleep 2; }

section "1/8 build base.jpg via stb_image_write (in-memory callback)"
cc -O1 -Wall -I. tests/exif/make_base.c -o output/make_base -lm
./output/make_base 2>&1 | tee -a "$LOG"

section "2/8 inject EXIF orientations 1,3,6,8 plus a corrupt segment"
python3 tests/exif/make_exif_jpegs.py 2>&1 | tee -a "$LOG"
sleep 5

section "3/8 build reader demo against the modified stb_image.h"
cc -O1 -Wall -I. tests/exif/exif_demo.c -o output/exif_demo -lm 2>&1 | tee -a "$LOG"

section "4/8 four required cases, switch OFF then ON"
for o in 1 3 6 8; do
  ./output/exif_demo "orient_$o" "tests/exif/orient_$o.jpg" | tee -a "$LOG"
  sleep 2
done

section "5/8 corrupt and missing EXIF must fall back to orientation 1"
./output/exif_demo corrupt_exif tests/exif/corrupt_exif.jpg | tee -a "$LOG"; sleep 3
./output/exif_demo no_exif tests/exif/base.jpg | tee -a "$LOG"

section "6/8 pixel and geometry assertions"
python3 - "$LOG" <<'PY' | tee -a "$LOG"
import re, sys
t = open(sys.argv[1]).read()
def rows(c):
    m = re.search(r"CASE %s.*?(?=\nCASE|\Z)" % c, t, re.S).group(0)
    f = lambda k: re.search(k + r" topleft=\((\d+),(\d+),(\d+)\) size=(\d+)x(\d+)", m).groups()
    return f("switch=OFF"), f("switch=ON ")
exp = {"orient_1": ("8x12", "8x12", True), "orient_3": ("8x12", "8x12", False),
       "orient_6": ("8x12", "12x8", False), "orient_8": ("8x12", "12x8", False)}
ok = True
for c, (os0, os1, same) in exp.items():
    off, on = rows(c)
    good = ("%sx%s" % off[3:]) == os0 and ("%sx%s" % on[3:]) == os1
    good &= (off[:3] == on[:3]) == same
    print("%-9s %s" % (c, "PASS" if good else "FAIL")); ok &= good
for c in ("corrupt_exif", "no_exif"):
    off, on = rows(c)
    good = off == on and ("%sx%s" % off[3:]) == "8x12"
    print("%-9s %s" % (c, "PASS" if good else "FAIL")); ok &= good
sys.exit(0 if ok else 1)
PY
RC=${PIPESTATUS[0]}

section "7/8 switch OFF leaves stored pixels untouched (orient_6)"
./output/exif_demo orient_6 tests/exif/orient_6.jpg | grep OFF | tee -a "$LOG"

section "8/8 summary"
[ "$RC" -eq 0 ] && echo "ALL CHECKS PASSED" | tee -a "$LOG" || echo "CHECK FAILED" | tee -a "$LOG"; echo "report written to $LOG" | tee -a "$LOG"
exit "$RC"
