#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <math.h>

int main(void)
{
   unsigned char px[8 * 8 * 3];
   for (int i = 0; i < 8 * 8 * 3; ++i) px[i] = (unsigned char)(i * 7);
   int dpi96  = (int)round(96.0  / 0.0254); /* 3780 px/m */
   int dpi300 = (int)round(300.0 / 0.0254); /* 11811 px/m */
   stbi_write_png_phys("output/phys_96dpi.png",      8, 8, 3, px, 24, dpi96,  dpi96);
   stbi_write_png_phys("output/phys_300dpi.png",     8, 8, 3, px, 24, dpi300, dpi300);
   stbi_write_png_phys("output/phys_honly.png",      8, 8, 3, px, 24, dpi96,  0);
   stbi_write_png     ("output/phys_none.png",       8, 8, 3, px, 24);
   return 0;
}
