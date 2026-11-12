#include <stdio.h>
#include <stdlib.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../stb_image_write.h"

/* Generates the pre-change 8-bit baseline with byte-identical input to the
 * regression case in png16_demo.c. Run once against the original library;
 * the output PNG is kept under output/ as the regression reference. */
int main(void)
{
   int w = 128, h = 64, comp = 3;
   size_t count = (size_t) w * h * comp;
   unsigned char *data = malloc(count);
   size_t i;
   for (i = 0; i < count; ++i)
      data[i] = (unsigned char) ((i * 37 + 11) & 0xff);
   if (!stbi_write_png("output/baseline_8bit.png", w, h, comp, data, 0)) {
      fprintf(stderr, "baseline write failed\n");
      return 1;
   }
   free(data);
   printf("[baseline] output/baseline_8bit.png written via 8-bit stbi_write_png\n");
   return 0;
}
