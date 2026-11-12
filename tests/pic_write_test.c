#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image_write.h"
#include "../deprecated/stb_image_pic.h"
#include <stdio.h>
#include <string.h>

#define D "output/"

static int roundtrip(const char *path, int w, int h, int comp, const unsigned char *src)
{
   int rw = 0, rh = 0, rc = 0, diff = 0, i;
   unsigned char *got = stbi_load(path, &rw, &rh, &rc, comp);
   if (!got) {
      printf("%s: stbi_load FAILED (%s)\n", path, stbi_failure_reason());
      return 1;
   }
   if (rw != w || rh != h) {
      printf("%s: dims mismatch got %dx%d want %dx%d\n", path, rw, rh, w, h);
      diff = w * h * comp;
   }
   for (i = 0; i < w * h * comp; i++)
      if (got[i] != src[i])
         diff++;
   printf("%s: load %dx%d comp=%d, differing pixels/bytes=%d\n", path, rw, rh, rc, diff);
   stbi_image_free(got);
   return diff;
}

int main(void)
{
   enum { SW = 7, SH = 5, BW = 256, BH = 192 };
   unsigned char small[SW * SH * 3], big[BW * BH * 3], rgba[SW * SH * 4];
   int i, fails = 0;

   for (i = 0; i < SW * SH * 3; i++)
      small[i] = (unsigned char)(i * 37 + (i % 5 == 0 ? 0 : 11));
   for (i = 0; i < BW * BH * 3; i++)
      big[i] = (unsigned char)((i * 131 + (i / 3) * 7) & 0xff);
   for (i = 0; i < SW * SH * 4; i++)
      rgba[i] = (unsigned char)(i * 53 + 5);

   printf("== case1: small %dx%d RGB, RLE + uncompressed ==\n", SW, SH);
   stbi_write_pic_with_rle = 1;
   stbi_write_pic(D "small_rle.pic", SW, SH, 3, small);
   stbi_write_pic_with_rle = 0;
   stbi_write_pic(D "small_raw.pic", SW, SH, 3, small);
   fails += roundtrip(D "small_rle.pic", SW, SH, 3, small) > 0;
   fails += roundtrip(D "small_raw.pic", SW, SH, 3, small) > 0;

   printf("== case2: large %dx%d RGB (long runs/raw spans), RLE ==\n", BW, BH);
   stbi_write_pic_with_rle = 1;
   stbi_write_pic(D "big_rle.pic", BW, BH, 3, big);
   fails += roundtrip(D "big_rle.pic", BW, BH, 3, big) > 0;

   printf("== case3: %dx%d RGBA with alpha, RLE + uncompressed ==\n", SW, SH);
   stbi_write_pic(D "rgba_rle.pic", SW, SH, 4, rgba);
   stbi_write_pic_with_rle = 0;
   stbi_write_pic(D "rgba_raw.pic", SW, SH, 4, rgba);
   fails += roundtrip(D "rgba_rle.pic", SW, SH, 4, rgba) > 0;
   fails += roundtrip(D "rgba_raw.pic", SW, SH, 4, rgba) > 0;

   printf("== case4: bad parameters (expect all returns 0) ==\n");
   printf("bad comp=%d zero dims=%d null data=%d\n",
          stbi_write_pic(D "bad1.pic", SW, SH, 2, small),
          stbi_write_pic(D "bad2.pic", 0, SH, 3, big),
          stbi_write_pic(D "bad3.pic", SW, SH, 3, (void *)0));

   printf(fails ? "RESULT: %d ROUNDTRIP FAILURE(S)\n" : "RESULT: all roundtrips identical\n", fails);
   return fails ? 1 : 0;
}
