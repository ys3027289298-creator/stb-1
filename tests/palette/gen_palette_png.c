#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <stdio.h>

#define W 16
#define H 16

int main(void)
{
   unsigned char pal16[16 * 3], pal256[256 * 3], palA[8 * 4];
   unsigned char idx16[W * H], idx256[W * H], idxA[W * H], idxBad[W * H];
   int i, rc;
   for (i = 0; i < 256; ++i) {
      if (i < 16) { pal16[i*3] = (unsigned char)(i * 16); pal16[i*3+1] = (unsigned char)(255 - i * 16); pal16[i*3+2] = 128; }
      pal256[i*3] = (unsigned char)i; pal256[i*3+1] = (unsigned char)(255 - i); pal256[i*3+2] = (unsigned char)((i * 7) & 255);
   }
   for (i = 0; i < 8; ++i) {
      palA[i*4] = (unsigned char)(i * 30); palA[i*4+1] = 100; palA[i*4+2] = (unsigned char)(200 - i * 10);
      palA[i*4+3] = (i == 3) ? 0 : (i == 6 ? 128 : 255);
   }
   for (i = 0; i < W * H; ++i) {
      idx16[i] = (unsigned char)(i % 16);
      idx256[i] = (unsigned char)i;
      idxA[i] = (unsigned char)(i % 8);
      idxBad[i] = (unsigned char)(i % 16);
   }
   idxBad[10] = 200;

   rc  = stbi_write_png_indexed("output/pal16.png",  W, H, 3, pal16,  16,  idx16,  W);
   rc += stbi_write_png_indexed("output/pal256.png", W, H, 3, pal256, 256, idx256, W);
   rc += stbi_write_png_indexed("output/paltrns.png", W, H, 4, palA,  8,   idxA,   W);
   rc += stbi_write_png_indexed("output/palbad.png", W, H, 3, pal16,  16,  idxBad, W);
   printf("writes succeeded: %d of 4 (expect 3)\n", rc);
   return 0;
}
