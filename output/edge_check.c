#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <stdio.h>
#include <string.h>
static int calls;
static void cb(void *ctx, void *data, int size){ (void)ctx;(void)data;(void)size; calls++; }
int main(void){
  unsigned char pal[4*4] = {10,20,30,255, 40,50,60,255, 70,80,90,255, 100,110,120,128};
  unsigned char idx[4] = {0,1,2,3};
  calls = 0;
  int r1 = stbi_write_png_indexed_to_func(cb,0,4,1,4,pal,4,idx,4);
  printf("func ok=%d calls=%d (want 1,1)\n", r1, calls);
  unsigned char bad[4] = {0,1,4,3};
  calls = 0;
  int r2 = stbi_write_png_indexed_to_func(cb,0,4,1,4,pal,4,bad,4);
  printf("func bad=%d calls=%d (want 0,0)\n", r2, calls);
  int r3 = stbi_write_png_indexed("output/edge.png",4,1,5,pal,4,idx,4);
  int r4 = stbi_write_png_indexed("output/edge.png",4,1,3,pal,257,idx,4);
  int r5 = stbi_write_png_indexed("output/edge.png",0,1,3,pal,4,idx,0);
  printf("bad comp=%d pal257=%d dim0=%d (want 0,0,0)\n", r3,r4,r5);
  stbi_flip_vertically_on_write(1);
  unsigned char pal2[2*3] = {1,2,3, 4,5,6};
  unsigned char two[4] = {0,1, 1,0};
  int r6 = stbi_write_png_indexed("output/edge_flip.png",2,2,3,pal2,2,two,2);
  stbi_flip_vertically_on_write(0);
  printf("flip write=%d (want 1)\n", r6);
  return 0;
}
