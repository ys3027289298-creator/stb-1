#ifdef __clang__
#define STBIWDEF static inline
#endif
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <stdio.h>

// using an 'F' since it has no rotational symmetries, and 6x5
// because it's a small, atypical size likely to trigger edge cases.
//
// conveniently, it's also small enough to fully fit inside a typical
// directory listing thumbnail, which simplifies checking at a glance.
static const char img6x5_template[] =
   ".****."
   ".*...."
   ".***.."
   ".*...."
   ".*....";

void image_write_test(void)
{
   // make a RGB version of the template image
   // use red on blue to detect R<->B swaps
   unsigned char img6x5_rgb[6*5*3];
   float img6x5_rgbf[6*5*3];
   int i;

   for (i = 0; i < 6*5; i++) {
      int on = img6x5_template[i] == '*';
      img6x5_rgb[i*3 + 0] = on ? 255 : 0;
      img6x5_rgb[i*3 + 1] = 0;
      img6x5_rgb[i*3 + 2] = on ? 0 : 255;

      img6x5_rgbf[i*3 + 0] = on ? 1.0f : 0.0f;
      img6x5_rgbf[i*3 + 1] = 0.0f;
      img6x5_rgbf[i*3 + 2] = on ? 0.0f : 1.0f;
   }

   stbi_write_png("output/wr6x5_regular.png", 6, 5, 3, img6x5_rgb, 6*3);
   stbi_write_bmp("output/wr6x5_regular.bmp", 6, 5, 3, img6x5_rgb);
   stbi_write_tga("output/wr6x5_regular.tga", 6, 5, 3, img6x5_rgb);
   stbi_write_jpg("output/wr6x5_regular.jpg", 6, 5, 3, img6x5_rgb, 95);
   stbi_write_hdr("output/wr6x5_regular.hdr", 6, 5, 3, img6x5_rgbf);

   stbi_flip_vertically_on_write(1);

   stbi_write_png("output/wr6x5_flip.png", 6, 5, 3, img6x5_rgb, 6*3);
   stbi_write_bmp("output/wr6x5_flip.bmp", 6, 5, 3, img6x5_rgb);
   stbi_write_tga("output/wr6x5_flip.tga", 6, 5, 3, img6x5_rgb);
   stbi_write_jpg("output/wr6x5_flip.jpg", 6, 5, 3, img6x5_rgb, 95);
   stbi_write_hdr("output/wr6x5_flip.hdr", 6, 5, 3, img6x5_rgbf);
}

// stdio write callback: plain libc fwrite is used, so this path does not
// depend on the writer's FILE* bookkeeping.
static void color_file_cb(void *context, void *data, int size)
{
   fwrite(data, 1, (size_t) size, (FILE *) context);
}

static void write_png_cb(const char *path, const unsigned char *px)
{
   FILE *f = fopen(path, "wb");
   stbi_write_png_to_func(color_file_cb, f, 4, 2, 3, px, 4*3);
   fclose(f);
}

// PNG color-space chunk cases: sRGB, gAMA 1.8, gAMA 2.2, and no color chunk.
// Pixels are a small hand-made 4x2 RGB gradient (one byte per channel).
void image_write_color_test(void)
{
   unsigned char px[4*2*3];
   int i;
   for (i = 0; i < (int) sizeof(px); ++i) px[i] = (unsigned char) (i * 17);

#ifndef IWT_REF_BUILD
   stbi_write_png_srgb(1);
   write_png_cb("output/color/case_srgb.png", px);

   stbi_write_png_srgb(0);
   stbi_write_png_gamma(1.8f);
   write_png_cb("output/color/case_gama18.png", px);

   stbi_write_png_gamma(2.2f);
   write_png_cb("output/color/case_gama22.png", px);

   stbi_write_png_gamma(0.0f);
#endif

   write_png_cb("output/color/case_none.png", px);
}

#ifdef IWT_TEST
int main(int argc, char **argv)
{
#ifdef IWT_COLOR_ONLY
   image_write_color_test();
   return 0;
#endif
   image_write_test();
   image_write_color_test();
   return 0;
}
#endif
