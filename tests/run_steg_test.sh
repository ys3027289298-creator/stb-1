#!/bin/sh
# Builds and runs the steganography tests from tests/test_steg.c.
# Usage: tests/run_steg_test.sh   (run from the repository root or tests/)

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cc -I"$here/.." -O2 -Wall -Wextra -Werror -o "$here/test_steg" "$here/test_steg.c" -lm
"$here/test_steg"
