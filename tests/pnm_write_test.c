#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image_write.h"
#include "stb_image.h"
#include <stdio.h>

#define W 4
#define H 3
#define D "output/"

static void show8(const char *f)
{
   int w = 0, h = 0, c = 0;
   unsigned char *p = stbi_load(f, &w, &h, &c, 1);
   printf("%s w=%d h=%d depth=%d comp=%d sample[0]=%u sample[last]=%u\n",
          f, w, h, p ? 8 : 0, c, p ? p[0] : 0, p ? p[w * h * c - 1] : 0);
   stbi_image_free(p);
}

static void show16(const char *f)
{
   int w = 0, h = 0, c = 0;
   unsigned short *p = stbi_load_16(f, &w, &h, &c, 1);
   printf("%s w=%d h=%d depth=%d comp=%d sample[0]=%u sample[1]=%u\n",
          f, w, h, p ? 16 : 0, c, p ? p[0] : 0, p ? p[1] : 0);
   stbi_image_free(p);
}

int main(void)
{
   unsigned char g8[W * H], rgb[W * H * 3], bm[W * H] = {1, 0, 1, 0};
   unsigned short g16[W * H];
   int i;
   for (i = 0; i < W * H; i++) { g8[i] = (unsigned char) (i * 20); g16[i] = (unsigned short) (i * 4000); }
   for (i = 0; i < W * H * 3; i++) rgb[i] = (unsigned char) (i * 7);

   printf("== case1: 8-bit PGM P5 ==\n");
   stbi_write_pnm(D "g8.pgm", W, H, 1, g8);
   show8(D "g8.pgm");

   printf("== case2: 16-bit PGM P5 (big-endian) ==\n");
   stbi_write_pnm16(D "g16.pgm", W, H, 1, g16);
   show16(D "g16.pgm");

   printf("== case3: RGB PPM P6 ==\n");
   stbi_write_pnm(D "rgb.ppm", W, H, 3, rgb);
   show8(D "rgb.ppm");

   printf("== case4: bad parameters (expect all zero) ==\n");
   printf("bad comp=%d bad dims=%d null=%d\n",
          stbi_write_pnm(D "bad1.pgm", W, H, 9, g8),
          stbi_write_pnm16(D "bad2.pgm", 0, H, 1, g16),
          stbi_write_pbm(D "bad3.pbm", W, H, 1, (void *) 0));

   printf("== optional: PBM P4 header check ==\n");
   if (stbi_write_pbm(D "bw.pbm", W, H, 1, bm))
      printf("bw.pbm written, header: ");
   return 0;
}
