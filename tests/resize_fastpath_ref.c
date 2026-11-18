// Reference build of stb_image_resize with ALL fast paths disabled
// (STBIR_NO_FAST_PATHS): byte-identical to the library before the
// fast-path change. The test binary compares the fast-path build
// against this one pixel by pixel.
//
// STBIRDEF static gives this translation unit its own private copy of
// the library so it can be linked together with the instrumented one.

// The reference must always keep the general resampler, even when the
// test binary as a whole is compiled with -DSTBIR_NO_GENERAL_FALLBACK.
#undef STBIR_NO_GENERAL_FALLBACK

#define STBIR_NO_FAST_PATHS
#define STBIRDEF static
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

int ref_resize_uint8(const unsigned char* input, int in_w, int in_h,
                     unsigned char* output, int out_w, int out_h, int channels)
{
    return stbir_resize_uint8(input, in_w, in_h, 0, output, out_w, out_h, 0, channels);
}
