// dump the first N glyph names of a font via stbtt_GetGlyphName
#include <stdio.h>
#include <stdlib.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "../../stb_truetype.h"

static unsigned char data[1 << 20];

int main(int argc, char **argv)
{
   stbtt_fontinfo font;
   char name[128];
   int i, n = 20;
   FILE *f;

   if (argc < 2) {
      printf("usage: %s font.ttf [count]\n", argv[0]);
      return 1;
   }
   f = fopen(argv[1], "rb");
   if (!f) {
      printf("cannot open %s\n", argv[1]);
      return 1;
   }
   fread(data, 1, sizeof(data), f);
   fclose(f);

   if (!stbtt_InitFont(&font, data, stbtt_GetFontOffsetForIndex(data, 0))) {
      printf("stbtt_InitFont failed\n");
      return 1;
   }
   if (argc > 2)
      n = atoi(argv[2]);
   for (i = 0; i < n; ++i)
      printf("%d %s\n", i, stbtt_GetGlyphName(&font, i, name, sizeof(name)));
   return 0;
}
