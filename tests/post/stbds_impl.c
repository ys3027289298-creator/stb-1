// support file for linking tests/test_truetype.c (TT_TEST):
// stb_truetype's packing API needs stb_rect_pack, and stbds_unit_tests
// (unrelated to truetype) is stubbed out
#define STB_RECT_PACK_IMPLEMENTATION
#include "../../stb_rect_pack.h"
void stbds_unit_tests(void) {}
