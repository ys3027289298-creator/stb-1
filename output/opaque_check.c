#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
int main(void){
  unsigned char pal[3*4] = {1,2,3,255, 4,5,6,255, 7,8,9,255};
  unsigned char idx[3] = {0,1,2};
  return stbi_write_png_indexed("output/opaque_rgba.png",3,1,4,pal,3,idx,3) ? 0 : 1;
}
