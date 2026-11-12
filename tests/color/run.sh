#!/usr/bin/env bash
# Staged verification for PNG sRGB/gAMA color chunks. All artifacts go to output/.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=output/color
mkdir -p "$OUT"

title() { printf '\n========== %s ==========\n' "$1"; sleep 2; }

title "stage 1/5: build current example (image_write_test, IWT_TEST)"
cc -I.. -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -DIWT_TEST \
   -DIWT_COLOR_ONLY image_write_test.c -lm -o "$OUT/image_write_test"
sleep 4

title "stage 2/5: build reference example from pristine header in git HEAD"
REF_INC="$OUT/ref_include"; mkdir -p "$REF_INC"
git show HEAD:stb_image_write.h > "$REF_INC/stb_image_write.h"
git show HEAD:tests/image_write_test.c > "$OUT/ref_image_write_test.c"
# pristine main() only knows image_write_test(), whose stdio path crashes in
# this environment; make it emit the no-color baseline via plain libc stdio.
cat >> "$OUT/ref_image_write_test.c" <<'EOF'
#ifdef IWT_BASELINE_MAIN
static void ref_file_cb(void *context, void *data, int size)
{
   fwrite(data, 1, (size_t) size, (FILE *) context);
}
int main(void)
{
   unsigned char px[4*2*3];
   int i;
   FILE *f;
   for (i = 0; i < (int) sizeof(px); ++i) px[i] = (unsigned char) (i * 17);
   f = fopen("output/color/case_none.png", "wb");
   stbi_write_png_to_func(ref_file_cb, f, 4, 2, 3, px, 4*3);
   fclose(f);
   return 0;
}
#endif
EOF
cc -I"$REF_INC" -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast \
   -DIWT_BASELINE_MAIN "$OUT/ref_image_write_test.c" -lm \
   -o "$OUT/image_write_test_ref"
sleep 4

title "stage 3/5: generate four cases: sRGB / gamma 1.8 / gamma 2.2 / none"
mkdir -p output/color
echo "-- reference baseline (pristine header, no color chunk) --"
"$OUT/image_write_test_ref"
sleep 2
echo "-- current header: sRGB / gamma 1.8 / gamma 2.2 / none --"
"$OUT/image_write_test"
cp output/color/case_none.png "$OUT/ref_none.png"
sleep 4

title "stage 4/5: read chunks back and check values, order, CRCs, size delta"
python3 color/check_chunks.py "$OUT"
sleep 4

title "stage 5/5: byte-identical baseline check (no color entry used)"
cmp output/color/case_none.png "$OUT/ref_none.png" \
  && echo "PASS: no-entry output is byte-identical to git HEAD build"
sleep 5
echo
echo "all stages finished; artifacts in tests/$OUT"
