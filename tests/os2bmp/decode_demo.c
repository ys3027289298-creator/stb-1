#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <stdio.h>

static int header_bpp(const char *path)
{
   FILE *f = fopen(path, "rb");
   unsigned char h[32];
   int hsz, bpp = -1;
   size_t got;
   if (!f) return -1;
   got = fread(h, 1, sizeof(h), f);
   if (got >= 26 && h[0] == 'B' && h[1] == 'M') {
      hsz = h[14] | (h[15] << 8) | (h[16] << 16) | (h[17] << 24);
      if (hsz == 12)       bpp = h[24] | (h[25] << 8);  // OS/2 1.x: 16-bit cx/cy
      else if (got >= 30)  bpp = h[28] | (h[29] << 8);  // OS/2 2.x and Windows: 32-bit cx/cy
   }
   fclose(f);
   return bpp;
}

int main(int argc, char **argv)
{
   int i, x, y, n, shown;
   stbi_uc *img;
   if (argc < 2) { fprintf(stderr, "usage: %s file.bmp ...\n", argv[0]); return 2; }

   for (i = 1; i < argc; ++i) {
      printf("file:   %s\n", argv[i]);
      img = stbi_load(argv[i], &x, &y, &n, 3);
      if (!img) {
         printf("result: decode failed: %s\n\n", stbi_failure_reason());
         continue;
      }
      printf("size:   %dx%d\n", x, y);
      printf("bpp:    %d\n", header_bpp(argv[i]));
      shown = x * y < 8 ? x * y : 8;
      printf("pixels:");
      for (int p = 0; p < shown; ++p)
         printf(" (%d,%d,%d)", img[p * 3], img[p * 3 + 1], img[p * 3 + 2]);
      printf("\n\n");
      stbi_image_free(img);
   }
   return 0;
}
