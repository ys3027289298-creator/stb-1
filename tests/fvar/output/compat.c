#include <stdio.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <stdlib.h>
static unsigned char* load(const char*p,long*n){FILE*f=fopen(p,"rb");fseek(f,0,2);*n=ftell(f);fseek(f,0,0);unsigned char*b=malloc(*n);fread(b,1,*n,f);fclose(f);return b;}
int main(void){long n;unsigned char*b=load("fonts/var_wght.ttf",&n);stbtt_fontinfo f;if(!stbtt_InitFont(&f,b,0))return 1;int aw,lsb,a,d,g;
stbtt_GetFontVMetrics(&f,&a,&d,&g);stbtt_GetCodepointHMetrics(&f,65,&aw,&lsb);
printf("glyphA=%d vmet=(%d,%d,%d) hmet=(%d,%d)\n",stbtt_FindGlyphIndex(&f,65),a,d,g,aw,lsb);return 0;}
