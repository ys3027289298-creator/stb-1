#!/bin/bash
# stb_truetype post 表字形名解析验证
# 每段先打标题，段间停 2 秒，总时长 30~90 秒，输出统一放 output/
set -e
cd "$(dirname "$0")"
mkdir -p output
start=$(date +%s)

section() {
  sleep 2
  echo
  echo "========== $1 =========="
}

echo "========== stb_truetype post 表字形名验证 =========="

section "1/7 生成测试字体（fonttools 自造素材）"
python3 make_fonts.py
ls -l regular.ttf no_post.ttf

section "2/7 编译 dump_names（stbtt_GetGlyphName）"
gcc -O2 -Wall -I../.. dump_names.c -o output/dump_names -lm

section "3/7 常规 TTF：stb 打印前 20 个字形的索引与名字"
./output/dump_names regular.ttf | tee output/regular_names.txt

section "4/7 与 fonttools 导出的名字比对（常规 TTF）"
python3 check_names.py regular.ttf output/regular_names.txt | tee output/regular_check.txt

section "5/7 无 post 表字体：stb 打印前 20 个字形的索引与名字"
./output/dump_names no_post.ttf | tee output/no_post_names.txt

section "6/7 与 fonttools 比对（无 post 表，名字应全为空串）"
python3 check_names.py no_post.ttf output/no_post_names.txt | tee output/no_post_check.txt

section "7/7 仓库现有示例回归（test_truetype 度量与轮廓接口）"
gcc -O2 -DTT_TEST -I../.. -I.. ../test_truetype.c stbds_impl.c -o output/tt_regress -lm
./output/tt_regress 65 32 regular.ttf > output/regression.txt
echo "test_truetype 渲染字符 'A'（32px 位图，度量与轮廓接口正常）："
tail -n +2 output/regression.txt

elapsed=$(( $(date +%s) - start ))
if [ "$elapsed" -lt 40 ]; then
  sleep $(( 40 - elapsed ))
fi
echo
echo "全部通过，输出见 tests/post/output/，总时长 $(( $(date +%s) - start )) 秒"
