#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <stdio.h>
int main(int c,char**v){for(int i=1;i<c;i++){stbi_info_res r=stbi_info2(v[i]);
 printf("%s -> type=%d %dx%d comp=%d\n",v[i],r.type,r.x,r.y,r.comp);}return 0;}
