#!/usr/bin/env bash
# Five-stage 16-bit PNG writer demo. All artifacts land in output/.
# No interactive input is required.
set -u

cd "$(dirname "$0")"
mkdir -p output

PY=../.venv-png16/bin/python
CC=${CC:-gcc}
CFLAGS="-std=c11 -Wall -Wextra -O2"

echo "================================================================"
echo "STAGE 1 - environment"
echo "================================================================"
echo "pwd    : $(pwd)"
echo "repo   : $(dirname "$PWD")"
echo "output : $PWD/output"
echo "cc     : $($CC --version | head -1)"
echo "python : $($PY --version 2>&1) ($PY)"

echo
echo "================================================================"
echo "STAGE 2 - build"
echo "================================================================"
run() {
   echo "+ $*"
   "$@"
   local rc=$?
   echo "exit_code=$rc"
   return $rc
}

run $CC $CFLAGS make_baseline_8bit.c -o output/make_baseline_8bit || exit 1
run $CC $CFLAGS png16_demo.c -o output/png16_demo -lm || exit 1

echo
echo "================================================================"
echo "STAGE 3 - write cases (16-bit gray / RGB / RGBA / error cases + 8-bit)"
echo "================================================================"
# Baseline is produced by the same 8-bit code path the library always had.
run ./output/make_baseline_8bit || exit 1
run ./output/png16_demo || exit 1
ls -l output

echo
echo "================================================================"
echo "STAGE 4 - read back with Python (pypng + Pillow)"
echo "================================================================"
run "$PY" verify.py || exit 1

echo
echo "================================================================"
echo "STAGE 5 - regression summary"
echo "================================================================"
"$PY" - <<'EOF'
import array, png
b = png.Reader(filename="output/baseline_8bit.png").read()
n = png.Reader(filename="output/regression_8bit.png").read()
fb, fn = array.array("B"), array.array("B")
fb.frombytes(b"".join(b[2]))
fn.frombytes(b"".join(n[2]))
d = sum(1 for x, y in zip(fb, fn) if x != y)
print(f"8-bit regression pixel diffs vs baseline: {d} (expected 0)")
raise SystemExit(1 if d else 0)
EOF
rc=$?
echo "exit_code=$rc"
echo
echo "ALL STAGES COMPLETE"
exit $rc
