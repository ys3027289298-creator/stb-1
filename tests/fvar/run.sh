#!/usr/bin/env bash
# End-to-end fvar validation: builds fonts, runs all scenarios, checks output.
set -u
cd "$(dirname "$0")"
OUT=output; mkdir -p "$OUT"; LOG="$OUT/run.log"; : > "$LOG"
stage() { echo "===== $1 =====" | tee -a "$LOG"; sleep "${2:-3}"; }
run()   { echo "+ $*" | tee -a "$LOG"; "$@" 2>&1 | tee -a "$LOG"; }

stage "阶段1/10 生成测试字体"
run python3 make_fonts.py
stage "阶段2/10 编译 fvar 测试程序"
run gcc -std=c99 -Wall -Wextra -I../.. fvar_test.c -lm -o "$OUT/fvar_test"
stage "阶段3/10 变体字体：单字重轴 wght"
run "$OUT/fvar_test" dump fonts/var_wght.ttf
stage "阶段4/10 变体字体：双轴 wght+wdth"
run "$OUT/fvar_test" dump fonts/var_two_axes.ttf
stage "阶段5/10 普通字体（无 fvar，退化为常规行为）" 4
run "$OUT/fvar_test" dump fonts/regular.ttf
stage "阶段6/10 损坏 fvar（解析失败时退化）"
run "$OUT/fvar_test" dump fonts/corrupt_fvar.ttf
stage "阶段7/10 越界与非法轴调用（打印错误码）" 4
run "$OUT/fvar_test" oob fonts/var_wght.ttf wght 950
run "$OUT/fvar_test" oob fonts/var_wght.ttf wght 50
run "$OUT/fvar_test" oob fonts/var_wght.ttf wght 700
run "$OUT/fvar_test" oob fonts/regular.ttf wght 700
run "$OUT/fvar_test" oob fonts/var_wght.ttf XXXX 700
stage "阶段8/10 既有接口回归（cmap/字形索引/度量）"
printf '%s\n' '#include <stdio.h>' '#define STB_TRUETYPE_IMPLEMENTATION' '#include "stb_truetype.h"' '#include <stdlib.h>' \
 'static unsigned char* load(const char*p,long*n){FILE*f=fopen(p,"rb");fseek(f,0,2);*n=ftell(f);fseek(f,0,0);unsigned char*b=malloc(*n);fread(b,1,*n,f);fclose(f);return b;}' \
 'int main(void){long n;unsigned char*b=load("fonts/var_wght.ttf",&n);stbtt_fontinfo f;if(!stbtt_InitFont(&f,b,0))return 1;int aw,lsb,a,d,g;' \
 'stbtt_GetFontVMetrics(&f,&a,&d,&g);stbtt_GetCodepointHMetrics(&f,65,&aw,&lsb);' \
 'printf("glyphA=%d vmet=(%d,%d,%d) hmet=(%d,%d)\n",stbtt_FindGlyphIndex(&f,65),a,d,g,aw,lsb);return 0;}' > "$OUT/compat.c"
run gcc -std=c99 -I../.. "$OUT/compat.c" -lm -o "$OUT/compat"
run "$OUT/compat"
stage "阶段9/10 校验输出"
ok=1
expect() { grep -q "$1" "$LOG" && echo "PASS: $1" | tee -a "$LOG" || { echo "FAIL: $1" | tee -a "$LOG"; ok=0; }; }
expect "axes=1"; expect "tag=wght min=100.0"; expect "named_instances=2"; expect "axes=2"
expect "tag=wdth min=75.0"; expect "regular.ttf axes=0"; expect "corrupt_fvar.ttf axes=0"
expect "error -3"; expect "error -1"; expect "error -2"; expect "glyphA=1"
stage "阶段10/10 结果汇总" 3
[ "$ok" -eq 1 ] && echo "ALL CHECKS PASSED" | tee -a "$LOG" || echo "CHECK FAILURES PRESENT" | tee -a "$LOG"
exit $((1-ok))
