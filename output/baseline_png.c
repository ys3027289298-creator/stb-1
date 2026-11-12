#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <stdio.h>
int main(void){
  unsigned char rgb[6*5*3]; int i;
  for(i=0;i<6*5;i++){rgb[i*3]= (i%2)?255:0; rgb[i*3+1]=40; rgb[i*3+2]=90;}
  if(!stbi_write_png("output/base_rgb8.png",6,5,3,rgb,6*3)) return 2;
  unsigned short rg16[4*3*2];
  for(i=0;i<4*3;i++){rg16[i*2]=(unsigned short)(i*1000); rg16[i*2+1]=(unsigned short)(60000-i*777);}
  /* this version has no 16-bit API; gray+rgba8 instead for coverage */
  unsigned char rgba[4*3*4];
  for(i=0;i<4*3;i++){rgba[i*4]=i;rgba[i*4+1]=200;rgba[i*4+2]=64;rgba[i*4+3]=128;}
  if(!stbi_write_png("output/base_rgba8.png",4,3,4,rgba,4*4)) return 3;
  unsigned char gray[5*2];
  for(i=0;i<10;i++) gray[i]=(unsigned char)(i*25);
  if(!stbi_write_png("output/base_gray8.png",5,2,1,gray,5)) return 4;
  printf("baseline png ok\n");
  return 0;
}
